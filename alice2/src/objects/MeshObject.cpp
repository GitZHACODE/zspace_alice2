#include "MeshObject.h"
#include "../core/Renderer.h"
#include "../core/Camera.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <array>

namespace alice2 {

    namespace {
        // Curvature estimators follow standard discrete differential geometry:
        // - Meyer, Desbrun, Schroder, Barr, "Discrete Differential-Geometry Operators
        //   for Triangulated 2-Manifolds", 2003. https://doi.org/10.1007/978-3-662-05105-4_2
        // - Rusinkiewicz, "Estimating Curvatures and Their Derivatives on Triangle Meshes",
        //   3DPVT 2004. https://gfx.cs.princeton.edu/pubs/Rusinkiewicz_2004_ECA/
        constexpr float kPi = 3.14159265358979323846f;
        constexpr float kEpsilon = 1e-8f;

        struct CurvatureData {
            std::vector<float> gaussian;
            std::vector<float> mean;
            std::vector<float> area;
            std::vector<bool> boundary;
            std::vector<std::vector<int>> neighbors;
        };

        struct EdgeKey {
            int a;
            int b;

            EdgeKey(int v0, int v1) : a(std::min(v0, v1)), b(std::max(v0, v1)) {}

            bool operator==(const EdgeKey& other) const {
                return a == other.a && b == other.b;
            }
        };

        struct EdgeKeyHash {
            std::size_t operator()(const EdgeKey& key) const {
                return std::hash<int>()(key.a) ^ (std::hash<int>()(key.b) << 1);
            }
        };

        float triangleArea(const Vec3& a, const Vec3& b, const Vec3& c) {
            return 0.5f * (b - a).cross(c - a).length();
        }

        float vertexAngle(const Vec3& center, const Vec3& a, const Vec3& b) {
            Vec3 u = a - center;
            Vec3 v = b - center;
            const float ul = u.length();
            const float vl = v.length();
            if (ul <= kEpsilon || vl <= kEpsilon) return 0.0f;
            float cosine = u.dot(v) / (ul * vl);
            cosine = std::clamp(cosine, -1.0f, 1.0f);
            return std::acos(cosine);
        }

        float cotangent(const Vec3& a, const Vec3& b) {
            const float crossLength = a.cross(b).length();
            if (crossLength <= kEpsilon) return 0.0f;
            return a.dot(b) / crossLength;
        }

        void addNeighbor(std::vector<std::vector<int>>& neighbors, int a, int b) {
            auto& list = neighbors[a];
            if (std::find(list.begin(), list.end(), b) == list.end()) {
                list.push_back(b);
            }
        }

        std::vector<std::array<int, 3>> buildTriangles(const MeshData& data) {
            std::vector<std::array<int, 3>> triangles;
            for (const auto& face : data.faces) {
                if (face.vertices.size() < 3) continue;
                for (size_t i = 1; i + 1 < face.vertices.size(); ++i) {
                    const int a = face.vertices[0];
                    const int b = face.vertices[i];
                    const int c = face.vertices[i + 1];
                    const int vertexCount = static_cast<int>(data.vertices.size());
                    if (a >= 0 && a < vertexCount &&
                        b >= 0 && b < vertexCount &&
                        c >= 0 && c < vertexCount &&
                        a != b && b != c && c != a) {
                        triangles.push_back({a, b, c});
                    }
                }
            }
            return triangles;
        }

        Color scalarToColor(float value, float minValue, float maxValue) {
            if (maxValue - minValue <= kEpsilon) {
                return Color(1.0f, 1.0f, 1.0f, 1.0f);
            }

            const float zeroRange = std::max(std::abs(minValue), std::abs(maxValue));
            float t = 0.5f;
            if (zeroRange > kEpsilon) {
                t = 0.5f + 0.5f * std::clamp(value / zeroRange, -1.0f, 1.0f);
            } else {
                t = (value - minValue) / (maxValue - minValue);
            }

            if (t < 0.5f) {
                return Color::lerp(Color(0.0f, 0.2f, 1.0f, 1.0f), Color(1.0f, 1.0f, 1.0f, 1.0f), t * 2.0f);
            }
            return Color::lerp(Color(1.0f, 1.0f, 1.0f, 1.0f), Color(1.0f, 0.05f, 0.15f, 1.0f), (t - 0.5f) * 2.0f);
        }

        void minMaxValues(const std::vector<float>& values, float& minValue, float& maxValue) {
            if (values.empty()) {
                minValue = 0.0f;
                maxValue = 0.0f;
                return;
            }

            minValue = values.front();
            maxValue = values.front();
            for (float value : values) {
                minValue = std::min(minValue, value);
                maxValue = std::max(maxValue, value);
            }
        }

        void updateVertexColors(MeshData& data, const std::vector<float>& values, float minValue, float maxValue) {
            const size_t count = std::min(data.vertices.size(), values.size());
            for (size_t i = 0; i < count; ++i) {
                data.vertices[i].color = scalarToColor(values[i], minValue, maxValue);
            }
        }

        void colorRange(float actualMin, float actualMax,
                        std::optional<float> remapMin, std::optional<float> remapMax,
                        float& colorMin, float& colorMax) {
            colorMin = actualMin;
            colorMax = actualMax;

            if (remapMin && remapMax && *remapMax > *remapMin) {
                colorMin = *remapMin;
                colorMax = *remapMax;
            }
        }

        CurvatureData computeCurvatureData(const MeshData& data) {
            const size_t vertexCount = data.vertices.size();
            CurvatureData result;
            result.gaussian.assign(vertexCount, 0.0f);
            result.mean.assign(vertexCount, 0.0f);
            result.area.assign(vertexCount, 0.0f);
            result.boundary.assign(vertexCount, false);
            result.neighbors.assign(vertexCount, {});

            const auto triangles = buildTriangles(data);
            std::vector<float> angleSum(vertexCount, 0.0f);
            std::vector<Vec3> meanNormal(vertexCount, Vec3(0, 0, 0));
            std::unordered_map<EdgeKey, int, EdgeKeyHash> edgeUse;

            for (const auto& tri : triangles) {
                const int i0 = tri[0];
                const int i1 = tri[1];
                const int i2 = tri[2];
                const Vec3& p0 = data.vertices[i0].position;
                const Vec3& p1 = data.vertices[i1].position;
                const Vec3& p2 = data.vertices[i2].position;

                const float area = triangleArea(p0, p1, p2);
                if (area <= kEpsilon) continue;

                result.area[i0] += area / 3.0f;
                result.area[i1] += area / 3.0f;
                result.area[i2] += area / 3.0f;

                angleSum[i0] += vertexAngle(p0, p1, p2);
                angleSum[i1] += vertexAngle(p1, p2, p0);
                angleSum[i2] += vertexAngle(p2, p0, p1);

                const float cot0 = cotangent(p1 - p0, p2 - p0);
                const float cot1 = cotangent(p2 - p1, p0 - p1);
                const float cot2 = cotangent(p0 - p2, p1 - p2);

                meanNormal[i1] += (p2 - p1) * cot0;
                meanNormal[i2] += (p1 - p2) * cot0;
                meanNormal[i2] += (p0 - p2) * cot1;
                meanNormal[i0] += (p2 - p0) * cot1;
                meanNormal[i0] += (p1 - p0) * cot2;
                meanNormal[i1] += (p0 - p1) * cot2;

                ++edgeUse[EdgeKey(i0, i1)];
                ++edgeUse[EdgeKey(i1, i2)];
                ++edgeUse[EdgeKey(i2, i0)];

                addNeighbor(result.neighbors, i0, i1);
                addNeighbor(result.neighbors, i1, i0);
                addNeighbor(result.neighbors, i1, i2);
                addNeighbor(result.neighbors, i2, i1);
                addNeighbor(result.neighbors, i2, i0);
                addNeighbor(result.neighbors, i0, i2);
            }

            for (const auto& edge : edgeUse) {
                if (edge.second == 1) {
                    result.boundary[edge.first.a] = true;
                    result.boundary[edge.first.b] = true;
                }
            }

            for (size_t i = 0; i < vertexCount; ++i) {
                if (result.area[i] <= kEpsilon) continue;

                const float targetAngle = result.boundary[i] ? kPi : 2.0f * kPi;
                result.gaussian[i] = (targetAngle - angleSum[i]) / result.area[i];

                Vec3 laplace = meanNormal[i] / (2.0f * result.area[i]);
                const Vec3 normal = data.vertices[i].normal.normalized();
                const float sign = laplace.dot(normal) <= 0.0f ? 1.0f : -1.0f;
                result.mean[i] = 0.5f * laplace.length() * sign;
            }

            return result;
        }

        bool solve3x3(float a[3][3], float b[3], float x[3]) {
            for (int pivot = 0; pivot < 3; ++pivot) {
                int best = pivot;
                for (int row = pivot + 1; row < 3; ++row) {
                    if (std::abs(a[row][pivot]) > std::abs(a[best][pivot])) {
                        best = row;
                    }
                }

                if (std::abs(a[best][pivot]) <= kEpsilon) return false;

                if (best != pivot) {
                    for (int col = pivot; col < 3; ++col) {
                        std::swap(a[pivot][col], a[best][col]);
                    }
                    std::swap(b[pivot], b[best]);
                }

                const float invPivot = 1.0f / a[pivot][pivot];
                for (int col = pivot; col < 3; ++col) {
                    a[pivot][col] *= invPivot;
                }
                b[pivot] *= invPivot;

                for (int row = 0; row < 3; ++row) {
                    if (row == pivot) continue;
                    const float factor = a[row][pivot];
                    for (int col = pivot; col < 3; ++col) {
                        a[row][col] -= factor * a[pivot][col];
                    }
                    b[row] -= factor * b[pivot];
                }
            }

            x[0] = b[0];
            x[1] = b[1];
            x[2] = b[2];
            return true;
        }

        void tangentFrame(const Vec3& normal, Vec3& tangent, Vec3& bitangent) {
            Vec3 n = normal.normalized();
            if (n.lengthSquared() <= kEpsilon) {
                n = Vec3(0, 0, 1);
            }
            const Vec3 reference = std::abs(n.z) < 0.9f ? Vec3(0, 0, 1) : Vec3(1, 0, 0);
            tangent = reference.cross(n).normalized();
            if (tangent.lengthSquared() <= kEpsilon) {
                tangent = Vec3(1, 0, 0);
            }
            bitangent = n.cross(tangent).normalized();
        }
    }

    // MeshData implementation
    void MeshData::clear() {
        vertices.clear();
        edges.clear();
        faces.clear();
        triangleIndices.clear();
        triangulationDirty = true;
    }

    void MeshData::calculateNormals() {
        // Reset vertex normals
        for (auto& vertex : vertices) {
            vertex.normal = Vec3(0, 0, 0);
        }

        // Calculate face normals and accumulate vertex normals
        for (auto& face : faces) {
            face.normal = calculateFaceNormal(face);
            
            // Add face normal to each vertex normal
            for (int vertexIndex : face.vertices) {
                if (vertexIndex >= 0 && vertexIndex < static_cast<int>(vertices.size())) {
                    vertices[vertexIndex].normal = vertices[vertexIndex].normal + face.normal;
                }
            }
        }

        // Normalize vertex normals
        for (auto& vertex : vertices) {
            float length = std::sqrt(vertex.normal.x * vertex.normal.x + 
                                   vertex.normal.y * vertex.normal.y + 
                                   vertex.normal.z * vertex.normal.z);
            if (length > 0.0001f) {
                vertex.normal = vertex.normal * (1.0f / length);
            } else {
                vertex.normal = Vec3(0, 0, 1); // Default up normal
            }
        }
    }

    Vec3 MeshData::calculateFaceNormal(const MeshFace& face) const {
        if (face.vertices.size() < 3) {
            return Vec3(0, 0, 1); // Default up normal
        }

        // Use first three vertices to calculate normal
        int i0 = face.vertices[0];
        int i1 = face.vertices[1];
        int i2 = face.vertices[2];

        if (i0 < 0 || i0 >= static_cast<int>(vertices.size()) ||
            i1 < 0 || i1 >= static_cast<int>(vertices.size()) ||
            i2 < 0 || i2 >= static_cast<int>(vertices.size())) {
            return Vec3(0, 0, 1);
        }

        Vec3 v0 = vertices[i0].position;
        Vec3 v1 = vertices[i1].position;
        Vec3 v2 = vertices[i2].position;

        Vec3 edge1 = v1 - v0;
        Vec3 edge2 = v2 - v0;
        
        // Cross product
        Vec3 normal = Vec3(
            edge1.y * edge2.z - edge1.z * edge2.y,
            edge1.z * edge2.x - edge1.x * edge2.z,
            edge1.x * edge2.y - edge1.y * edge2.x
        );

        // Normalize
        float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
        if (length > 0.0001f) {
            normal = normal * (1.0f / length);
        } else {
            normal = Vec3(0, 0, 1);
        }

        return normal;
    }

    void MeshData::triangulate() {
        triangleIndices.clear();

        for (const auto& face : faces) {
            if (face.vertices.size() < 3) continue;

            // Simple fan triangulation for n-gons
            // This works well for convex polygons
            for (size_t i = 1; i < face.vertices.size() - 1; i++) {
                triangleIndices.push_back(face.vertices[0]);
                triangleIndices.push_back(face.vertices[i]);
                triangleIndices.push_back(face.vertices[i + 1]);
            }
        }

        triangulationDirty = false;
    }

    void MeshData::updateBounds(Vec3& minBounds, Vec3& maxBounds) const {
        if (vertices.empty()) {
            minBounds = Vec3(-0.5f, -0.5f, -0.5f);
            maxBounds = Vec3(0.5f, 0.5f, 0.5f);
            return;
        }

        minBounds = vertices[0].position;
        maxBounds = vertices[0].position;

        for (const auto& vertex : vertices) {
            minBounds.x = std::min(minBounds.x, vertex.position.x);
            minBounds.y = std::min(minBounds.y, vertex.position.y);
            minBounds.z = std::min(minBounds.z, vertex.position.z);
            
            maxBounds.x = std::max(maxBounds.x, vertex.position.x);
            maxBounds.y = std::max(maxBounds.y, vertex.position.y);
            maxBounds.z = std::max(maxBounds.z, vertex.position.z);
        }
    }

    // MeshObject implementation
    MeshObject::MeshObject(const std::string& name)
        : SceneObject(name)
        , m_meshData(std::make_shared<MeshData>())
        , m_renderMode(MeshRenderMode::Lit)
        , m_frontColor(1.0f, 1.0f, 1.0f)  // Default: white
        , m_backColor(0.0f, 0.0f, 0.0f)   // Default: black
        , m_showVertices(false)
        , m_showEdges(false)
        , m_showFaces(true)
        , m_vertexSize(3.0f)
        , m_edgeWidth(1.0f)
    {
    }

    void MeshObject::setMeshData(std::shared_ptr<MeshData> meshData) {
        m_meshData = meshData;
        if (m_meshData) {
            m_meshData->triangulationDirty = true;
        }
        calculateBounds();
    }

    MeshObject MeshObject::duplicate() const{
        MeshObject copy;

        // Mesh data
        if(m_meshData)
            copy.setMeshData(std::make_shared<MeshData>(*m_meshData));

        // Normal shading colors
        copy.m_frontColor = m_frontColor;
        copy.m_backColor = m_backColor;

        // Overlay controls
        copy.m_showVertices = m_showVertices;
        copy.m_showEdges = m_showEdges;
        copy.m_showFaces = m_showFaces;

        // Rendering properties
        copy.setRenderMode(m_renderMode);
        copy.m_vertexSize = m_vertexSize;
        copy.m_edgeWidth = m_edgeWidth;
        copy.m_vertexSize = m_vertexSize;

        return copy;
    }

    void MeshObject::renderImpl(Renderer& renderer, Camera& camera) {
        if (!m_meshData || m_meshData->vertices.empty()) {
            // Render placeholder when no mesh data
            std::cout << "MeshObject::renderImpl: No mesh data" << std::endl;
            return;
        }

        // Ensure triangulation is up to date
        ensureTriangulation();

        if (renderer.getSceneRenderMode() != SceneRenderMode::Regular) {
            renderSceneModeOverride(renderer, camera);
            return;
        }

        // Render faces if enabled
        if (m_showFaces) {
            renderMesh(renderer, camera);
        }

        // Render overlays
        if (m_showVertices) {
            renderVertexOverlay(renderer);
        }
        if (m_showEdges) {
            renderEdgeOverlay(renderer);
        }
    }

    void MeshObject::calculateBounds() {
        if (!m_meshData) {
            setBounds(Vec3(-0.5f, -0.5f, -0.5f), Vec3(0.5f, 0.5f, 0.5f));
            return;
        }

        Vec3 minBounds, maxBounds;
        m_meshData->updateBounds(minBounds, maxBounds);
        setBounds(minBounds, maxBounds);
    }

    MeshObject::MeshScalarAnalysisResult MeshObject::gaussianCurvature(bool updateMeshColors,
                                                                       std::optional<float> remapMin,
                                                                       std::optional<float> remapMax) {
        MeshScalarAnalysisResult result;
        if (!m_meshData || m_meshData->vertices.empty()) {
            return result;
        }

        m_meshData->calculateNormals();
        CurvatureData curvature = computeCurvatureData(*m_meshData);
        result.vertexValues = std::move(curvature.gaussian);
        minMaxValues(result.vertexValues, result.minValue, result.maxValue);

        if (updateMeshColors) {
            float colorMin = result.minValue;
            float colorMax = result.maxValue;
            colorRange(result.minValue, result.maxValue, remapMin, remapMax, colorMin, colorMax);
            updateVertexColors(*m_meshData, result.vertexValues, colorMin, colorMax);
        }

        return result;
    }

    MeshObject::MeshScalarAnalysisResult MeshObject::meanCurvature(bool updateMeshColors,
                                                                   std::optional<float> remapMin,
                                                                   std::optional<float> remapMax) {
        MeshScalarAnalysisResult result;
        if (!m_meshData || m_meshData->vertices.empty()) {
            return result;
        }

        m_meshData->calculateNormals();
        CurvatureData curvature = computeCurvatureData(*m_meshData);
        result.vertexValues = std::move(curvature.mean);
        minMaxValues(result.vertexValues, result.minValue, result.maxValue);

        if (updateMeshColors) {
            float colorMin = result.minValue;
            float colorMax = result.maxValue;
            colorRange(result.minValue, result.maxValue, remapMin, remapMax, colorMin, colorMax);
            updateVertexColors(*m_meshData, result.vertexValues, colorMin, colorMax);
        }

        return result;
    }

    MeshObject::MeshPrincipalCurvatureResult MeshObject::principleCurvature(bool updateMeshColors,
                                                                           std::optional<float> remapMin,
                                                                           std::optional<float> remapMax) {
        MeshPrincipalCurvatureResult result;
        if (!m_meshData || m_meshData->vertices.empty()) {
            return result;
        }

        m_meshData->calculateNormals();
        const CurvatureData curvature = computeCurvatureData(*m_meshData);
        const size_t vertexCount = m_meshData->vertices.size();

        result.k1.assign(vertexCount, 0.0f);
        result.k2.assign(vertexCount, 0.0f);
        result.principalDirections.assign(vertexCount, Vec3(1, 0, 0));
        result.otherDirections.assign(vertexCount, Vec3(0, 1, 0));

        for (size_t i = 0; i < vertexCount; ++i) {
            const MeshVertex& vertex = m_meshData->vertices[i];
            Vec3 normal = vertex.normal.normalized();
            if (normal.lengthSquared() <= kEpsilon) {
                normal = Vec3(0, 0, 1);
            }

            Vec3 tangent;
            Vec3 bitangent;
            tangentFrame(normal, tangent, bitangent);

            float ata[3][3] = {
                {0.0f, 0.0f, 0.0f},
                {0.0f, 0.0f, 0.0f},
                {0.0f, 0.0f, 0.0f}
            };
            float atb[3] = {0.0f, 0.0f, 0.0f};

            int equationCount = 0;
            for (int neighborIndex : curvature.neighbors[i]) {
                if (neighborIndex < 0 || neighborIndex >= static_cast<int>(vertexCount)) continue;

                Vec3 edge = m_meshData->vertices[neighborIndex].position - vertex.position;
                edge -= normal * edge.dot(normal);
                if (edge.lengthSquared() <= kEpsilon) continue;

                Vec3 normalDelta = m_meshData->vertices[neighborIndex].normal - vertex.normal;
                normalDelta -= normal * normalDelta.dot(normal);

                const float x = edge.dot(tangent);
                const float y = edge.dot(bitangent);
                const float qx = -normalDelta.dot(tangent);
                const float qy = -normalDelta.dot(bitangent);

                const float row0[3] = {x, y, 0.0f};
                const float row1[3] = {0.0f, x, y};

                for (int r = 0; r < 3; ++r) {
                    for (int c = 0; c < 3; ++c) {
                        ata[r][c] += row0[r] * row0[c] + row1[r] * row1[c];
                    }
                    atb[r] += row0[r] * qx + row1[r] * qy;
                }
                equationCount += 2;
            }

            float shape[3] = {0.0f, 0.0f, 0.0f};
            const bool hasShapeOperator = equationCount >= 4 && solve3x3(ata, atb, shape);

            float k1 = 0.0f;
            float k2 = 0.0f;
            Vec3 dir1 = tangent;
            Vec3 dir2 = bitangent;

            if (hasShapeOperator) {
                const float a = shape[0];
                const float b = shape[1];
                const float c = shape[2];
                const float trace = a + c;
                const float delta = std::sqrt(std::max(0.0f, (a - c) * (a - c) + 4.0f * b * b));
                k1 = 0.5f * (trace + delta);
                k2 = 0.5f * (trace - delta);

                Vec3 localDir1;
                if (std::abs(b) > kEpsilon || std::abs(k1 - a) > kEpsilon) {
                    localDir1 = Vec3(b, k1 - a, 0.0f).normalized();
                } else {
                    localDir1 = Vec3(1, 0, 0);
                }

                dir1 = (tangent * localDir1.x + bitangent * localDir1.y).normalized();
                dir2 = normal.cross(dir1).normalized();
                if (dir2.lengthSquared() <= kEpsilon) {
                    dir2 = bitangent;
                }
            } else {
                const float mean = curvature.mean[i];
                const float gaussian = curvature.gaussian[i];
                const float discriminant = std::sqrt(std::max(0.0f, mean * mean - gaussian));
                k1 = mean + discriminant;
                k2 = mean - discriminant;
            }

            if (std::abs(k2) > std::abs(k1)) {
                std::swap(k1, k2);
                std::swap(dir1, dir2);
            }

            result.k1[i] = k1;
            result.k2[i] = k2;
            result.principalDirections[i] = dir1;
            result.otherDirections[i] = dir2;
        }

        minMaxValues(result.k1, result.minK1, result.maxK1);
        minMaxValues(result.k2, result.minK2, result.maxK2);

        if (updateMeshColors) {
            float colorMin = result.minK1;
            float colorMax = result.maxK1;
            colorRange(result.minK1, result.maxK1, remapMin, remapMax, colorMin, colorMax);
            updateVertexColors(*m_meshData, result.k1, colorMin, colorMax);
        }

        return result;
    }

    void MeshObject::renderSceneModeOverride(Renderer& renderer, Camera& camera) {
        const SceneRenderMode mode = renderer.getSceneRenderMode();
        const Color edgeColor(0.0f, 0.0f, 0.0f, 1.0f);
        const Color vertexColor(1.0f, 0.0f, 120.0f / 255.0f, 1.0f);
        const Color faceColor(0.5f, 0.5f, 0.5f, 0.18f);
        const Color normalFrontColor(1.0f, 1.0f, 1.0f, 1.0f);
        const Color normalBackColor(0.18f, 0.18f, 0.18f, 1.0f);

        std::vector<Vec3> vertexPositions;
        vertexPositions.reserve(m_meshData->vertices.size());
        for (const auto& vertex : m_meshData->vertices) {
            vertexPositions.push_back(vertex.position);
        }

        if (mode == SceneRenderMode::MeshNormalShaded) {
            const Color oldFrontColor = m_frontColor;
            const Color oldBackColor = m_backColor;
            m_frontColor = normalFrontColor;
            m_backColor = normalBackColor;
            renderNormalShaded(renderer, camera);
            m_frontColor = oldFrontColor;
            m_backColor = oldBackColor;
            return;
        }

        if (mode == SceneRenderMode::MeshGray && !m_meshData->triangleIndices.empty()) {
            const bool wasLightingEnabled = glIsEnabled(GL_LIGHTING);
            if (wasLightingEnabled) {
                glDisable(GL_LIGHTING);
            }

            renderer.setColor(faceColor);
            glBegin(GL_TRIANGLES);
            for (int index : m_meshData->triangleIndices) {
                if (index >= 0 && index < static_cast<int>(m_meshData->vertices.size())) {
                    const Vec3& p = m_meshData->vertices[index].position;
                    glVertex3f(p.x, p.y, p.z);
                }
            }
            glEnd();

            if (wasLightingEnabled) {
                glEnable(GL_LIGHTING);
            }
        }

        if (!m_meshData->edges.empty()) {
            std::vector<int> edgeIndices;
            std::vector<Color> edgeColors;
            edgeIndices.reserve(m_meshData->edges.size() * 2);
            edgeColors.reserve(m_meshData->edges.size());

            for (const auto& edge : m_meshData->edges) {
                if (edge.vertexA >= 0 && edge.vertexA < static_cast<int>(m_meshData->vertices.size()) &&
                    edge.vertexB >= 0 && edge.vertexB < static_cast<int>(m_meshData->vertices.size())) {
                    edgeIndices.push_back(edge.vertexA);
                    edgeIndices.push_back(edge.vertexB);
                    edgeColors.push_back(edgeColor);
                }
            }

            renderer.setLineWidth(std::max(m_edgeWidth, 1.0f));
            renderer.drawMeshEdges(
                vertexPositions.data(),
                edgeIndices.data(),
                edgeColors.data(),
                static_cast<int>(edgeColors.size())
            );
        }

        if (mode == SceneRenderMode::MeshWireframeWithVertices) {
            renderer.setColor(vertexColor);
            renderer.setPointSize(std::max(m_vertexSize, 5.0f));
            renderer.drawPoints(vertexPositions.data(), static_cast<int>(vertexPositions.size()));
        }
    }

    void MeshObject::renderMesh(Renderer& renderer, Camera& camera) {
        switch (m_renderMode) {
            case MeshRenderMode::Lit:
                renderLit(renderer);
                break;
            case MeshRenderMode::NormalShaded:
                renderNormalShaded(renderer, camera);
                break;
        }
    }

    void MeshObject::renderWireframe(Renderer& renderer) {
        if (!m_meshData->edges.empty() && !m_meshData->vertices.empty()) {
            // Prepare edge data for wireframe rendering using original mesh edges
            std::vector<int> edgeIndices;
            std::vector<Color> edgeColors;

            // Extract vertex positions for rendering
            std::vector<Vec3> vertexPositions;
            for (const auto& vertex : m_meshData->vertices) {
                vertexPositions.push_back(vertex.position);
            }

            // Extract edge indices and colors
            for (const auto& edge : m_meshData->edges) {
                if (edge.vertexA >= 0 && edge.vertexA < static_cast<int>(m_meshData->vertices.size()) &&
                    edge.vertexB >= 0 && edge.vertexB < static_cast<int>(m_meshData->vertices.size())) {
                    edgeIndices.push_back(edge.vertexA);
                    edgeIndices.push_back(edge.vertexB);
                    edgeColors.push_back(edge.color);
                }
            }

            if (!edgeIndices.empty()) {
                renderer.drawMeshEdges(
                    vertexPositions.data(),
                    edgeIndices.data(),
                    edgeColors.data(),
                    static_cast<int>(edgeColors.size())
                );
            }
        }
    }

    void MeshObject::renderLit(Renderer& renderer) {
        if (!m_meshData->triangleIndices.empty()) {
            // Prepare vertex data for lit rendering
            std::vector<Vec3> triangleVertices;
            std::vector<Vec3> triangleNormals;
            std::vector<Color> triangleColors;

            for (int index : m_meshData->triangleIndices) {
                if (index >= 0 && index < static_cast<int>(m_meshData->vertices.size())) {
                    triangleVertices.push_back(m_meshData->vertices[index].position);
                    triangleNormals.push_back(m_meshData->vertices[index].normal);
                    triangleColors.push_back(m_meshData->vertices[index].color);
                }
            }

            if (!triangleVertices.empty()) {
                renderer.drawMesh(
                    triangleVertices.data(),
                    triangleNormals.data(),
                    triangleColors.data(),
                    static_cast<int>(triangleVertices.size()),
                    nullptr, 0,
                    false  // No lighting for lit mode
                );
            }
        }
    }

    void MeshObject::renderNormalShaded(Renderer& renderer, Camera& camera) {
        if (!m_meshData->triangleIndices.empty()) {
            // Get camera view direction (from camera position to origin)
            Vec3 cameraPos = camera.getPosition();
            Vec3 viewDir = (Vec3(0, 0, 0) - cameraPos).normalized(); // Simplified: looking towards origin

            // Prepare vertex data for normal shaded rendering
            std::vector<Vec3> triangleVertices;
            std::vector<Vec3> triangleNormals;
            std::vector<Color> triangleColors;

            for (int index : m_meshData->triangleIndices) {
                if (index >= 0 && index < static_cast<int>(m_meshData->vertices.size())) {
                    const MeshVertex& vertex = m_meshData->vertices[index];

                    // Calculate dot product between vertex normal and view direction
                    float dotProduct = vertex.normal.dot(viewDir);

                    // Blend between front and back colors based on dot product
                    // Positive dot product = facing camera (front), negative = facing away (back)
                    float t = (dotProduct + 1.0f) * 0.5f; // Map [-1,1] to [0,1]
                    Color blendedColor = Color(
                        m_backColor.r + t * (m_frontColor.r - m_backColor.r),
                        m_backColor.g + t * (m_frontColor.g - m_backColor.g),
                        m_backColor.b + t * (m_frontColor.b - m_backColor.b),
                        m_backColor.a + t * (m_frontColor.a - m_backColor.a)
                    );

                    triangleVertices.push_back(vertex.position);
                    triangleNormals.push_back(vertex.normal);
                    triangleColors.push_back(blendedColor);
                }
            }

            if (!triangleVertices.empty()) {
                renderer.drawMesh(
                    triangleVertices.data(),
                    triangleNormals.data(),
                    triangleColors.data(),
                    static_cast<int>(triangleVertices.size()),
                    nullptr, 0,
                    false  // No OpenGL lighting for normal shaded mode
                );
            }
        }
    }

    void MeshObject::renderVertexOverlay(Renderer& renderer) {
        if (m_meshData->vertices.empty()) return;

        renderer.setPointSize(m_vertexSize);

        std::vector<Vec3> vertexPositions;
        for (const auto& vertex : m_meshData->vertices) {
            vertexPositions.push_back(vertex.position);
        }

        if (!vertexPositions.empty()) {
            renderer.drawPoints(vertexPositions.data(), static_cast<int>(vertexPositions.size()));
        }
    }

    void MeshObject::renderEdgeOverlay(Renderer& renderer) {
        if (m_meshData->edges.empty() || m_meshData->vertices.empty()) return;

        renderer.setLineWidth(m_edgeWidth);

        // Prepare edge data for rendering using original mesh edges
        std::vector<int> edgeIndices;
        std::vector<Color> edgeColors;

        // Extract vertex positions for rendering
        std::vector<Vec3> vertexPositions;
        for (const auto& vertex : m_meshData->vertices) {
            vertexPositions.push_back(vertex.position);
        }

        // Extract edge indices and colors
        for (const auto& edge : m_meshData->edges) {
            if (edge.vertexA >= 0 && edge.vertexA < static_cast<int>(m_meshData->vertices.size()) &&
                edge.vertexB >= 0 && edge.vertexB < static_cast<int>(m_meshData->vertices.size())) {
                edgeIndices.push_back(edge.vertexA);
                edgeIndices.push_back(edge.vertexB);
                edgeColors.push_back(edge.color);
            }
        }

        if (!edgeIndices.empty()) {
            renderer.drawMeshEdges(
                vertexPositions.data(),
                edgeIndices.data(),
                edgeColors.data(),
                static_cast<int>(edgeColors.size())
            );
        }
    }





    void MeshObject::ensureTriangulation() {
        if (m_meshData && m_meshData->triangulationDirty) {
            m_meshData->triangulate();
        }
    }

    void MeshObject::printMeshInfo(){
        if (m_meshData && m_meshData->vertices.size() != 0 && m_meshData->edges.size() != 0 && m_meshData->faces.size() != 0){
            std::cout<< 
            " V: " << m_meshData->vertices.size() <<
            " E: " << m_meshData->edges.size() <<
            " F: " << m_meshData->faces.size() << std::endl;
        }
        else{
            std::cout << "Mesh is empty" << std::endl;
        }
    }

    void MeshObject::createCube(float size) {
        if (!m_meshData) {
            m_meshData = std::make_shared<MeshData>();
        }

        m_meshData->clear();
        float half = size * 0.5f;

        // Create 8 vertices
        m_meshData->vertices = {
            MeshVertex(Vec3(-half, -half, -half), Vec3(0, 0, 0), Color(1, 0, 0)), // 0: left-bottom-back
            MeshVertex(Vec3( half, -half, -half), Vec3(0, 0, 0), Color(0, 1, 0)), // 1: right-bottom-back
            MeshVertex(Vec3( half,  half, -half), Vec3(0, 0, 0), Color(0, 0, 1)), // 2: right-top-back
            MeshVertex(Vec3(-half,  half, -half), Vec3(0, 0, 0), Color(1, 1, 0)), // 3: left-top-back
            MeshVertex(Vec3(-half, -half,  half), Vec3(0, 0, 0), Color(1, 0, 1)), // 4: left-bottom-front
            MeshVertex(Vec3( half, -half,  half), Vec3(0, 0, 0), Color(0, 1, 1)), // 5: right-bottom-front
            MeshVertex(Vec3( half,  half,  half), Vec3(0, 0, 0), Color(1, 1, 1)), // 6: right-top-front
            MeshVertex(Vec3(-half,  half,  half), Vec3(0, 0, 0), Color(0.5f, 0.5f, 0.5f)) // 7: left-top-front
        };

        // Create 6 quad faces
        m_meshData->faces = {
            MeshFace({0, 1, 2, 3}, Vec3(0, 0, -1), Color(0.8f, 0.2f, 0.2f)), // Back face
            MeshFace({5, 4, 7, 6}, Vec3(0, 0,  1), Color(0.2f, 0.8f, 0.2f)), // Front face
            MeshFace({4, 0, 3, 7}, Vec3(-1, 0, 0), Color(0.2f, 0.2f, 0.8f)), // Left face
            MeshFace({1, 5, 6, 2}, Vec3( 1, 0, 0), Color(0.8f, 0.8f, 0.2f)), // Right face
            MeshFace({3, 2, 6, 7}, Vec3(0,  1, 0), Color(0.8f, 0.2f, 0.8f)), // Top face
            MeshFace({4, 5, 1, 0}, Vec3(0, -1, 0), Color(0.2f, 0.8f, 0.8f))  // Bottom face
        };

        // Create edges
        m_meshData->edges = {
            // Bottom face edges
            MeshEdge(0, 1, Color(1, 1, 1)), MeshEdge(1, 2, Color(1, 1, 1)), MeshEdge(2, 3, Color(1, 1, 1)), MeshEdge(3, 0, Color(1, 1, 1)),
            // Top face edges
            MeshEdge(4, 5, Color(1, 1, 1)), MeshEdge(5, 6, Color(1, 1, 1)), MeshEdge(6, 7, Color(1, 1, 1)), MeshEdge(7, 4, Color(1, 1, 1)),
            // Vertical edges
            MeshEdge(0, 4, Color(1, 1, 1)), MeshEdge(1, 5, Color(1, 1, 1)), MeshEdge(2, 6, Color(1, 1, 1)), MeshEdge(3, 7, Color(1, 1, 1))
        };

        m_meshData->calculateNormals();
        m_meshData->triangulationDirty = true;
        calculateBounds();
    }

    void MeshObject::createPlane(float width, float height, int subdivisionsX, int subdivisionsY) {
        if (!m_meshData) {
            m_meshData = std::make_shared<MeshData>();
        }

        m_meshData->clear();

        float halfWidth = width * 0.5f;
        float halfHeight = height * 0.5f;
        float stepX = width / subdivisionsX;
        float stepY = height / subdivisionsY;

        // Create vertices
        for (int y = 0; y <= subdivisionsY; y++) {
            for (int x = 0; x <= subdivisionsX; x++) {
                float posX = -halfWidth + x * stepX;
                float posY = -halfHeight + y * stepY;
                float u = static_cast<float>(x) / subdivisionsX;
                float v = static_cast<float>(y) / subdivisionsY;

                Color color(u, v, 0.5f);
                m_meshData->vertices.emplace_back(Vec3(posX, posY, 0), Vec3(0, 0, 1), color);
            }
        }

        // Create quad faces
        for (int y = 0; y < subdivisionsY; y++) {
            for (int x = 0; x < subdivisionsX; x++) {
                int i0 = y * (subdivisionsX + 1) + x;
                int i1 = i0 + 1;
                int i2 = (y + 1) * (subdivisionsX + 1) + x + 1;
                int i3 = (y + 1) * (subdivisionsX + 1) + x;

                m_meshData->faces.emplace_back(std::vector<int>{i0, i1, i2, i3}, Vec3(0, 0, 1), Color(0.7f, 0.7f, 0.7f));
            }
        }

        // Create edges (simplified - just outer boundary for now)
        int vertsPerRow = subdivisionsX + 1;
        for (int x = 0; x < subdivisionsX; x++) {
            // Bottom edge
            m_meshData->edges.emplace_back(x, x + 1, Color(1, 1, 1));
            // Top edge
            int topStart = subdivisionsY * vertsPerRow;
            m_meshData->edges.emplace_back(topStart + x, topStart + x + 1, Color(1, 1, 1));
        }
        for (int y = 0; y < subdivisionsY; y++) {
            // Left edge
            m_meshData->edges.emplace_back(y * vertsPerRow, (y + 1) * vertsPerRow, Color(1, 1, 1));
            // Right edge
            m_meshData->edges.emplace_back(y * vertsPerRow + subdivisionsX, (y + 1) * vertsPerRow + subdivisionsX, Color(1, 1, 1));
        }

        m_meshData->calculateNormals();
        m_meshData->triangulationDirty = true;
        calculateBounds();
    }

    void MeshObject::createSphere(float radius, int segments, int rings) {
        if (!m_meshData) {
            m_meshData = std::make_shared<MeshData>();
        }

        m_meshData->clear();

        // Create vertices
        for (int ring = 0; ring <= rings; ring++) {
            float phi = static_cast<float>(ring) * 3.14159f / rings;
            float y = radius * std::cos(phi);
            float ringRadius = radius * std::sin(phi);

            for (int segment = 0; segment <= segments; segment++) {
                float theta = static_cast<float>(segment) * 2.0f * 3.14159f / segments;
                float x = ringRadius * std::cos(theta);
                float z = ringRadius * std::sin(theta);

                Vec3 position(x, y, z);
                Vec3 normal = position * (1.0f / radius); // Normalized position is the normal for a sphere
                Color color(0.5f + 0.5f * normal.x, 0.5f + 0.5f * normal.y, 0.5f + 0.5f * normal.z);

                m_meshData->vertices.emplace_back(position, normal, color);
            }
        }

        // Create quad faces (except at poles)
        for (int ring = 0; ring < rings; ring++) {
            for (int segment = 0; segment < segments; segment++) {
                int i0 = ring * (segments + 1) + segment;
                int i1 = i0 + 1;
                int i2 = (ring + 1) * (segments + 1) + segment + 1;
                int i3 = (ring + 1) * (segments + 1) + segment;

                m_meshData->faces.emplace_back(std::vector<int>{i0, i1, i2, i3}, Vec3(0, 0, 1), Color(0.8f, 0.8f, 0.8f));
            }
        }

        m_meshData->calculateNormals();
        m_meshData->triangulationDirty = true;
        calculateBounds();
    }

    // Create mesh from custom vertices and faces (for marching cubes and other procedural generation)
    void MeshObject::createFromVerticesAndFaces(const std::vector<Vec3>& positions,
                                               const std::vector<std::vector<int>>& faceIndices,
                                               const std::vector<Vec3>& normals,
                                               const std::vector<Color>& colors) {
        if (!m_meshData) {
            m_meshData = std::make_shared<MeshData>();
        }

        m_meshData->clear();

        // Create vertices
        for (size_t i = 0; i < positions.size(); ++i) {
            MeshVertex vertex;
            vertex.position = positions[i];
            vertex.normal = (i < normals.size()) ? normals[i] : Vec3(0, 0, 1);
            vertex.color = (i < colors.size()) ? colors[i] : Color(0.8f, 0.8f, 0.9f);
            m_meshData->vertices.push_back(vertex);
        }

        // Create faces
        for (const auto& face : faceIndices) {
            if (face.size() >= 3) {
                MeshFace meshFace;
                meshFace.vertices = face;
                meshFace.color = Color(0.8f, 0.8f, 0.9f);

                // Calculate face normal if not provided
                if (face.size() >= 3) {
                    meshFace.normal = m_meshData->calculateFaceNormal(meshFace);
                }

                m_meshData->faces.push_back(meshFace);
            }
        }

        // Generate edges from faces
        generateEdgesFromFaces();

        // Calculate normals if not provided
        if (normals.empty()) {
            m_meshData->calculateNormals();
        }

        m_meshData->triangulationDirty = true;
        calculateBounds();
    }

    // Create mesh from triangles (assumes every 3 vertices form a triangle)
    void MeshObject::createFromTriangles(const std::vector<Vec3>& vertices,
                                        const std::vector<Vec3>& normals,
                                        const std::vector<Color>& colors) {
        if (vertices.size() % 3 != 0) {
            throw std::invalid_argument("Vertex count must be divisible by 3 for triangle mesh");
        }

        if (!m_meshData) {
            m_meshData = std::make_shared<MeshData>();
        }

        m_meshData->clear();

        // Create vertices
        for (size_t i = 0; i < vertices.size(); ++i) {
            MeshVertex vertex;
            vertex.position = vertices[i];
            vertex.normal = (i < normals.size()) ? normals[i] : Vec3(0, 0, 1);
            vertex.color = (i < colors.size()) ? colors[i] : Color(0.8f, 0.8f, 0.9f);
            m_meshData->vertices.push_back(vertex);
        }

        // Create triangular faces
        for (size_t i = 0; i < vertices.size(); i += 3) {
            MeshFace face;
            face.vertices = {static_cast<int>(i), static_cast<int>(i + 1), static_cast<int>(i + 2)};
            face.color = Color(0.8f, 0.8f, 0.9f);

            // Calculate face normal
            face.normal = m_meshData->calculateFaceNormal(face);

            m_meshData->faces.push_back(face);
        }

        // Generate edges from faces
        generateEdgesFromFaces();

        // Calculate normals if not provided
        if (normals.empty()) {
            m_meshData->calculateNormals();
        }

        m_meshData->triangulationDirty = true;
        calculateBounds();
    }

    // Generate edges from faces
    void MeshObject::generateEdgesFromFaces() {
        if (!m_meshData) return;

        m_meshData->edges.clear();
        std::set<std::pair<int, int>> edgeSet;

        // Extract edges from faces
        for (const auto& face : m_meshData->faces) {
            for (size_t i = 0; i < face.vertices.size(); ++i) {
                int v1 = face.vertices[i];
                int v2 = face.vertices[(i + 1) % face.vertices.size()];

                // Ensure consistent edge ordering (smaller index first)
                if (v1 > v2) std::swap(v1, v2);

                edgeSet.insert({v1, v2});
            }
        }

        // Convert set to edge list
        for (const auto& edge : edgeSet) {
            m_meshData->edges.emplace_back(edge.first, edge.second, Color(1, 1, 1));
        }
    }

    // Recalculate normals
    void MeshObject::recalculateNormals() {
        if (!m_meshData) return;
        m_meshData->calculateNormals();
    }

    // Center mesh at origin
    void MeshObject::centerMesh() {
        if (!m_meshData || m_meshData->vertices.empty()) return;

        // Calculate center
        Vec3 center(0, 0, 0);
        for (const auto& vertex : m_meshData->vertices) {
            center += vertex.position;
        }
        center = center / static_cast<float>(m_meshData->vertices.size());

        // Translate all vertices
        for (auto& vertex : m_meshData->vertices) {
            vertex.position -= center;
        }

        calculateBounds();
    }

    // Scale mesh
    void MeshObject::scaleMesh(const Vec3& scale) {
        if (!m_meshData) return;

        for (auto& vertex : m_meshData->vertices) {
            vertex.position.x *= scale.x;
            vertex.position.y *= scale.y;
            vertex.position.z *= scale.z;
        }

        calculateBounds();
    }

    // Translate mesh
    void MeshObject::translateMesh(const Vec3& offset) {
        if (!m_meshData) return;

        for (auto& vertex : m_meshData->vertices) {
            vertex.position += offset;
        }

        calculateBounds();
    }

    void MeshObject::applyTransform() {
        Mat4 matrix = getTransform().getMatrix();
        if (m_meshData) {
            for (auto& vertex : m_meshData->vertices) {
                vertex.position = matrix.transformPoint(vertex.position);
                Vec3 transformedNormal = getTransform().transformDirection(vertex.normal);
                if (transformedNormal.lengthSquared() > 1e-8f) {
                    transformedNormal.normalize();
                }
                vertex.normal = transformedNormal;
            }
        }
        getTransform().setTranslation(Vec3(0, 0, 0));
        getTransform().setRotation(Quaternion());
        getTransform().setScale(Vec3(1, 1, 1));
        calculateBounds();
    }

    void MeshObject::weld(float epsilon)
    {
        struct Key
        {
            int x, y, z;
            bool operator==(Key const &o) const { return x == o.x && y == o.y && z == o.z; }
        };
        struct KeyHash
        {
            size_t operator()(Key const &k) const
            {
                // use a few large primes for a 3D grid hash
                return (size_t)k.x * 73856093u ^ (size_t)k.y * 19349663u ^ (size_t)k.z * 83492791u;
            }
        };

        float invEps = 1.0f / epsilon;
        std::unordered_map<Key, int, KeyHash> map;
        map.reserve(getMeshData()->vertices.size());

        std::vector<MeshVertex> newVerts;
        newVerts.reserve(getMeshData()->vertices.size());
        std::vector<int> remap(getMeshData()->vertices.size());

        // 1) build new vertex list + remapping
        for (size_t i = 0; i < getMeshData()->vertices.size(); ++i)
        {
            auto &v = getMeshData()->vertices[i].position;
            Key k{
                static_cast<int>(std::floor(v.x * invEps + 0.5f)),
                static_cast<int>(std::floor(v.y * invEps + 0.5f)),
                static_cast<int>(std::floor(v.z * invEps + 0.5f))};
            auto it = map.find(k);
            if (it == map.end())
            {
                int newIndex = (int)newVerts.size();
                map[k] = newIndex;
                newVerts.push_back(getMeshData()->vertices[i]);
                remap[i] = newIndex;
            }
            else
            {
                remap[i] = it->second;
            }
        }

        // 2) reindex faces, dropping degenerate ones
        std::vector<MeshFace> newFaces;
        newFaces.reserve(getMeshData()->faces.size());
        for (auto &face : getMeshData()->faces)
        {
            std::vector<int> idx;
            idx.reserve(face.vertices.size());
            for (int vid : face.vertices)
                idx.push_back(remap[vid]);
            // remove consecutive duplicates
            idx.erase(std::unique(idx.begin(), idx.end()), idx.end());
            if (idx.size() >= 3)
            {
                MeshFace f(idx, getMeshData()->calculateFaceNormal({idx, {}, {}}), face.color);
                newFaces.push_back(f);
            }
        }

        // 3) rebuild edges from faces
        std::set<std::pair<int, int>> edgeSet;
        for (auto &f : newFaces)
        {
            for (size_t i = 0; i < f.vertices.size(); ++i)
            {
                int a = f.vertices[i], b = f.vertices[(i + 1) % f.vertices.size()];
                if (a > b)
                    std::swap(a, b);
                edgeSet.insert({a, b});
            }
        }

        // 4) commit
        getMeshData()->vertices.swap(newVerts);
        getMeshData()->faces.swap(newFaces);
        getMeshData()->edges.clear();
        getMeshData()->edges.reserve(edgeSet.size());
        for (auto &e : edgeSet)
            getMeshData()->edges.emplace_back(e.first, e.second, Color(1, 1, 1));

        getMeshData()->calculateNormals();
        getMeshData()->triangulationDirty = true;
    }

    void MeshObject::combineWith(const MeshObject &other)
    {
        // nothing to do if the other mesh is empty
        if (!other.m_meshData || other.m_meshData->vertices.empty())
            return;

        // ensure we have a meshData to append into
        if (!m_meshData)
        {
            m_meshData = std::make_shared<MeshData>();
            m_meshData->clear();
        }

        // reserve enough space to avoid reallocation
        m_meshData->vertices.reserve(m_meshData->vertices.size() + other.m_meshData->vertices.size());
        m_meshData->faces.reserve(m_meshData->faces.size() + other.m_meshData->faces.size());

        // 1) record how many verts we have now
        const size_t vertOffset = m_meshData->vertices.size();

        // 2) copy all vertices from other
        //    (MeshVertex has position, normal, [color])
        m_meshData->vertices.insert(
            m_meshData->vertices.end(),
            other.m_meshData->vertices.begin(),
            other.m_meshData->vertices.end());

        // 3) copy all faces, offsetting their indices
        for (const auto &f : other.m_meshData->faces)
        {
            std::vector<int> newIdx;
            newIdx.reserve(f.vertices.size());
            for (int vi : f.vertices)
            {
                newIdx.push_back(int(vi) + int(vertOffset));
            }
            m_meshData->faces.emplace_back(std::move(newIdx));
        }

        // 4) rebuild derived data (edges, smooth normals, triangulation, bounds)
        generateEdgesFromFaces();
        m_meshData->calculateNormals();
        m_meshData->triangulationDirty = true;
        calculateBounds();
    }

    void MeshObject::readFromObj(const std::string &filename)
    {
        std::ifstream in(filename);
        if (!in)
            throw std::runtime_error("Failed to open OBJ file: " + filename);

        std::vector<Vec3> positions;
        std::vector<Vec3> normals;
        std::vector<std::vector<int>> facePosIdx;
        auto parseObjIndex = [](const std::string &value, int count) -> int
        {
            if (value.empty())
                return -1;
            int index = std::stoi(value);
            if (index < 0)
                index = count + index + 1;
            return index - 1;
        };

        std::string line;
        while (std::getline(in, line))
        {
            if (line.empty() || line[0] == '#')
                continue;
            std::istringstream iss(line);
            std::string tag;
            iss >> tag;

            if (tag == "v")
            {
                // vertex position
                Vec3 p;
                iss >> p.x >> p.y >> p.z;
                positions.push_back(p);
            }
            else if (tag == "vn")
            {
                // vertex normal
                Vec3 n;
                iss >> n.x >> n.y >> n.z;
                normals.push_back(n);
            }
            else if (tag == "f")
            {
                std::vector<int> pidx;
                std::string token;
                while (iss >> token)
                {
                    size_t slash = token.find('/');
                    std::string vertexToken = slash == std::string::npos ? token : token.substr(0, slash);
                    int vi = parseObjIndex(vertexToken, static_cast<int>(positions.size()));
                    if (vi < 0 || vi >= static_cast<int>(positions.size()))
                        throw std::runtime_error("Invalid OBJ face vertex index: " + token);
                    pidx.push_back(vi);
                }
                if (pidx.size() >= 3)
                {
                    facePosIdx.push_back(pidx);
                }
            }
        }

        // build mesh
        m_meshData = std::make_shared<MeshData>();
        m_meshData->clear();

        // create vertices: we’ll stash normals per‐vertex here
        // if a vertex is shared by faces with different normals, you may want to duplicate it
        for (size_t i = 0; i < positions.size(); ++i)
        {
            Vec3 n = (i < normals.size() ? normals[i] : Vec3(0, 0, 1));
            m_meshData->vertices.emplace_back(positions[i], n);
        }

        // faces: ignore normal‐index array (we assume one normal per vertex)
        for (auto &fp : facePosIdx)
        {
            m_meshData->faces.emplace_back(fp);
        }

        // regenerate derived data
        generateEdgesFromFaces();
        m_meshData->calculateNormals();
        m_meshData->triangulationDirty = true;
        calculateBounds();

        printMeshInfo();
    }

    void MeshObject::writeToObj(const std::string &filename)
    {
        if (!m_meshData)
        {
            std::cout << "No mesh data to write" << std::endl;
            return;
        }
        std::filesystem::path outPath(filename);
        if (outPath.has_parent_path())
        {
            std::error_code ec;
            std::filesystem::create_directories(outPath.parent_path(), ec);
            if (ec)
            {
                std::cerr << "Failed to create directory '"
                          << outPath.parent_path().string()
                          << "': " << ec.message() << "\n";
                return;
            }
        }

        std::ofstream out(filename, std::ios::out | std::ios::trunc);
        if (!out)
        {
            std::cout << "Failed to open OBJ file: " + filename << std::endl;
            return;
        }

        applyTransform();

        // positions
        for (auto &v : m_meshData->vertices)
        {
            out << "v "
                << v.position.x << ' '
                << v.position.y << ' '
                << v.position.z << '\n';
        }

        // normals
        for (auto &v : m_meshData->vertices)
        {
            out << "vn "
                << v.normal.x << ' '
                << v.normal.y << ' '
                << v.normal.z << '\n';
        }

        // faces using v//vn
        for (auto &f : m_meshData->faces)
        {
            out << "f";
            for (int vidx : f.vertices)
            {
                int i = vidx + 1; // OBJ is 1‑based
                out << ' ' << i << "//" << i;
            }
            out << '\n';
        }

        std::cout << "Successfully exported mesh to: " + filename << std::endl;
    }
} // namespace alice2

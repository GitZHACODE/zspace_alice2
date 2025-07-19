#include "MeshObject.h"
#include "../core/Renderer.h"
#include "../core/Camera.h"
#include <algorithm>
#include <cmath>

namespace alice2 {

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
    {
    }

    void MeshObject::setMeshData(std::shared_ptr<MeshData> meshData) {
        m_meshData = meshData;
        if (m_meshData) {
            m_meshData->triangulationDirty = true;
        }
        calculateBounds();
    }

    void MeshObject::renderImpl(Renderer& renderer, Camera& camera) {
        if (!m_meshData || m_meshData->vertices.empty()) {
            // Render placeholder when no mesh data
            renderer.setColor(Color(0.5f, 0.5f, 0.5f));
            renderer.drawCube(1.0f);
            return;
        }

        // Ensure triangulation is up to date
        ensureTriangulation();

        // Render main mesh based on render mode
        renderMesh(renderer, camera);
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

    void MeshObject::renderMesh(Renderer& renderer, Camera& camera) {
        switch (m_renderMode) {
            case MeshRenderMode::Wireframe:
                renderWireframe(renderer);
                break;
            case MeshRenderMode::Lit:
                renderLit(renderer);
                break;
        }
    }

    void MeshObject::renderWireframe(Renderer& renderer) {
        if (!m_meshData->triangleIndices.empty()) {
            // Prepare vertex data for wireframe rendering
            std::vector<Vec3> triangleVertices;
            std::vector<Color> triangleColors;

            for (int index : m_meshData->triangleIndices) {
                if (index >= 0 && index < static_cast<int>(m_meshData->vertices.size())) {
                    triangleVertices.push_back(m_meshData->vertices[index].position);
                    triangleColors.push_back(m_meshData->vertices[index].color);
                }
            }

            if (!triangleVertices.empty()) {
                renderer.drawMeshWireframe(
                    triangleVertices.data(),
                    triangleColors.data(),
                    static_cast<int>(triangleVertices.size()),
                    nullptr, 0
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





    void MeshObject::ensureTriangulation() {
        if (m_meshData && m_meshData->triangulationDirty) {
            m_meshData->triangulate();
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

} // namespace alice2

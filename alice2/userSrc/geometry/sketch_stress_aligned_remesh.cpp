// #define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace alice2;

enum class RemeshGuideField {
    Stress,
    Curvature
};

class StressAnalysisSketch : public ISketch {
public:
    std::string getName() const override { return "Stress Analysis"; }
    std::string getDescription() const override { return "Vertical slab stress tensor analysis"; }
    std::string getAuthor() const override { return "alice2 User"; }

    void setup() override {
        scene().setBackgroundColor(Color(1.0f, 1.0f, 1.0f));
        scene().setShowGrid(false);
        scene().setShowAxes(false);
        scene().setAxesLength(1.5f);

        m_mesh = std::make_shared<MeshObject>("stress_input_mesh");
        loadMesh();
        solve();
    }

    void update(float deltaTime) override {
    }

    void draw(Renderer& renderer, Camera& camera) override {
        if (hasMesh()) {
            if (m_guideField == RemeshGuideField::Stress) {
                m_analyzer.draw(renderer, *m_mesh, m_drawSettings);
            } else {
                drawCurvatureGuide(renderer);
            }
            if (m_showSingularities) {
                drawInputSingularities(renderer);
            }
        }
        if (m_showRemesh && m_remeshedMesh && m_remeshedMesh->getMeshData()) {
            m_remeshedMesh->render(renderer, camera);
        }

        renderer.setColor(Color(0.05f, 0.05f, 0.05f, 1.0f));
        renderer.drawString(m_report, 10, 30);
        renderer.drawString("c colour | x cross | s streamlines | q remesh | e export OBJ | r reload | f flip load | o/p cross scale", 10, 50);
        renderer.drawString("j/k field smooth: " + std::to_string(m_smoothingIterations) +
                            " | u/i density | h/g chamfer: " + std::to_string(m_chamferPercentage) +
                            " | n/m cc levels: " + std::to_string(m_smoothLevels) +
                            " | a solver: " + std::string(m_useFieldAlignmentSolver ? "on" : "off"), 10, 70);
        renderer.drawString("v guide field: " + std::string(m_guideField == RemeshGuideField::Stress ? "stress" : "curvature") +
                            " | z singularities: " + std::string(m_showSingularities ? "on" : "off"), 10, 90);
    }

    bool onKeyPress(unsigned char key, int x, int y) override {
        if (key == 'r') {
            loadMesh();
            solve();
            return true;
        }
        if (key == 'f') {
            m_forceSign *= -1.0f;
            solve();
            return true;
        }
        if (key == 'o') {
            m_drawSettings.crossScale *= 1.2f;
            return true;
        }
        if (key == 'p') {
            m_drawSettings.crossScale /= 1.2f;
            return true;
        }
        if (key == 's') {
            m_drawSettings.drawStreamlines = !m_drawSettings.drawStreamlines;
            return true;
        }
        if (key == 'c') {
            m_drawSettings.drawColoredMesh = !m_drawSettings.drawColoredMesh;
            return true;
        }
        if (key == 'x') {
            m_drawSettings.drawCrossField = !m_drawSettings.drawCrossField;
            return true;
        }
        if (key == 'q') {
            m_showRemesh = !m_showRemesh;
            return true;
        }
        if (key == 'e') {
            exportRemesh();
            return true;
        }
        if (key == 'u') {
            m_remeshTargetScale *= 1.15f;
            buildRemesh();
            return true;
        }
        if (key == 'i') {
            m_remeshTargetScale /= 1.15f;
            buildRemesh();
            return true;
        }
        if (key == 'h') {
            m_chamferPercentage = std::min(1.0f, m_chamferPercentage + 0.05f);
            buildRemesh();
            return true;
        }
        if (key == 'g') {
            m_chamferPercentage = std::max(0.0f, m_chamferPercentage - 0.05f);
            buildRemesh();
            return true;
        }
        if (key == 'n') {
            m_smoothLevels = std::max(0, m_smoothLevels - 1);
            buildRemesh();
            return true;
        }
        if (key == 'm') {
            m_smoothLevels += 1;
            buildRemesh();
            return true;
        }
        if (key == 'a') {
            m_useFieldAlignmentSolver = !m_useFieldAlignmentSolver;
            buildRemesh();
            return true;
        }
        if (key == 'v') {
            m_guideField = (m_guideField == RemeshGuideField::Stress) ? RemeshGuideField::Curvature : RemeshGuideField::Stress;
            if (m_guideField == RemeshGuideField::Curvature && m_curvatureField.empty()) buildCurvatureField();
            buildRemesh();
            return true;
        }
        if (key == 'z') {
            m_showSingularities = !m_showSingularities;
            return true;
        }
        if (key == 'j') {
            m_smoothingIterations = std::max(0, m_smoothingIterations - 2);
            solve();
            return true;
        }
        if (key == 'k') {
            m_smoothingIterations += 2;
            solve();
            return true;
        }
        return false;
    }

private:
    bool hasMesh() const {
        return m_mesh && m_mesh->getMeshData() && !m_mesh->getMeshData()->vertices.empty() && !m_mesh->getMeshData()->faces.empty();
    }

    void loadMesh() {
        m_loadVertices.clear();
        m_report.clear();

        m_mesh = std::make_shared<MeshObject>("stress_input_mesh");
        if (m_objPath.empty()) {
            m_report = "Set m_objPath in sketch_stress_aligned_remesh.cpp";
            return;
        }

        m_mesh->readFromObj(m_objPath);
        if (!hasMesh()) {
            m_report = "Could not load mesh: " + m_objPath;
            return;
        }

        m_mesh->generateEdgesFromFaces();
        m_mesh->recalculateNormals();
        m_curvatureField.clear();

        auto data = m_mesh->getMeshData();
        m_loadVertices.reserve(data->vertices.size());
        for (int i = 0; i < static_cast<int>(data->vertices.size()); ++i) {
            m_loadVertices.push_back(i);
        }
    }

    void solve() {
        if (!hasMesh()) return;

        m_analyzer.clearBoundaryConditions();
        m_analyzer.clearForces();
        m_analyzer.setFixedVertices(m_columnVertexIds);
        m_analyzer.setForces(m_loadVertices, Vec3(0.0f, 0.0f, -m_loadMagnitude * m_forceSign));
        m_analyzer.setFieldSmoothingIterations(m_smoothingIterations);
        m_analyzer.setStressMagnitudeThreshold(1e-8);

        bool ok = m_analyzer.solveVerticalSlab(*m_mesh);
        if (ok) {
            m_analyzer.colorMeshByMagnitude(*m_mesh,
                                            m_drawSettings.lowMagnitudeColor,
                                            m_drawSettings.highMagnitudeColor);
            buildCurvatureField();
            buildRemesh();
        } else {
            m_remeshedMesh.reset();
        }

        float maxStress = 0.0f;
        for (float value : m_analyzer.getStressMagnitudes()) maxStress = std::max(maxStress, value);
        m_report = ok ? "vertical slab analysis | vertices: " + std::to_string(m_mesh->getMeshData()->vertices.size()) +
                        " | faces: " + std::to_string(m_mesh->getMeshData()->faces.size()) +
                        " | remesh faces: " + std::to_string(m_remeshedMesh && m_remeshedMesh->getMeshData() ? m_remeshedMesh->getMeshData()->faces.size() : 0) +
                        " | columns: " + std::to_string(m_columnVertexIds.size()) +
                        " | max stress: " + std::to_string(maxStress)
                      : "stress solve failed";
    }

    void buildRemesh() {
        auto data = m_mesh ? m_mesh->getMeshData() : nullptr;
        if (!data) {
            m_remeshedMesh.reset();
            return;
        }

        Vec3 minBounds;
        Vec3 maxBounds;
        data->updateBounds(minBounds, maxBounds);
        float extent = std::max(maxBounds.x - minBounds.x, maxBounds.y - minBounds.y);
        if (extent > 1e-6f) {
            m_remesher.setTargetEdgeLength((extent / 12.0f) * m_remeshTargetScale);
        }
        m_remesher.setSnapVertices(m_columnVertexIds);
        m_remesher.setAngleSimplificationTolerance(m_angleSimplificationTolerance);
        m_remesher.setChamferPercentage(m_chamferPercentage);
        m_remesher.setSmoothLevels(m_smoothLevels);
        m_remesher.setFieldAlignmentIterations(m_useFieldAlignmentSolver ? m_fieldAlignmentIterations : 0);
        m_remesher.setSingularityIndexThreshold(m_singularityIndexThreshold);

        const TensorField& singularityField = m_analyzer.getSmoothedCrossField();
        const TensorField& solverField = activeGuideField();
        auto remeshedData = m_remesher.remesh(*data, singularityField, solverField);
        if (!remeshedData || remeshedData->faces.empty()) {
            m_remeshedMesh.reset();
            return;
        }

        m_remeshedMesh = std::make_shared<MeshObject>("stress_aligned_remesh");
        m_remeshedMesh->setMeshData(remeshedData);
        m_remeshedMesh->setShowFaces(true);
        m_remeshedMesh->setShowEdges(true);
        m_remeshedMesh->setShowVertices(true);
        m_remeshedMesh->setEdgeWidth(2.0f);
    }

    void exportRemesh() {
        if (!m_remeshedMesh || !m_remeshedMesh->getMeshData() || m_remeshedMesh->getMeshData()->faces.empty()) {
            m_report = "No remeshed mesh to export";
            return;
        }

        m_remeshedMesh->writeToObj(m_exportObjPath);
        m_report = "Exported remesh OBJ: " + m_exportObjPath;
    }

    void buildCurvatureField() {
        m_curvatureField.clear();
        if (!hasMesh()) return;

        auto data = m_mesh->getMeshData();
        auto curvature = m_mesh->principleCurvature(false);
        if (curvature.principalDirections.size() != data->vertices.size() ||
            curvature.otherDirections.size() != data->vertices.size()) {
            return;
        }

        m_curvatureField.resize(data->faces.size());
        for (int fi = 0; fi < static_cast<int>(data->faces.size()); ++fi) {
            const MeshFace& face = data->faces[fi];
            FaceStressTensor tensor;
            Vec3 major;
            Vec3 minor;
            bool hasMajor = false;
            bool hasMinor = false;
            float k1 = 0.0f;
            float k2 = 0.0f;
            int count = 0;

            for (int id : face.vertices) {
                if (id < 0 || id >= static_cast<int>(data->vertices.size())) continue;

                Vec3 d1 = curvature.principalDirections[id];
                Vec3 d2 = curvature.otherDirections[id];
                d1.z = 0.0f;
                d2.z = 0.0f;
                if (d1.lengthSquared() > 1e-10f) {
                    d1.normalize();
                    if (hasMajor && major.dot(d1) < 0.0f) d1 = -d1;
                    major += d1;
                    hasMajor = true;
                }
                if (d2.lengthSquared() > 1e-10f) {
                    d2.normalize();
                    if (hasMinor && minor.dot(d2) < 0.0f) d2 = -d2;
                    minor += d2;
                    hasMinor = true;
                }
                k1 += curvature.k1[id];
                k2 += curvature.k2[id];
                ++count;
            }

            if (major.lengthSquared() <= 1e-10f && face.vertices.size() >= 2) {
                major = data->vertices[face.vertices[1]].position - data->vertices[face.vertices[0]].position;
                major.z = 0.0f;
            }
            if (major.lengthSquared() <= 1e-10f) major = Vec3(1, 0, 0);
            major.normalize();

            if (minor.lengthSquared() <= 1e-10f) minor = Vec3(-major.y, major.x, 0.0f);
            minor.z = 0.0f;
            if (minor.lengthSquared() <= 1e-10f) minor = Vec3(0, 1, 0);
            minor.normalize();

            float invCount = count > 0 ? 1.0f / static_cast<float>(count) : 1.0f;
            tensor.majorValue = k1 * invCount;
            tensor.minorValue = k2 * invCount;
            tensor.magnitude = std::abs(tensor.majorValue - tensor.minorValue);
            tensor.majorDirection = major;
            tensor.minorDirection = minor;
            m_curvatureField[fi] = tensor;
        }
    }

    const TensorField& activeGuideField() {
        if (m_guideField == RemeshGuideField::Curvature) {
            if (m_curvatureField.empty()) buildCurvatureField();
            if (!m_curvatureField.empty()) return m_curvatureField;
        }
        return m_analyzer.getSmoothedCrossField();
    }

    Vec3 faceCenter(const MeshData& mesh, const MeshFace& face) const {
        Vec3 center;
        if (face.vertices.empty()) return center;
        int count = 0;
        for (int id : face.vertices) {
            if (id < 0 || id >= static_cast<int>(mesh.vertices.size())) continue;
            center += mesh.vertices[id].position;
            ++count;
        }
        return count > 0 ? center / static_cast<float>(count) : center;
    }

    Color lerpColor(const Color& a, const Color& b, float t) const {
        t = std::clamp(t, 0.0f, 1.0f);
        return Color(a.r + (b.r - a.r) * t,
                     a.g + (b.g - a.g) * t,
                     a.b + (b.b - a.b) * t,
                     a.a + (b.a - a.a) * t);
    }

    void drawGuideColoredMesh(Renderer& renderer, const MeshData& mesh, const TensorField& field) const {
        if (field.empty()) return;

        float minMagnitude = std::numeric_limits<float>::max();
        float maxMagnitude = std::numeric_limits<float>::lowest();
        for (const auto& tensor : field.tensors()) {
            minMagnitude = std::min(minMagnitude, tensor.magnitude);
            maxMagnitude = std::max(maxMagnitude, tensor.magnitude);
        }
        float range = std::max(1e-8f, maxMagnitude - minMagnitude);

        std::vector<Vec3> vertices;
        std::vector<Vec3> normals;
        std::vector<Color> colors;
        for (int fi = 0; fi < static_cast<int>(mesh.faces.size()); ++fi) {
            if (fi >= static_cast<int>(field.size())) break;
            const MeshFace& face = mesh.faces[fi];
            if (face.vertices.size() < 3) continue;

            float t = (field[fi].magnitude - minMagnitude) / range;
            Color color = lerpColor(m_drawSettings.lowMagnitudeColor, m_drawSettings.highMagnitudeColor, t);
            Vec3 normal = mesh.calculateFaceNormal(face);
            for (size_t i = 1; i + 1 < face.vertices.size(); ++i) {
                int tri[3] = {face.vertices[0], face.vertices[i], face.vertices[i + 1]};
                for (int id : tri) {
                    if (id < 0 || id >= static_cast<int>(mesh.vertices.size())) continue;
                    vertices.push_back(mesh.vertices[id].position);
                    normals.push_back(normal);
                    colors.push_back(color);
                }
            }
        }

        if (!vertices.empty()) {
            renderer.drawMesh(vertices.data(), normals.data(), colors.data(), static_cast<int>(vertices.size()), nullptr, 0, false);
        }
    }

    void drawGuideCrossField(Renderer& renderer, const MeshData& mesh, const TensorField& field) const {
        int count = std::min(static_cast<int>(mesh.faces.size()), static_cast<int>(field.size()));
        for (int fi = 0; fi < count; ++fi) {
            const MeshFace& face = mesh.faces[fi];
            if (face.vertices.size() < 3) continue;

            Vec3 center = faceCenter(mesh, face);
            Vec3 major = field[fi].majorDirection;
            Vec3 minor = field[fi].minorDirection;
            major.z = 0.0f;
            minor.z = 0.0f;
            if (major.lengthSquared() <= 1e-10f || minor.lengthSquared() <= 1e-10f) continue;
            major.normalize();
            minor.normalize();

            float scale = m_drawSettings.crossScale;
            renderer.drawLine(center - major * scale, center + major * scale, m_drawSettings.majorColor, 1.4f);
            renderer.drawLine(center - minor * scale, center + minor * scale, m_drawSettings.minorColor, 1.0f);
        }
    }

    void drawCurvatureGuide(Renderer& renderer) {
        if (!hasMesh()) return;
        auto data = m_mesh->getMeshData();
        if (!data) return;
        if (m_curvatureField.empty()) buildCurvatureField();

        if (m_drawSettings.drawColoredMesh) {
            drawGuideColoredMesh(renderer, *data, m_curvatureField);
        }
        if (m_drawSettings.drawMeshEdges) {
            m_analyzer.drawMeshEdges(renderer, *data, m_drawSettings.edgeColor, m_drawSettings.edgeWidth);
        }
        if (m_drawSettings.drawBoundaryConditions) {
            m_analyzer.drawBoundaryConditions(renderer,
                                             *data,
                                             m_drawSettings.loadScale,
                                             m_drawSettings.loadColor,
                                             m_drawSettings.fixedVertexColor,
                                             m_drawSettings.fixedVertexSize);
        }
        if (m_drawSettings.drawCrossField) {
            drawGuideCrossField(renderer, *data, m_curvatureField);
        }
    }

    void drawInputSingularities(Renderer& renderer) const {
        const auto& points = m_remesher.singularityDebugPoints();
        if (points.empty()) return;

        float size = std::max(7.0f, m_drawSettings.fixedVertexSize + 2.0f);
        float markScale = std::max(0.025f, static_cast<float>(m_remesher.targetEdgeLength()) * 0.12f);
        for (const auto& point : points) {
            Color color = point.index >= 0.0f ? Color(1.0f, 0.0f, 0.0f, 1.0f)
                                              : Color(0.0f, 0.15f, 1.0f, 1.0f);
            Vec3 p = point.position;
            p.z += 0.0008f;
            renderer.drawPoint(p, color, size);
            renderer.drawLine(p + Vec3(-markScale, -markScale, 0.0f),
                              p + Vec3(markScale, markScale, 0.0f),
                              color,
                              2.0f);
            renderer.drawLine(p + Vec3(-markScale, markScale, 0.0f),
                              p + Vec3(markScale, -markScale, 0.0f),
                              color,
                              2.0f);
        }
    }

    std::string m_objPath{"slab_long.obj"};
    std::string m_exportObjPath{"stress_aligned_remesh.obj"};
    std::vector<int> m_columnVertexIds{0, 1, 2, 3};
    std::shared_ptr<MeshObject> m_mesh;
    std::shared_ptr<MeshObject> m_remeshedMesh;
    StressAnalyzer m_analyzer;
    StressAlignedRemesher m_remesher;
    StressAnalysisDrawSettings m_drawSettings;
    TensorField m_curvatureField;
    std::vector<int> m_loadVertices;
    std::string m_report;
    float m_loadMagnitude{0.03f};
    float m_forceSign{1.0f};
    float m_remeshTargetScale{1.5f};
    float m_angleSimplificationTolerance{0.38f};
    float m_singularityIndexThreshold{0.12f};
    float m_chamferPercentage{0.20f};
    int m_smoothLevels{1};
    int m_fieldAlignmentIterations{15};
    int m_smoothingIterations{15};
    RemeshGuideField m_guideField{RemeshGuideField::Stress};
    bool m_useFieldAlignmentSolver{true};
    bool m_showRemesh{false};
    bool m_showSingularities{true};
};

ALICE2_REGISTER_SKETCH_AUTO(StressAnalysisSketch)

#endif

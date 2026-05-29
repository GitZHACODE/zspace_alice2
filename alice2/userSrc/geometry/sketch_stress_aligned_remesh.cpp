#define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace alice2;

class StressAnalysisSketch : public ISketch {
public:
    std::string getName() const override { return "Stress Analysis"; }
    std::string getDescription() const override { return "Vertical slab stress tensor analysis"; }
    std::string getAuthor() const override { return "alice2 User"; }

    void setup() override {
        scene().setBackgroundColor(Color(1.0f, 1.0f, 1.0f));
        scene().setShowGrid(false);
        scene().setShowAxes(true);
        scene().setAxesLength(1.5f);

        m_mesh = std::make_shared<MeshObject>("stress_input_mesh");
        loadMesh();
        solve();
    }

    void update(float deltaTime) override {
    }

    void draw(Renderer& renderer, Camera& camera) override {
        if (hasMesh()) {
            m_analyzer.draw(renderer, *m_mesh, m_drawSettings);
        }
        if (m_showRemesh && m_remeshedMesh && m_remeshedMesh->getMeshData()) {
            m_remeshedMesh->render(renderer, camera);
        }

        renderer.setColor(Color(0.05f, 0.05f, 0.05f, 1.0f));
        renderer.drawString(m_report, 10, 30);
        renderer.drawString("c colour | x cross | s streamlines | q remesh | r reload | f flip load | o/p cross scale", 10, 50);
        renderer.drawString("j/k field smooth: " + std::to_string(m_smoothingIterations) +
                            " | u/i density | h/g chamfer: " + std::to_string(m_chamferPercentage) +
                            " | n/m cc levels: " + std::to_string(m_smoothLevels), 10, 70);
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

        auto remeshedData = m_remesher.remesh(*data, m_analyzer.getSmoothedCrossField());
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

    std::string m_objPath{"slab.obj"};
    std::vector<int> m_columnVertexIds{0, 1, 2, 3};
    std::shared_ptr<MeshObject> m_mesh;
    std::shared_ptr<MeshObject> m_remeshedMesh;
    StressAnalyzer m_analyzer;
    StressAlignedRemesher m_remesher;
    StressAnalysisDrawSettings m_drawSettings;
    std::vector<int> m_loadVertices;
    std::string m_report;
    float m_loadMagnitude{0.03f};
    float m_forceSign{1.0f};
    float m_remeshTargetScale{1.5f};
    float m_angleSimplificationTolerance{0.38f};
    float m_chamferPercentage{0.20f};
    int m_smoothLevels{1};
    int m_smoothingIterations{15};
    bool m_showRemesh{false};
};

ALICE2_REGISTER_SKETCH_AUTO(StressAnalysisSketch)

#endif

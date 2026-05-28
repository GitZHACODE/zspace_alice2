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

        renderer.setColor(Color(0.05f, 0.05f, 0.05f, 1.0f));
        renderer.drawString(m_report, 10, 30);
        renderer.drawString("c colour | x cross | s streamlines | r reload | f flip load | o/p scale", 10, 50);
        renderer.drawString("j/k smooth iterations: " + std::to_string(m_smoothingIterations), 10, 70);
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
        }

        float maxStress = 0.0f;
        for (float value : m_analyzer.getStressMagnitudes()) maxStress = std::max(maxStress, value);
        m_report = ok ? "vertical slab analysis | vertices: " + std::to_string(m_mesh->getMeshData()->vertices.size()) +
                        " | faces: " + std::to_string(m_mesh->getMeshData()->faces.size()) +
                        " | columns: " + std::to_string(m_columnVertexIds.size()) +
                        " | max stress: " + std::to_string(maxStress)
                      : "stress solve failed";
    }

    std::string m_objPath{"slab.obj"};
    std::vector<int> m_columnVertexIds{0, 1, 2, 3};
    std::shared_ptr<MeshObject> m_mesh;
    StressAnalyzer m_analyzer;
    StressAnalysisDrawSettings m_drawSettings;
    std::vector<int> m_loadVertices;
    std::string m_report;
    float m_loadMagnitude{0.03f};
    float m_forceSign{1.0f};
    int m_smoothingIterations{15};
};

ALICE2_REGISTER_SKETCH_AUTO(StressAnalysisSketch)

#endif

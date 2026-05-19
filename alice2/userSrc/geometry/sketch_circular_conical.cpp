#define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>

using namespace alice2;

class CircularConicalSketch : public ISketch {
public:
    std::string getName() const override { return "Circular Conical"; }
    std::string getDescription() const override { return "Circular and conical projection solver"; }
    std::string getAuthor() const override { return "alice2 User"; }

    void setup() override {
        scene().setBackgroundColor(Color(1.0f, 1.0f, 1.0f));
        scene().setShowGrid(false);
        scene().setShowAxes(true);
        scene().setAxesLength(2.0f);

        m_mesh = std::make_shared<MeshObject>("circular_conical_mesh");

        if (!m_objPath.empty()) {
            m_mesh->readFromObj(m_objPath);
            m_mesh->setShowFaces(false);
            m_originalMesh = std::make_shared<MeshObject>(m_mesh->duplicate());
        }

        m_solver.settings.maxIterations = 200;
        m_solver.settings.strength = 1.0f;
        m_solver.settings.tolerance = 1e-5f;
        m_solver.settings.shapePreservationWeight = 1e-5f;
        m_solver.settings.fixBoundaryVertices = m_fixBoundary;

        m_analyzer.tolerance = m_solver.settings.tolerance;
        m_analyzer.drawSettings.edgeColor = Color(0.02f, 0.02f, 0.02f, 1.0f);
        m_analyzer.drawSettings.edgeWidth = 2.0f;
        m_analyzer.drawSettings.drawConstraintGuides = true;
        m_analyzer.drawSettings.circleColor = Color(0.0f, 0.2f, 1.0f, 1.0f);
        m_analyzer.drawSettings.tangentColor = Color(1.0f, 0.55f, 0.0f, 1.0f);
        m_analyzer.drawSettings.guideLineWidth = 2.0f;
        m_analyzer.drawSettings.tangentScale = 0.18f;
        m_analyzer.drawSettings.circleSegments = 64;
        m_analyzer.drawSettings.drawCones = true;
        m_analyzer.drawSettings.coneColor = Color(0.0f, 0.35f, 1.0f, 1.0f);
        m_analyzer.drawSettings.coneScale = 0.8f;
        m_analyzer.drawSettings.coneSegments = 32;

        setMode(ProjectionAnalysisMode::Circular);
    }

    void update(float deltaTime) override {
        if (!m_running || !hasMesh()) return;

        m_stepTimer += deltaTime;
        float interval = 1.0f / std::max(0.001f, m_stepsPerSecond);

        while (m_stepTimer >= interval) {
            m_stepTimer -= interval;
            if (!runStep()) {
                m_running = false;
                break;
            }
        }
    }

    void draw(Renderer& renderer, Camera& camera) override {
        renderer.setColor(Color(0.5f, 0.5f, 0.5f));

        if (!hasMesh()) {
            renderer.drawString("Set m_objPath in sketch_circular_conical.cpp", 10, 30);
            return;
        }

        m_analyzer.draw(renderer);
        drawOriginalWireframe(renderer);
        renderer.drawString(m_report, 10, 30);
        renderer.drawString("c circular | v conical | u step | p run/pause | o solve", 10, 50);
    }

    bool onKeyPress(unsigned char key, int x, int y) override {
        if (!hasMesh()) return false;

        if (key == 'c') {
            setMode(ProjectionAnalysisMode::Circular);
            return true;
        }

        if (key == 'v') {
            setMode(ProjectionAnalysisMode::Conical);
            return true;
        }

        if (key == 'u') {
            runStep();
            return true;
        }

        if (key == 'p') {
            m_running = !m_running;
            m_stepTimer = 0.0f;
            return true;
        }

        if (key == 'o') {
            m_iteration += m_solver.solve(*m_mesh);
            m_running = false;
            analyze();
            return true;
        }

        return false;
    }

private:
    bool hasMesh() const {
        return m_mesh && m_mesh->getMeshData() && !m_mesh->getMeshData()->vertices.empty();
    }

    void setMode(ProjectionAnalysisMode mode) {
        m_mode = mode;
        m_running = false;
        m_solver.clearConstraints();

        if (m_mode == ProjectionAnalysisMode::Circular) {
            m_solver.addConstraint<CircularFaceConstraint>();
        } else {
            auto planar = m_solver.addConstraint<PlanarFaceConstraint>();
            planar->weight = 0.5f;
            m_solver.addConstraint<ConicalVertexConstraint>();
        }

        analyze();
    }

    void analyze() {
        if (!hasMesh()) return;
        m_solver.settings.fixBoundaryVertices = m_fixBoundary;
        m_analyzer.mode = m_mode;
        m_analyzer.iteration = m_iteration;
        m_analyzer.tolerance = m_solver.settings.tolerance;
        m_analyzer.analyze(*m_mesh);
        m_report = m_analyzer.print();
        std::cout << m_report << std::endl;
    }

    bool runStep() {
        if (!hasMesh()) return false;
        bool keepGoing = m_solver.step(*m_mesh);
        if (keepGoing) ++m_iteration;
        analyze();
        return keepGoing;
    }

    void drawOriginalWireframe(Renderer& renderer) const {
        if (!m_drawOriginalWireframe || !m_originalMesh) return;

        auto data = m_originalMesh->getMeshData();
        if (!data || data->vertices.empty() || data->edges.empty()) return;

        std::vector<Vec3> vertices;
        std::vector<int> edgeIndices;
        std::vector<Color> edgeColors;

        vertices.reserve(data->vertices.size());
        edgeIndices.reserve(data->edges.size() * 2);
        edgeColors.reserve(data->edges.size());

        for (const auto& vertex : data->vertices) vertices.push_back(vertex.position);

        for (const auto& edge : data->edges) {
            if (edge.vertexA < 0 || edge.vertexA >= static_cast<int>(data->vertices.size())) continue;
            if (edge.vertexB < 0 || edge.vertexB >= static_cast<int>(data->vertices.size())) continue;
            edgeIndices.push_back(edge.vertexA);
            edgeIndices.push_back(edge.vertexB);
            edgeColors.push_back(m_originalWireColor);
        }

        renderer.setLineWidth(m_originalWireWidth);
        renderer.drawMeshEdges(vertices.data(), edgeIndices.data(), edgeColors.data(), static_cast<int>(edgeColors.size()));
    }

    std::string m_objPath = "tunnel.obj";
    std::shared_ptr<MeshObject> m_mesh;
    std::shared_ptr<MeshObject> m_originalMesh;
    ProjectionSolver m_solver;
    ProjectionConstraintAnalyzer m_analyzer;
    ProjectionAnalysisMode m_mode{ProjectionAnalysisMode::Circular};
    std::string m_report;
    int m_iteration{0};
    bool m_running{false};
    float m_stepsPerSecond{50.0f};
    float m_stepTimer{0.0f};
    bool m_fixBoundary{false};
    bool m_drawOriginalWireframe{true};
    Color m_originalWireColor{0.75f, 0.75f, 0.75f, 1.0f};
    float m_originalWireWidth{1.0f};
};

ALICE2_REGISTER_SKETCH_AUTO(CircularConicalSketch)

#endif

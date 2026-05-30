// #define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>
#include <cmath>

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

        m_solver.settings.maxIterations = 500;
        m_solver.settings.strength = 1.0f;
        m_solver.settings.tolerance = 1e-5f;
        m_solver.settings.shapePreservationWeight = 1e-5f;
        updateFixedVertexSettings();

        m_analyzer.planarVolumeTolerance = m_solver.settings.tolerance;
        m_analyzer.planarPlaneTolerance = 1e-3f;
        m_analyzer.circularTolerance = m_solver.settings.tolerance;
        m_analyzer.conicalTolerance = m_solver.settings.tolerance;
        m_analyzer.drawSettings.edgeColor = Color(0.02f, 0.02f, 0.02f, 1.0f);
        m_analyzer.drawSettings.edgeWidth = 1.0f;
        m_analyzer.drawSettings.drawConstraintGuides = true;
        m_analyzer.drawSettings.circleColor = Color(0.0f, 0.2f, 1.0f, 1.0f);
        m_analyzer.drawSettings.tangentColor = Color(1.0f, 0.55f, 0.0f, 1.0f);
        m_analyzer.drawSettings.guideLineWidth = 2.0f;
        m_analyzer.drawSettings.tangentScale = 0.18f;
        m_analyzer.drawSettings.circleSegments = 64;
        m_analyzer.drawSettings.drawCones = true;
        m_analyzer.drawSettings.drawConeAxes = true;
        m_analyzer.drawSettings.coneColor = Color(0.0f, 0.35f, 1.0f, 1.0f);
        m_analyzer.drawSettings.coneAxisColor = Color(0.9f, 0.0f, 1.0f, 1.0f);
        m_analyzer.drawSettings.coneScale = 0.8f;
        m_analyzer.drawSettings.coneAxisLength = 0.2f;
        m_analyzer.drawSettings.coneSegments = 32;

        setMode(MeshAnalysisMode::Circular);
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

        if (m_curvatureView == CurvatureView::Projection) {
            m_analyzer.draw(renderer);
        } else {
            drawCurvatureVisualization(renderer);
        }

        if(m_analyzer.drawSettings.drawConstraintGuides) drawOriginalWireframe(renderer);
        renderer.drawString(m_report, 10, 30);
        renderer.drawString("x circular | v conical | c curvature | e extrude | u step | p run/pause | o solve", 10, 50);
    }

    bool onKeyPress(unsigned char key, int x, int y) override {
        if (!hasMesh()) return false;

        if (key == 'x') {
            setMode(MeshAnalysisMode::Circular);
            return true;
        }

        if (key == 'c') {
            cycleCurvatureView();
            return true;
        }

        if (key == 'v') {
            setMode(MeshAnalysisMode::Conical);
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

        if (key == 'd') {
            m_analyzer.drawSettings.drawConstraintGuides = !m_analyzer.drawSettings.drawConstraintGuides;
            return true;
        }

        if (key == 'e') {
            extrudeMesh();
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
    enum class CurvatureView {
        Projection,
        Gaussian,
        Mean,
        Principal
    };

    bool hasMesh() const {
        return m_mesh && m_mesh->getMeshData() && !m_mesh->getMeshData()->vertices.empty();
    }

    void setMode(MeshAnalysisMode mode) {
        m_mode = mode;
        m_running = false;
        m_solver.clearConstraints();

        if (m_mode == MeshAnalysisMode::Circular) {
            m_solver.addConstraint<CircularFaceConstraint>();
        } else if (m_mode == MeshAnalysisMode::Conical) {
            auto planar = m_solver.addConstraint<PlanarFaceConstraint>();
            planar->weight = 0.5f;
            m_solver.addConstraint<ConicalVertexConstraint>();
        } else {
            auto planar = m_solver.addConstraint<PlanarFaceConstraint>();
            planar->weight = 0.5f;
        }

        analyze();
    }

    void analyze() {
        if (!hasMesh()) return;
        updateFixedVertexSettings();
        m_analyzer.mode = m_mode;
        m_analyzer.iteration = m_iteration;
        m_analyzer.planarVolumeTolerance = m_solver.settings.tolerance;
        m_analyzer.circularTolerance = m_solver.settings.tolerance;
        m_analyzer.conicalTolerance = m_solver.settings.tolerance;
        m_analyzer.analyze(*m_mesh);
        m_report = m_analyzer.print();
        if (m_curvatureView != CurvatureView::Projection) {
            updateCurvatureAnalysis();
        }
        std::cout << m_report << std::endl;
    }

    void extrudeMesh() {
        if (!hasMesh()) return;
        constexpr float extrudeDistance = 0.05f;
        std::vector<Vec3> offsets;
        const auto& directions = m_solver.resultVertexNormals();
        offsets.reserve(directions.size());
        for (const Vec3& direction : directions) {
            offsets.push_back(direction.normalized() * extrudeDistance);
        }

        MeshObject extruded = m_mesh->extrudeMesh(extrudeDistance, MeshExtrudeMode::Stereotomy, offsets);
        m_mesh = std::make_shared<MeshObject>(std::move(extruded));
        m_originalMesh = std::make_shared<MeshObject>(m_mesh->duplicate());
        m_running = false;
        m_iteration = 0;
        setMode(MeshAnalysisMode::PlanarPlane);
        m_analyzer.drawSettings.drawFixedVertices = false;
        m_analyzer.fixedVertices.clear();
    }

    void updateFixedVertexSettings() {
        const bool fixZZero = m_fixBoundary && m_fixZZeroVertices;
        const bool fixBoundary = m_fixBoundary && !m_fixZZeroVertices;

        m_solver.settings.fixBoundaryVertices = fixBoundary;
        m_solver.settings.fixedVertices = fixZZero ? fixedVertexIndices_zZero() : std::vector<int>{};
        m_analyzer.drawSettings.drawFixedVertices = m_fixBoundary;

        if (!hasMesh() || !m_fixBoundary) {
            m_analyzer.fixedVertices.clear();
            return;
        }

        m_analyzer.fixedVertices = fixZZero
            ? m_solver.fixedVertexIndices(*m_mesh, m_solver.settings.fixedVertices)
            : m_solver.fixedVertexIndices_allBoundary(*m_mesh);
    }

    std::vector<int> fixedVertexIndices_zZero() const {
        std::vector<int> indices;
        if (!hasMesh()) return indices;

        auto data = m_mesh->getMeshData();
        indices.reserve(data->vertices.size());
        for (size_t i = 0; i < data->vertices.size(); ++i) {
            if (std::abs(data->vertices[i].position.z) <= m_zZeroTolerance) {
                indices.push_back(static_cast<int>(i));
            }
        }

        return indices;
    }

    bool runStep() {
        if (!hasMesh()) return false;
        bool keepGoing = m_solver.step(*m_mesh);
        if (keepGoing) ++m_iteration;
        analyze();
        return keepGoing;
    }

    void cycleCurvatureView() {
        switch (m_curvatureView) {
            case CurvatureView::Projection:
                m_curvatureView = CurvatureView::Gaussian;
                break;
            case CurvatureView::Gaussian:
                m_curvatureView = CurvatureView::Mean;
                break;
            case CurvatureView::Mean:
                m_curvatureView = CurvatureView::Principal;
                break;
            case CurvatureView::Principal:
                m_curvatureView = CurvatureView::Projection;
                break;
        }

        if (m_curvatureView == CurvatureView::Projection) {
            analyze();
        } else {
            updateCurvatureAnalysis();
        }
    }

    void updateCurvatureAnalysis() {
        if (!hasMesh()) return;

        if (m_curvatureView == CurvatureView::Gaussian) {
            auto result = m_mesh->gaussianCurvature(true);
            m_report = "Gaussian curvature | min: " + std::to_string(result.minValue) +
                       " max: " + std::to_string(result.maxValue);
            std::cout << m_report << std::endl;
            return;
        }

        if (m_curvatureView == CurvatureView::Mean) {
            auto result = m_mesh->meanCurvature(true);
            m_report = "Mean curvature | min: " + std::to_string(result.minValue) +
                       " max: " + std::to_string(result.maxValue);
            std::cout << m_report << std::endl;
            return;
        }

        if (m_curvatureView == CurvatureView::Principal) {
            m_principalCurvature = m_mesh->principleCurvature(true);
            m_report = "Principal curvature | k1 min: " + std::to_string(m_principalCurvature.minK1) +
                       " max: " + std::to_string(m_principalCurvature.maxK1) +
                       " | k2 min: " + std::to_string(m_principalCurvature.minK2) +
                       " max: " + std::to_string(m_principalCurvature.maxK2);
            std::cout << m_report << std::endl;
        }
    }

    void drawCurvatureVisualization(Renderer& renderer) const {
        auto data = m_mesh->getMeshData();
        if (!data || data->vertices.empty()) return;

        std::vector<Vec3> triangleVertices;
        std::vector<Vec3> triangleNormals;
        std::vector<Color> triangleColors;

        for (const auto& face : data->faces) {
            if (face.vertices.size() < 3) continue;
            for (size_t i = 1; i + 1 < face.vertices.size(); ++i) {
                int ids[3] = {face.vertices[0], face.vertices[i], face.vertices[i + 1]};
                bool validTriangle = true;
                for (int id : ids) {
                    if (id < 0 || id >= static_cast<int>(data->vertices.size())) {
                        validTriangle = false;
                        break;
                    }
                }
                if (!validTriangle) continue;

                for (int id : ids) {
                    const MeshVertex& vertex = data->vertices[id];
                    triangleVertices.push_back(vertex.position);
                    triangleNormals.push_back(vertex.normal);
                    triangleColors.push_back(vertex.color);
                }
            }
        }

        if (!triangleVertices.empty()) {
            renderer.drawMesh(
                triangleVertices.data(),
                triangleNormals.data(),
                triangleColors.data(),
                static_cast<int>(triangleVertices.size()),
                nullptr,
                0,
                false
            );
        }

        drawMeshEdges(renderer, *data, Color(0.02f, 0.02f, 0.02f, 1.0f), 1.0f);

        if (m_curvatureView == CurvatureView::Principal) {
            drawPrincipalDirections(renderer, *data);
        }
    }

    void drawMeshEdges(Renderer& renderer, const MeshData& data, const Color& color, float width) const {
        if (data.edges.empty()) return;

        std::vector<Vec3> vertices;
        std::vector<int> edgeIndices;
        std::vector<Color> edgeColors;

        vertices.reserve(data.vertices.size());
        edgeIndices.reserve(data.edges.size() * 2);
        edgeColors.reserve(data.edges.size());

        for (const auto& vertex : data.vertices) vertices.push_back(vertex.position);

        for (const auto& edge : data.edges) {
            if (edge.vertexA < 0 || edge.vertexA >= static_cast<int>(data.vertices.size())) continue;
            if (edge.vertexB < 0 || edge.vertexB >= static_cast<int>(data.vertices.size())) continue;
            edgeIndices.push_back(edge.vertexA);
            edgeIndices.push_back(edge.vertexB);
            edgeColors.push_back(color);
        }

        renderer.setLineWidth(width);
        renderer.drawMeshEdges(vertices.data(), edgeIndices.data(), edgeColors.data(), static_cast<int>(edgeColors.size()));
    }

    void drawPrincipalDirections(Renderer& renderer, const MeshData& data) const {
        const size_t count = std::min(data.vertices.size(), m_principalCurvature.principalDirections.size());
        for (size_t i = 0; i < count; ++i) {
            const Vec3& p = data.vertices[i].position;
            const Vec3 d1 = m_principalCurvature.principalDirections[i] * m_principalScale;
            const Vec3 d2 = m_principalCurvature.otherDirections[i] * m_principalScale;
            renderer.drawLine(p - d1, p + d1, Color(1.0f, 0.05f, 0.15f, 1.0f), 1.5f);
            renderer.drawLine(p - d2, p + d2, Color(0.0f, 0.25f, 1.0f, 1.0f), 1.0f);
        }
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

    std::string m_objPath = "skeleton.obj";
    std::shared_ptr<MeshObject> m_mesh;
    std::shared_ptr<MeshObject> m_originalMesh;
    ProjectionSolver m_solver;
    MeshAnalyzer m_analyzer;
    MeshAnalysisMode m_mode{MeshAnalysisMode::Circular};
    CurvatureView m_curvatureView{CurvatureView::Projection};
    MeshObject::MeshPrincipalCurvatureResult m_principalCurvature;
    std::string m_report;
    int m_iteration{0};
    bool m_running{false};
    float m_stepsPerSecond{100.0f};
    float m_stepTimer{0.0f};
    bool m_fixBoundary{true};
    // When fixing is enabled, true fixes vertices on z = 0 instead of all boundary vertices.
    bool m_fixZZeroVertices{true};
    float m_zZeroTolerance{7e-2f};
    bool m_drawOriginalWireframe{true};
    Color m_originalWireColor{0.75f, 0.75f, 0.75f, 1.0f};
    float m_originalWireWidth{1.0f};
    float m_principalScale{0.08f};
};

ALICE2_REGISTER_SKETCH_AUTO(CircularConicalSketch)

#endif

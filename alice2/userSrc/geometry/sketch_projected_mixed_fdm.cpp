// #define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>
#include <fdm/ProjectedMixedFDM.h>
#include <tna/OffsetCableNetwork.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <sstream>

using namespace alice2;

class ProjectedMixedFDMSketch : public ISketch {
public:
    std::string getName() const override { return "Projected Mixed FDM"; }
    std::string getDescription() const override { return "Projected signed FDM on the welded tna.obj offset cable network"; }
    std::string getAuthor() const override { return "alice2 User"; }

    void setup() override {
        scene().setBackgroundColor(Color(.96f, .96f, .96f, 1));
        scene().setShowGrid(false);
        m_ui = std::make_unique<SimpleUI>(input());
        m_ui->setTheme(SimpleUI::UITheme::Dark);
        m_ui->addSlider("Projection", Vec2{10, 160}, 220, .01f, 1.f, m_projection);
        m_ui->addSlider("Density trust", Vec2{10, 190}, 220, .01f, 1.f, m_trust);
        m_ui->addSlider("Support balance", Vec2{10, 220}, 220, 0.f, 10.f, m_supportWeight);
        m_ui->addSlider("Cable offset", Vec2{10, 250}, 220, -2.f, -.02f, m_offset);
        m_ui->addSlider("Surface nodal load", Vec2{10, 280}, 220, 0.f, .05f, m_nodalLoad);
        m_ui->addSlider("Surface weight density", Vec2{10, 310}, 220, 0.f, 10.f, m_density);
        reload();
    }
    void update(float) override {
        if (m_offset != m_lastOffset || m_nodalLoad != m_lastNodalLoad ||
            m_density != m_lastDensity) { reload(); return; }
        if (m_projection != m_lastProjection || m_trust != m_lastTrust ||
            m_supportWeight != m_lastSupportWeight) solve();
    }
    void draw(Renderer& renderer, Camera&) override {
        for (int k = 0; k < int(m_graph.edges.size()); ++k) {
            const auto& e = m_graph.edges[k];
            const bool connector = m_network.memberKinds[k] == "connector";
            const bool cable = e.type == fdm::MemberType::Tension;
            Color color = connector ? Color(.95f,.55f,.08f,1)
                : cable ? Color(.90f,.12f,.45f,1) : Color(.13f,.34f,.82f,1);
            renderer.drawLine(position(e.i), position(e.j), color, cable ? 2.5f : 1.5f);
            if (m_showInput) renderer.drawLine(inputPosition(e.i), inputPosition(e.j), Color(.65f,.65f,.65f,1), 1.f);
        }
        for (int i = 0; i < int(m_graph.nodes.size()); ++i) {
            const bool fixed = m_graph.nodes[i].fixed[0] && m_graph.nodes[i].fixed[1] && m_graph.nodes[i].fixed[2];
            if (fixed || i >= m_network.surfaceVertexCount)
                renderer.drawPoint(position(i), fixed ? Color(.12f,.12f,.12f,1) : Color(.15f,.7f,.25f,1), fixed ? 7.f : 3.f);
            if (m_showForces && m_result.q.size() == int(m_graph.edges.size()) && !fixed) {
                Eigen::Vector3d r = m_result.residuals.row(i).transpose();
                if (r.norm() > 1e-9) renderer.drawLine(position(i), position(i) + Vec3(float(r.x()),float(r.y()),float(r.z())), Color(1,0,0,1), 2.f);
            }
        }
        if (m_showForces && m_result.q.size() == int(m_graph.edges.size())) {
            for (int k = 0; k < int(m_graph.edges.size()); ++k) {
                const auto& e = m_graph.edges[k];
                std::ostringstream label; label << std::fixed << std::setprecision(2) << m_result.q[k];
                renderer.setColor(Color(.08f,.08f,.08f,1));
                renderer.drawText(label.str(), (position(e.i) + position(e.j)) * .5f, 1.f);
            }
            drawSupportForces(renderer);
        }
        renderer.setColor(Color(.08f,.08f,.08f,1));
        renderer.drawString("Projected mixed FDM | blue compression, pink cable, orange connector | i input | d forces", 10, 30);
        renderer.drawString(m_status, 10, 55);
        renderer.drawString("tna.obj | positive compression, negative tension | surface gravity loads", 10, 80);
        renderer.drawString(m_detail, 10, 105);
        if (m_showForces) renderer.drawString("Green arrows: structure on supports | red: free-node residuals", 10, 130);
        if (m_ui) m_ui->draw(renderer);
    }
    bool onKeyPress(unsigned char key, int, int) override {
        if (key == 'i' || key == 'I') { m_showInput = !m_showInput; return true; }
        if (key == 'd' || key == 'D') { m_showForces = !m_showForces; return true; }
        return false;
    }
    bool onMousePress(int b, int s, int x, int y) override { return m_ui && m_ui->onMousePress(b,s,x,y); }
    bool onMouseMove(int x, int y) override { return m_ui && m_ui->onMouseMove(x,y); }

private:
    void drawSupportForces(Renderer& renderer) const {
        if (m_supportForces.rows() != int(m_graph.nodes.size())) return;
        double largest = 0;
        for (int i = 0; i < m_supportForces.rows(); ++i)
            largest = std::max(largest, m_supportForces.row(i).norm());
        if (largest < 1e-12) return;
        const float scale = float(.75 / largest);
        const Color green(.08f,.65f,.18f,1);
        for (int i = 0; i < m_supportForces.rows(); ++i) {
            const double magnitude = m_supportForces.row(i).norm();
            if (magnitude < 1e-10) continue;
            const Vec3 start = position(i);
            const Vec3 force(float(m_supportForces(i,0)), float(m_supportForces(i,1)), float(m_supportForces(i,2)));
            const Vec3 end = start + force * scale;
            const Vec3 direction = force / float(magnitude);
            const Vec3 up = std::abs(direction.z) > .9f ? Vec3(1,0,0) : Vec3(0,0,1);
            const Vec3 side = direction.cross(up).normalized();
            const float head = std::min(.16f, float(magnitude) * scale * .3f);
            renderer.drawLine(start, end, green, 2.5f);
            renderer.drawLine(end, end - direction * head + side * (head * .45f), green, 2.5f);
            renderer.drawLine(end, end - direction * head - side * (head * .45f), green, 2.5f);
            std::ostringstream label;
            label << std::fixed << std::setprecision(2) << magnitude;
            renderer.setColor(green);
            renderer.drawText(label.str(), end + Vec3(0,0,.03f), 1.f);
        }
    }
    static std::filesystem::path dataPath() {
        auto path = std::filesystem::current_path() / "data" / "tna.obj";
        if (std::filesystem::exists(path)) return path;
        return std::filesystem::current_path() / "alice2" / "data" / "tna.obj";
    }
    void reload() {
        m_lastOffset = m_offset;
        m_lastNodalLoad = m_nodalLoad;
        m_lastDensity = m_density;
        m_graph = {};
        m_network = {};
        m_result = {};
        m_detail.clear();
        m_supportForces.resize(0, 0);
        try {
            MeshObject inputMesh("projected_fdm_input");
            inputMesh.readFromObj(dataPath().string());
            const auto welded = weldOffsetCableMesh(*inputMesh.getMeshData());
            m_network = makeOffsetCableNetwork(welded, m_offset, true);

            // Keep the cable selection from the reference sketch, with vertical connectors.
            for (int v = 0; v < m_network.surfaceVertexCount; ++v) {
                int cable = m_network.cableNode[v];
                if (cable < m_network.surfaceVertexCount) continue;
                const Vec3 p = m_network.form.mesh.vertices[v].position;
                m_network.form.mesh.vertices[cable].position = p + Vec3(0, 0, m_offset);
            }
            auto& mesh = m_network.form.mesh;
            m_graph.nodes.resize(mesh.vertices.size());
            const bool useYellowBoundarySupports = m_network.form.supportVertices.empty();
            for (int i = 0; i < int(mesh.vertices.size()); ++i) {
                const Vec3 p = mesh.vertices[i].position;
                auto& node = m_graph.nodes[i];
                node.x0 = Eigen::Vector3d(p.x, p.y, p.z);
                const bool fixed = std::find(m_network.form.supportVertices.begin(),
                    m_network.form.supportVertices.end(), i) != m_network.form.supportVertices.end() ||
                    (useYellowBoundarySupports && std::find(m_network.yellowSupports.begin(),
                        m_network.yellowSupports.end(), i) != m_network.yellowSupports.end());
                if (fixed) for (bool& coordinate : node.fixed) coordinate = true;
                else if (std::find(m_network.sliding.begin(), m_network.sliding.end(), i) != m_network.sliding.end())
                    node.fixed[2] = true;
            }
            for (int k = 0; k < int(m_network.form.activeFormEdges.size()); ++k) {
                const auto& edge = m_network.form.activeFormEdges[k];
                const bool compression = !edge.isTension;
                const double magnitude = m_network.memberKinds[k] == "connector" ? .07 : .1;
                m_graph.edges.push_back({edge.vertexA, edge.vertexB,
                    compression ? fdm::MemberType::Compression : fdm::MemberType::Tension, magnitude});
            }
            // Freeze tributary areas at the input mesh for constant-load FDM.
            const auto areas = TnaSolver::vertexAreas(mesh);
            for (int i = 0; i < m_network.surfaceVertexCount; ++i)
                m_graph.nodes[i].load.z() = -double(m_nodalLoad + m_density * areas[i]);
            solve();
            Vec3 lo, hi;
            mesh.updateBounds(lo, hi);
            Application::getInstance()->getCameraController().focusOnBounds(lo, hi);
        } catch (const std::exception& e) {
            m_graph = {};
            m_network = {};
            m_status = e.what();
        }
    }
    Vec3 inputPosition(int i) const {
        const auto& x = m_graph.nodes[i].x0;
        return Vec3(float(x.x()),float(x.y()),float(x.z()));
    }
    Vec3 position(int i) const {
        if (m_result.X.rows() != int(m_graph.nodes.size())) return inputPosition(i);
        return Vec3(float(m_result.X(i,0)),float(m_result.X(i,1)),float(m_result.X(i,2)));
    }
    void solve() {
        m_lastProjection = m_projection; m_lastTrust = m_trust; m_lastSupportWeight = m_supportWeight;
        if (m_graph.edges.empty()) return;
        fdm::SolverOptions options; options.projection = m_projection; options.trustFraction = m_trust;
        options.supportHorizontalWeight = m_supportWeight;
        options.regularization = 1e-6;
        options.minDensity = 1e-4;
        options.maxIterations = 40;
        m_result = fdm::solveProjectedMixedFDM(m_graph, options);
        m_supportForces.resize(0, 0);
        if (m_result.equilibriumValidated && m_result.X.rows() == int(m_graph.nodes.size()) &&
            m_result.q.size() == int(m_graph.edges.size())) {
            m_supportForces = fdm::forceDensityMatrix(m_graph, m_result.q) * m_result.X;
            for (int i = 0; i < m_supportForces.rows(); ++i)
                for (int d = 0; d < 3; ++d)
                    m_supportForces(i,d) = m_graph.nodes[i].fixed[d]
                        ? m_graph.nodes[i].load[d] - m_supportForces(i,d) : 0.0;
        }
        std::ostringstream text;
        text << m_graph.nodes.size() << " nodes, " << m_graph.edges.size() << " edges | " << m_result.reason;
        if (!m_result.history.empty()) {
            const auto& last = m_result.history.back();
            text << " | iterations " << m_result.history.size()-1
                 << " | shape " << std::setprecision(4) << last.shapeError
                 << " | force residual " << last.forceResidual
                 << " | support horizontal " << last.horizontalSupportForce;
        }
        m_status = text.str();
        std::ostringstream detail;
        detail << "Input fixed-geometry residual " << std::setprecision(3) << m_result.fixedGeometryResidual;
        if (m_result.q.size()) detail << " | q range " << m_result.q.minCoeff() << " to " << m_result.q.maxCoeff();
        m_detail = detail.str();
    }
    fdm::StructuralGraph m_graph;
    OffsetCableNetwork m_network;
    fdm::SolverResult m_result;
    Eigen::MatrixXd m_supportForces;
    std::unique_ptr<SimpleUI> m_ui;
    std::string m_status;
    std::string m_detail;
    float m_projection = .15f, m_trust = .25f, m_supportWeight = 0.f, m_offset = -.5f;
    float m_nodalLoad = .001f, m_density = 1.f;
    float m_lastProjection = -1, m_lastTrust = -1, m_lastSupportWeight = -1;
    float m_lastOffset = 0, m_lastNodalLoad = -1, m_lastDensity = -1;
    bool m_showInput = true, m_showForces = false;
};

ALICE2_REGISTER_SKETCH_AUTO(ProjectedMixedFDMSketch)
#endif

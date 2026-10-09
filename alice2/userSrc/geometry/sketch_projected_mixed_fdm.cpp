#define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>
#include <fdm/ProjectedMixedFDM.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

using namespace alice2;

class ProjectedMixedFDMSketch : public ISketch {
public:
    std::string getName() const override { return "Projected Mixed FDM"; }
    std::string getDescription() const override { return "Compression above tension cables with vertical tension connectors"; }
    std::string getAuthor() const override { return "alice2 User"; }

    void setup() override {
        scene().setBackgroundColor(Color(.96f, .96f, .96f, 1));
        scene().setShowGrid(false);
        m_ui = std::make_unique<SimpleUI>(input());
        m_ui->setTheme(SimpleUI::UITheme::Dark);
        m_ui->addSlider("Projection", Vec2{10, 160}, 220, .01f, 1.f, m_projection);
        m_ui->addSlider("Density trust", Vec2{10, 190}, 220, .01f, 1.f, m_trust);
        m_graph = demoGraph();
        solve();
        Application::getInstance()->getCameraController().focusOnBounds(Vec3(-2.5f,-1.5f,-.3f), Vec3(2.5f,1.5f,1.5f));
    }
    void update(float) override {
        if (m_projection != m_lastProjection || m_trust != m_lastTrust) solve();
    }
    void draw(Renderer& renderer, Camera&) override {
        for (const auto& e : m_graph.edges) {
            const bool cable = e.type == fdm::MemberType::Tension;
            Color color = cable ? Color(.90f,.12f,.45f,1) : Color(.13f,.34f,.82f,1);
            renderer.drawLine(position(e.i), position(e.j), color, cable ? 3.f : 2.f);
            if (m_showInput) renderer.drawLine(inputPosition(e.i), inputPosition(e.j), Color(.65f,.65f,.65f,1), 1.f);
        }
        for (int i = 0; i < int(m_graph.nodes.size()); ++i) {
            const bool fixed = m_graph.nodes[i].fixed[0] && m_graph.nodes[i].fixed[1] && m_graph.nodes[i].fixed[2];
            renderer.drawPoint(position(i), fixed ? Color(.12f,.12f,.12f,1) : Color(.15f,.7f,.25f,1), fixed ? 9.f : 7.f);
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
        renderer.drawString("Projected mixed FDM | blue compression, pink tension | i input | d forces", 10, 30);
        renderer.drawString(m_status, 10, 55);
        renderer.drawString("Top compression (+) | middle vertical and lower cable tension (-)", 10, 80);
        if (m_showForces) renderer.drawString("Green arrows: structure on supports | red: free-node residuals", 10, 105);
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
    static fdm::StructuralGraph demoGraph() {
        fdm::StructuralGraph g;
        const Eigen::Vector3d trueX[] = {
            {-2,-1,0}, {2,-1,0}, {-2,1,0}, {2,1,0},
            {-.7,0,1}, {.7,0,1},
            {-.7,0,.35}, {.7,0,.35}
        };
        for (int i = 0; i < 8; ++i) {
            fdm::Node n; n.x0 = trueX[i];
            if (i < 4) for (bool& f : n.fixed) f = true;
            g.nodes.push_back(n);
        }
        using Type = fdm::MemberType;
        g.edges = {
            {0,4,Type::Compression,1.2}, {2,4,Type::Compression,1.2},
            {1,5,Type::Compression,1.2}, {3,5,Type::Compression,1.2},
            {4,5,Type::Compression,.8}, {0,6,Type::Tension,1.1},
            {2,6,Type::Tension,1.1}, {1,7,Type::Tension,1.1},
            {3,7,Type::Tension,1.1}, {6,7,Type::Tension,.8},
            {4,6,Type::Tension,.7}, {5,7,Type::Tension,.7}
        };
        Eigen::VectorXd q(g.edges.size());
        Eigen::MatrixXd X(8,3);
        for (int i = 0; i < 8; ++i) X.row(i) = trueX[i].transpose();
        for (int k = 0; k < q.size(); ++k)
            q[k] = (g.edges[k].type == Type::Compression ? 1 : -1) * g.edges[k].targetDensityMagnitude;
        Eigen::MatrixXd P = fdm::forceDensityMatrix(g,q) * X;
        for (int i = 4; i < 8; ++i) g.nodes[i].load = P.row(i).transpose();
        g.nodes[4].x0 += Eigen::Vector3d(.2,-.1,.18);
        g.nodes[5].x0 += Eigen::Vector3d(-.15,.12,.1);
        g.nodes[6].x0 += Eigen::Vector3d(.12,.08,-.12);
        g.nodes[7].x0 += Eigen::Vector3d(-.1,-.07,.14);
        return g;
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
        m_lastProjection = m_projection; m_lastTrust = m_trust;
        fdm::SolverOptions options; options.projection = m_projection; options.trustFraction = m_trust;
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
        text << m_result.reason;
        if (!m_result.history.empty()) {
            const auto& last = m_result.history.back();
            text << " | iterations " << m_result.history.size()-1
                 << " | shape " << std::setprecision(4) << last.shapeError
                 << " | force residual " << last.forceResidual;
        }
        m_status = text.str();
    }
    fdm::StructuralGraph m_graph;
    fdm::SolverResult m_result;
    Eigen::MatrixXd m_supportForces;
    std::unique_ptr<SimpleUI> m_ui;
    std::string m_status;
    float m_projection = .15f, m_trust = .25f;
    float m_lastProjection = -1, m_lastTrust = -1;
    bool m_showInput = true, m_showForces = false;
};

ALICE2_REGISTER_SKETCH_AUTO(ProjectedMixedFDMSketch)
#endif

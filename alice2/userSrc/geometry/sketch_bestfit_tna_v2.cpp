#define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>
#include <tna/OffsetCableNetwork.h>
#include <tna/VerticalReactionFit.h>
#include <nlohmann/json.hpp>
#include <array>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <sstream>
#include <iomanip>

using namespace alice2;

// Direct spatial force-density solve; no planar reciprocal topology is required.
class OffsetCableNetworkSketch : public ISketch {
public:
    std::string getName() const override { return "Offset Cable Network (3D)"; }
    std::string getDescription() const override { return "Colored mesh + normal-offset cable network, spatial force-density fitting"; }
    std::string getAuthor() const override { return "alice2 User"; }
    ~OffsetCableNetworkSketch() override { stop(); }
    void setup() override {
        scene().setBackgroundColor(Color(.96f, .96f, .96f, 1));
        scene().setShowGrid(false);
        m_ui = std::make_unique<SimpleUI>(input());
        m_ui->setTheme(SimpleUI::UITheme::Dark);
        m_ui->addSlider("Cable offset", Vec2{10, 200}, 230, -10, -.001f, m_offset);
        m_ui->addSlider("Fit iterations", Vec2{10, 228}, 230, 1, 1000, m_iterations);
        m_ui->addSlider("Design attraction", Vec2{10, 256}, 230, 0, 2, m_attraction);
        m_ui->addSlider("Surface nodal load", Vec2{10, 284}, 230, 0, 10, m_load);
        m_ui->addSlider("Surface weight density", Vec2{10, 312}, 230, 0, 10, m_density);
        m_ui->addSlider("Support force scale", Vec2{10, 340}, 230, 0, 5, m_supportForceScale);
        m_ui->addSlider("Sliding movement penalty", Vec2{10, 368}, 230, 0, 500, m_slidingMovementPenalty);
        m_ui->addSlider("Yellow support penalty", Vec2{10, 396}, 230, 0, 500, m_yellowMovementPenalty);
        reload();
        frame();
    }
    void cleanup() override { stop(); }
    void update(float) override {
        if (controls() != m_applied) reload();
        std::optional<BestFitForceDensityResult> latest;
        std::string error;
        {
            std::lock_guard lock(m_mutex);
            latest = std::exchange(m_latest, std::nullopt);
            error = std::exchange(m_error, {});
        }
        if (latest) { m_result = std::move(*latest); m_status = m_result.diagnostic; }
        if (!error.empty()) { m_status = error; m_result.status = BestFitStatus::Failed; }
    }
    void draw(Renderer& renderer, Camera&) override {
        if (m_network.form.success) {
            if (m_showInput) {
                std::vector<Vec3> segments;
                for (size_t i = 0; i < m_network.form.activeFormEdges.size(); ++i) {
                    if (m_network.memberKinds[i] != "compression") continue;
                    const auto& e = m_network.form.activeFormEdges[i];
                    segments.push_back(m_network.form.mesh.vertices[e.vertexA].position);
                    segments.push_back(m_network.form.mesh.vertices[e.vertexB].position);
                }
                if (!segments.empty()) renderer.drawLines(segments.data(), int(segments.size()), Color(.65f,.65f,.65f,1), 1.f);
            }
            const auto& mesh = m_result.mesh;
            for (size_t i = 0; i < m_network.form.activeFormEdges.size(); ++i) {
                const auto& e = m_network.form.activeFormEdges[i];
                Vec3 segment[] = {mesh.vertices[e.vertexA].position, mesh.vertices[e.vertexB].position};
                const auto& kind = m_network.memberKinds[i];
                Color color = kind == "compression" ? Color(.15f, .35f, .85f, 1)
                    : kind == "cable" ? Color(.95f, .1f, .55f, 1) : Color(.95f, .55f, .1f, 1);
                renderer.drawLines(segment, 2, color, kind == "cable" ? 3.f : 1.5f);
            }
            for (int v : m_network.form.supportVertices)
                renderer.drawPoint(mesh.vertices[v].position, m_network.form.mesh.vertices[v].color, 8);
            for (int v : m_network.sliding)
                renderer.drawPoint(mesh.vertices[v].position, Color(1, 0, 0, 1), 8);
            for (int v : m_network.yellowSupports)
                renderer.drawPoint(mesh.vertices[v].position, Color(1, 1, 0, 1), 8);
        }
        drawForces(renderer);
        renderer.setColor(Color(.06f, .06f, .06f, 1));
        renderer.drawString("q fit | c cancel | r reload | f frame | e export | d forces | i input", 10, 30);
        renderer.drawString(m_status, 10, 52);
        renderer.drawString("Blue compression | pink cables | orange tension connectors", 10, 74);
        renderer.drawString("Black: fixed XYZ | red / boundary yellow: fixed Z, free XY", 10, 96);
        renderer.drawString("Target: vertical-only reactions at every support; offset is initial geometry.", 10, 118);
        if (m_result.report.finite) {
            double maxH = 0;
            for (const auto& r : m_result.report.reactions)
                maxH = std::max(maxH, std::hypot(double(r.x), double(r.y)));
            std::ostringstream out;
            out << "Equilibrium residual " << m_result.report.residual << " | RMS " << m_result.report.rmsDesignDistance
                << " | max support horizontal reaction " << maxH;
            renderer.drawString(out.str(), 10, 145);
        }
        if (m_ui) m_ui->draw(renderer);
    }
    bool onKeyPress(unsigned char key, int, int) override {
        switch (key) {
        case 'q': case 'Q': start(); return true;
        case 'c': case 'C': m_thread.request_stop(); return true;
        case 'r': case 'R': reload(); return true;
        case 'f': case 'F': frame(); return true;
        case 'd': case 'D': m_showForces = !m_showForces; return true;
        case 'i': case 'I': m_showInput = !m_showInput; return true;
        case 'e': case 'E':
            try { exportNetwork(); } catch (const std::exception& e) { m_status = e.what(); }
            return true;
        default: return false;
        }
    }
    bool onMousePress(int b, int s, int x, int y) override { return m_ui && m_ui->onMousePress(b, s, x, y); }
    bool onMouseMove(int x, int y) override { return m_ui && m_ui->onMouseMove(x, y); }
private:
    void drawForces(Renderer& renderer) const {
        if (!m_showForces || !m_result.report.finite) return;
        const auto& mesh = m_result.mesh;
        for (size_t i = 0; i < m_result.edges.size() && i < m_result.forceDensities.size(); ++i) {
            const auto& edge = m_result.edges[i];
            const float q = m_result.forceDensities[i];
            if (!std::isfinite(q)) continue;
            const Vec3 midpoint = (mesh.vertices[edge.vertexA].position + mesh.vertices[edge.vertexB].position) * .5f;
            std::ostringstream label;
            label << std::fixed << std::setprecision(3) << q;
            renderer.setColor(q < 0 ? Color(.85f, .08f, .35f, 1) : Color(.05f, .32f, .82f, 1));
            renderer.drawText(label.str(), midpoint + Vec3(0,0,.001f), 1.f);
        }
        if (m_supportForceScale <= 0) return;
        const Color green(.10f, .82f, .22f, 1);
        std::vector<int> supports = m_network.form.supportVertices;
        for (int v : m_settings.loads.additionalFixedVertices)
            if (std::find(supports.begin(), supports.end(), v) == supports.end()) supports.push_back(v);
        for (int v : supports) {
            if (v < 0 || v >= static_cast<int>(m_result.report.reactions.size())) continue;
            // Physical support reaction, including the applied nodal load.
            // Red supports have only a vertical constrained component.
            const Vec3 force = m_result.report.reactions[v];
            const float magnitude = force.length();
            if (!std::isfinite(magnitude) || magnitude < 1e-4f) continue;
            const Vec3 start = mesh.vertices[v].position;
            const Vec3 end = start + force * m_supportForceScale;
            const Vec3 direction = force / magnitude;
            const Vec3 up = std::abs(direction.z) > .9f ? Vec3(1,0,0) : Vec3(0,0,1);
            const Vec3 side = direction.cross(up).normalized();
            const Vec3 normal = direction.cross(side).normalized();
            const float length = std::min(.25f * magnitude * m_supportForceScale, .25f);
            const Vec3 base = end - direction * length;
            renderer.drawLine(start, end, green, 2.5f);
            for (const Vec3& wing : {side, side * -1.f, normal, normal * -1.f})
                renderer.drawLine(end, base + wing * (length * .4f), green, 2.f);
            renderer.drawPoint(end, green, 4.f);
            std::ostringstream label;
            label << std::fixed << std::setprecision(2) << magnitude;
            renderer.setColor(green);
            renderer.drawText(label.str(), end + Vec3(0,0,.01f), 1.f);
        }
    }
    static std::filesystem::path dataPath(const char* name) {
        return std::filesystem::current_path() / "data" / name;
    }
    std::array<float, 7> controls() const { return {m_offset, m_iterations, m_attraction, m_load, m_density, m_slidingMovementPenalty, m_yellowMovementPenalty}; }
    void stop() {
        m_thread.request_stop();
        if (m_thread.joinable()) m_thread.join();
        std::lock_guard lock(m_mutex);
        m_latest.reset(); m_error.clear();
    }
    void reload() {
        stop();
        m_applied = controls(); m_network = {}; m_result = {};
        try {
            MeshObject inputMesh("offset_cable_input");
            inputMesh.readFromObj(dataPath("tna.obj").string());
            const auto welded = weldOffsetCableMesh(*inputMesh.getMeshData());
            m_network = makeOffsetCableNetwork(welded, m_offset, true);
            m_settings = offsetCableSettings(m_network, m_load, m_density);
            m_settings.maximumIterations = std::max(1, int(std::lround(m_iterations)));
            m_settings.designAttraction = m_attraction;
            VerticalReactionFit solver;
            const bool valid = solver.reset(m_network.form, m_settings, m_slidingMovementPenalty, m_network.yellowSupports, m_yellowMovementPenalty);
            m_result = solver.result();
            if (!valid) { m_status = m_result.diagnostic; return; }
            m_result.status = BestFitStatus::Idle;
            m_status = "Ready: " + std::to_string(m_result.mesh.vertices.size()) + " nodes, "
                + std::to_string(m_result.edges.size()) + " members, "
                + std::to_string(m_result.mesh.vertices.size() - m_network.surfaceVertexCount)
                + " offset nodes; q to fit";
        } catch (const std::exception& e) { m_network = {}; m_status = e.what(); }
    }
    void start() {
        stop();
        if (controls() != m_applied) reload();
        if (!m_network.form.success) return;
        VerticalReactionFit solver;
        if (!solver.reset(m_network.form, m_settings, m_slidingMovementPenalty, m_network.yellowSupports, m_yellowMovementPenalty)) { m_status = solver.result().diagnostic; return; }
        m_result = solver.result(); m_status = "Fitting spatial network...";
        m_thread = std::jthread([this, solver = std::move(solver)](std::stop_token token) mutable {
            try {
                while (solver.result().status == BestFitStatus::Running) {
                    solver.step(token);
                    auto snapshot = solver.result();
                    std::lock_guard lock(m_mutex);
                    m_latest = std::move(snapshot);
                }
            } catch (const std::exception& e) {
                std::lock_guard lock(m_mutex); m_error = std::string("Fit failed: ") + e.what();
            }
        });
    }
    void frame() {
        if (m_result.mesh.vertices.empty() || !Application::getInstance()) return;
        Vec3 lo, hi;
        m_result.mesh.updateBounds(lo, hi);
        Application::getInstance()->getCameraController().focusOnBounds(lo, hi);
    }
    void exportNetwork() {
        if (!m_network.form.success) throw std::runtime_error("Load a valid network first");
        nlohmann::json out;
        out["equilibrated"] = m_result.success;
        out["force_density_convention"] = "q = axial_force / length; positive compression, negative tension";
        out["fit_iteration"] = m_result.iteration;
        out["diagnostic"] = m_result.diagnostic;
        out["initial_offset"] = m_offset;
        out["sliding_movement_penalty"] = m_slidingMovementPenalty;
        out["yellow_movement_penalty"] = m_yellowMovementPenalty;
        out["yellow_supports"] = m_network.yellowSupports;
        out["vertical_only_reactions_required"] = true;
        out["surface_vertex_count"] = m_network.surfaceVertexCount;
        out["source_to_cable_node"] = m_network.cableNode;
        out["fixed_xyz"] = m_network.form.supportVertices;
        out["fixed_z"] = m_settings.loads.additionalFixedVertices;
        out["support_force_convention"] = "reaction is the support force on the network, matching the green arrows; force_on_support is its opposite";
        out["support_forces"] = nlohmann::json::array();
        std::vector<int> supportIds = m_network.form.supportVertices;
        for (int v : m_settings.loads.additionalFixedVertices)
            if (std::find(supportIds.begin(), supportIds.end(), v) == supportIds.end()) supportIds.push_back(v);
        for (int v : supportIds) {
            const auto& p = m_result.mesh.vertices[v].position;
            const bool fixed = std::find(m_network.form.supportVertices.begin(), m_network.form.supportVertices.end(), v)
                != m_network.form.supportVertices.end();
            const bool yellow = std::find(m_network.yellowSupports.begin(), m_network.yellowSupports.end(), v)
                != m_network.yellowSupports.end();
            nlohmann::json support = {{"node_id", v}, {"position", {p.x, p.y, p.z}},
                {"type", fixed ? "fixed_xyz" : yellow ? "yellow_sliding" : "red_sliding"},
                {"reaction", nullptr}, {"force_on_support", nullptr}, {"magnitude", nullptr},
                {"horizontal_magnitude", nullptr}, {"nodal_residual", nullptr}};
            if (m_result.report.finite && v < static_cast<int>(m_result.report.reactions.size())) {
                const auto& r = m_result.report.reactions[v];
                support["reaction"] = {r.x, r.y, r.z};
                support["force_on_support"] = {-r.x, -r.y, -r.z};
                support["magnitude"] = std::hypot(double(r.x), double(r.y), double(r.z));
                support["horizontal_magnitude"] = std::hypot(double(r.x), double(r.y));
                // Sliding-support XY reactions are zero by constraint, even if
                // an unfinished iterate still has horizontal force imbalance.
                if (v < static_cast<int>(m_result.report.residuals.size())) {
                    const auto& residual = m_result.report.residuals[v];
                    support["nodal_residual"] = {residual.x, residual.y, residual.z};
                }
            }
            out["support_forces"].push_back(support);
        }
        out["faces"] = nlohmann::json::array();
        for (const auto& f : m_network.form.mesh.faces) out["faces"].push_back(f.vertices);
        for (size_t i = 0; i < m_result.mesh.vertices.size(); ++i) {
            const auto& p = m_result.mesh.vertices[i].position;
            const auto& original = m_network.form.mesh.vertices[i].position;
            nlohmann::json node = {{"id", i}, {"position", {p.x, p.y, p.z}},
                {"reference", {original.x, original.y, original.z}}};
            if (m_result.report.finite) {
                const auto& r = m_result.report.reactions[i];
                node["reaction"] = {r.x, r.y, r.z};
                node["vertical_load"] = m_result.report.loads[i];
            }
            out["nodes"].push_back(node);
        }
        for (size_t i = 0; i < m_network.form.activeFormEdges.size(); ++i) {
            const auto& e = m_network.form.activeFormEdges[i];
            nlohmann::json member = {{"a", e.vertexA}, {"b", e.vertexB}, {"kind", m_network.memberKinds[i]}, {"tension", e.isTension}};
            // Export the displayed iterate even if it has not equilibrated.
            // Keep unavailable values explicit rather than silently omitting q.
            member["force_density"] = nullptr;
            if (i < m_result.forceDensities.size() && std::isfinite(m_result.forceDensities[i]))
                member["force_density"] = m_result.forceDensities[i];
            out["members"].push_back(member);
        }
        const auto path = dataPath("offset_cable_network.json");
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path);
        if (!file || !(file << out.dump(2))) throw std::runtime_error("Cannot write offset_cable_network.json");
        file.close();
        if (!file) throw std::runtime_error("Cannot finish offset_cable_network.json");

        const auto objPath = dataPath("offset_cable_network.obj");
        std::ofstream obj(objPath);
        if (!obj) throw std::runtime_error("Cannot write offset_cable_network.obj");
        obj << "# Surface mesh and tension network; shared 1-based vertex indices\n"
            << "# equilibrated: " << (m_result.success ? "true" : "false") << '\n'
            << std::setprecision(9);
        for (const auto& vertex : m_result.mesh.vertices) {
            const auto& p = vertex.position;
            obj << "v " << p.x << ' ' << p.y << ' ' << p.z << '\n';
        }
        obj << "g surface\n";
        for (const auto& face : m_network.form.mesh.faces) {
            obj << 'f';
            for (int v : face.vertices) obj << ' ' << v + 1;
            obj << '\n';
        }
        for (const std::string kind : {"cable", "connector"}) {
            obj << "g " << (kind == "cable" ? "cables" : "tension_connectors") << '\n';
            for (size_t i = 0; i < m_network.form.activeFormEdges.size(); ++i) {
                if (m_network.memberKinds[i] != kind) continue;
                const auto& edge = m_network.form.activeFormEdges[i];
                obj << "l " << edge.vertexA + 1 << ' ' << edge.vertexB + 1 << '\n';
            }
        }
        obj.close();
        if (!obj) throw std::runtime_error("Cannot finish offset_cable_network.obj");
        m_status = "Exported data/offset_cable_network.json and .obj";
    }
    std::unique_ptr<SimpleUI> m_ui;
    OffsetCableNetwork m_network;
    BestFitForceDensitySettings m_settings;
    BestFitForceDensityResult m_result;
    float m_offset{-.05f}, m_iterations{50}, m_attraction{1}, m_load{.001f}, m_density{1};
    float m_supportForceScale{.4f}, m_slidingMovementPenalty{0.0f}, m_yellowMovementPenalty{50};
    bool m_showForces{false}, m_showInput{true};
    std::array<float, 7> m_applied{};
    std::string m_status;
    std::mutex m_mutex;
    std::optional<BestFitForceDensityResult> m_latest;
    std::string m_error;
    std::jthread m_thread;
};

ALICE2_REGISTER_SKETCH_AUTO(OffsetCableNetworkSketch)

#endif

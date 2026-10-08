#define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>
#include <tna/OffsetCableNetwork.h>
#include <tna/TnaOffsetSolver.h>
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

// Experiment: compression-only reciprocal TNA, then spatial mixed-sign fitting.
class TnaOffsetBestFitSketch : public ISketch {
public:
    std::string getName() const override { return "Compression Fit + TNA + Cable Fit"; }
    std::string getDescription() const override { return "q compression best fit, t reciprocal TNA, c offset cable best fit"; }
    std::string getAuthor() const override { return "alice2 User"; }
    ~TnaOffsetBestFitSketch() override { stop(); }
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
        m_ui->addSlider("TNA H iterations", Vec2{10, 424}, 230, 100, 10000, m_horizontalIterations);
        m_ui->addSlider("TNA form weight", Vec2{10, 452}, 230, 0, 1, m_formWeight);
        m_ui->addSlider("TNA force scale", Vec2{10, 480}, 230, .01f, 20, m_forceScale);
        reload();
        frame();
    }
    void cleanup() override { stop(); }
    void update(float) override {
        if (controls() != m_applied) {
            stop();
            if (m_solver) { m_solver->configure(experimentSettings()); apply(snapshot(*m_solver)); }
            m_applied = controls();
        }
        std::optional<Snapshot> latest;
        std::string error;
        {
            std::lock_guard lock(m_mutex);
            latest = std::exchange(m_latest, std::nullopt);
            error = std::exchange(m_error, {});
        }
        if (latest) apply(std::move(*latest));
        if (!error.empty()) { m_status = error; m_result.status = BestFitStatus::Failed; m_result.success = false; m_stage = TnaOffsetStage::Failed; }
    }
    void draw(Renderer& renderer, Camera&) override {
        if (m_network.form.success) {
            if (m_showInput) {
                drawSurface(renderer, m_input, Color(.65f,.65f,.65f,1));
            }
            if (m_showTna) drawSurface(renderer, m_compression, Color(.1f,.65f,.3f,1));
            const bool showMixed = m_offsetBuiltFromTna;
            const auto& mesh = showMixed ? m_result.mesh : m_compression.vertices.empty() ? m_input : m_compression;
            if (!showMixed) drawSurface(renderer, mesh, Color(.15f,.35f,.85f,1));
            for (size_t i = 0; showMixed && i < m_network.form.activeFormEdges.size(); ++i) {
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
        drawForceDiagram(renderer);
        renderer.setColor(Color(.06f, .06f, .06f, 1));
        renderer.drawString("q compression fit | t TNA | c cable fit | Esc cancel | f frame | e export | d forces | i input", 10, 30);
        renderer.drawString(m_status, 10, 52);
        renderer.drawString("Blue compression | pink cables | orange tension connectors", 10, 74);
        renderer.drawString(m_offsetBuiltFromTna
            ? "Black: fixed XYZ | red / boundary yellow: fixed Z, free XY"
            : "Compression TNA: all coloured supports temporarily fixed XYZ", 10, 96);
        renderer.drawString("Compression fit -> TNA + force diagram -> cable fit | v force diagram | g TNA reference", 10, 118);
        if (m_showForceDiagram && !m_force.mesh.vertices.empty())
            renderer.drawString("Right: compression TNA reciprocal (blue = aligned, red = direction error); frozen during cable fit", 10, 173);
        const auto& report = m_offsetBuiltFromTna ? m_result.report : m_compressionReport;
        if (report.finite) {
            double maxH = 0;
            for (const auto& r : report.reactions)
                maxH = std::max(maxH, std::hypot(double(r.x), double(r.y)));
            std::ostringstream out;
            out << (m_offsetBuiltFromTna ? "Mixed residual " : "Compression residual ") << report.residual << " | RMS " << report.rmsDesignDistance
                << " | max support horizontal reaction " << maxH;
            renderer.drawString(out.str(), 10, 145);
        }
        if (m_ui) m_ui->draw(renderer);
    }
    bool onKeyPress(unsigned char key, int, int) override {
        switch (key) {
        case 'q': case 'Q': runStage(1); return true;
        case 'c': case 'C': runStage(3); return true;
        case 'r': case 'R': reload(); return true;
        case 'f': case 'F': frame(); return true;
        case 'd': case 'D': m_showForces = !m_showForces; return true;
        case 'i': case 'I': m_showInput = !m_showInput; return true;
        case 't': case 'T': m_showForceDiagram = true; runStage(2); return true;
        case 27: m_thread.request_stop(); return true;
        case 'v': case 'V': m_showForceDiagram = !m_showForceDiagram; frame(); return true;
        case 'g': case 'G': m_showTna = !m_showTna; return true;
        case 'e': case 'E':
            try { exportNetwork(); } catch (const std::exception& e) { m_status = e.what(); }
            return true;
        default: return false;
        }
    }
    bool onMousePress(int b, int s, int x, int y) override { return m_ui && m_ui->onMousePress(b, s, x, y); }
    bool onMouseMove(int x, int y) override { return m_ui && m_ui->onMouseMove(x, y); }
private:
    struct Snapshot {
        BestFitForceDensityResult result;
        OffsetCableNetwork network;
        BestFitForceDensitySettings settings;
        MeshData compression;
        TnaOffsetStage stage;
        bool offsetBuiltFromTna;
        std::vector<TnaEdge> compressionEdges;
        std::vector<float> compressionQ;
        TnaEquilibriumReport compressionReport;
        TnaForceDiagram force;
        MeshData fittedCompression;
    };
    static Snapshot snapshot(const TnaOffsetSolver& solver) {
        auto compression = solver.compressionReference();
        if (!compression.vertices.empty()) compression.faces = solver.input().faces;
        return {solver.result(), solver.network(), solver.fitSettings(), std::move(compression), solver.stage(), solver.offsetBuiltFromTna(),
            solver.compressionEdges(), solver.compressionDensities(), solver.compressionReport(), solver.forceDiagram(), solver.compressionFitResult().mesh};
    }
    void apply(Snapshot latest) {
        const bool firstForce = m_force.mesh.vertices.empty() && !latest.force.mesh.vertices.empty();
        m_result = std::move(latest.result); m_network = std::move(latest.network); m_settings = std::move(latest.settings);
        m_compression = std::move(latest.compression); m_offsetBuiltFromTna = latest.offsetBuiltFromTna;
        m_compressionEdges = std::move(latest.compressionEdges); m_compressionQ = std::move(latest.compressionQ);
        m_compressionReport = std::move(latest.compressionReport); m_force = std::move(latest.force);
        m_fittedCompression = std::move(latest.fittedCompression); m_stage = latest.stage; m_status = m_result.diagnostic;
        if (firstForce) frame();
    }
    // Display-only translation and scale: raw reciprocal coordinates stay untouched.
    std::pair<float,Vec3> forceTransform() const {
        if (m_force.mesh.vertices.empty() || m_result.mesh.vertices.empty()) return {1,Vec3(0,0,0)};
        Vec3 formMin,formMax,forceMin,forceMax;
        m_result.mesh.updateBounds(formMin,formMax); m_force.mesh.updateBounds(forceMin,forceMax);
        const float formSize = std::max({formMax.x-formMin.x,formMax.y-formMin.y,.001f});
        const float forceSize = std::max(forceMax.x-forceMin.x,forceMax.y-forceMin.y);
        const float scale = forceSize > 1e-6f ? formSize/forceSize : 1;
        const float gap = std::max(.05f,.12f*formSize);
        return {scale,Vec3(formMax.x+gap-forceMin.x*scale,
            (formMin.y+formMax.y-(forceMin.y+forceMax.y)*scale)*.5f,formMin.z)};
    }
    void drawForceDiagram(Renderer& renderer) const {
        if (!m_showForceDiagram || m_force.mesh.vertices.empty()) return;
        const auto [scale,offset] = forceTransform();
        for (size_t i = 0; i < m_force.mesh.edges.size(); ++i) {
            const auto& e = m_force.mesh.edges[i];
            const float error = i < m_force.edgeDirectionDeviationsDegrees.size() ? m_force.edgeDirectionDeviationsDegrees[i] : 180;
            const float t = std::isfinite(error) ? std::clamp(error/90.f,0.f,1.f) : 1;
            const Color color(.1f+.8f*t,.35f*(1-t),.85f*(1-t),1);
            const auto a = m_force.mesh.vertices[e.vertexA].position*scale+offset;
            const auto b = m_force.mesh.vertices[e.vertexB].position*scale+offset;
            renderer.drawLine(a,b,color,2.f);
            if (m_showForces) {
                std::ostringstream label; label << std::fixed << std::setprecision(1) << error;
                renderer.setColor(color); renderer.drawText(label.str(),(a+b)*.5f,1.f);
            }
        }
    }
    static void drawSurface(Renderer& renderer, const MeshData& mesh, const Color& color) {
        std::vector<Vec3> segments;
        for (const auto& face : mesh.faces) for (size_t i = 0; i < face.vertices.size(); ++i) {
            segments.push_back(mesh.vertices[face.vertices[i]].position);
            segments.push_back(mesh.vertices[face.vertices[(i+1)%face.vertices.size()]].position);
        }
        if (!segments.empty()) renderer.drawLines(segments.data(), int(segments.size()), color, 1.f);
    }
    TnaOffsetSettings experimentSettings() const {
        TnaOffsetSettings s;
        s.offset = m_offset; s.nodalLoad = m_load; s.density = m_density;
        s.fitIterations = std::max(1, int(std::lround(m_iterations)));
        s.horizontalIterations = std::max(1, int(std::lround(m_horizontalIterations)));
        s.formWeight = m_formWeight; s.forceScale = m_forceScale;
        s.attraction = m_attraction; s.redPenalty = m_slidingMovementPenalty; s.yellowPenalty = m_yellowMovementPenalty;
        return s;
    }
    void drawForces(Renderer& renderer) const {
        const auto& report = m_offsetBuiltFromTna ? m_result.report : m_compressionReport;
        if (!m_showForces || !report.finite) return;
        const auto& mesh = m_offsetBuiltFromTna ? m_result.mesh : m_compression;
        const auto& edges = m_offsetBuiltFromTna ? m_result.edges : m_compressionEdges;
        const auto& densities = m_offsetBuiltFromTna ? m_result.forceDensities : m_compressionQ;
        for (size_t i = 0; i < edges.size() && i < densities.size(); ++i) {
            const auto& edge = edges[i];
            const float q = densities[i];
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
            if (v < 0 || v >= static_cast<int>(report.reactions.size())) continue;
            // Physical support reaction, including the applied nodal load.
            // Red supports have only a vertical constrained component.
            const Vec3 force = report.reactions[v];
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
    std::array<float, 10> controls() const { return {m_offset, m_iterations, m_attraction, m_load, m_density, m_slidingMovementPenalty, m_yellowMovementPenalty, m_horizontalIterations, m_formWeight, m_forceScale}; }
    void stop() {
        m_thread.request_stop();
        if (m_thread.joinable()) m_thread.join();
        std::lock_guard lock(m_mutex);
        m_latest.reset(); m_error.clear();
    }
    void reload() {
        stop(); m_applied = controls();
        m_network = {}; m_result = {}; m_input = {}; m_compression = {}; m_fittedCompression = {}; m_force = {};
        m_compressionEdges.clear(); m_compressionQ.clear(); m_compressionReport = {};
        m_stage = TnaOffsetStage::Ready; m_offsetBuiltFromTna = false; m_solver.reset();
        try {
            MeshObject inputMesh("tna_offset_input");
            inputMesh.readFromObj(dataPath("tna.obj").string());
            m_input = weldOffsetCableMesh(*inputMesh.getMeshData());
            m_solver = std::make_unique<TnaOffsetSolver>();
            m_solver->reset(m_input,experimentSettings()); apply(snapshot(*m_solver));
        } catch (const std::exception& e) { m_network = {}; m_status = e.what(); }
    }
    void runStage(int stage) {
        stop();
        if (!m_solver) return;
        if (controls() != m_applied) { m_solver->configure(experimentSettings()); m_applied = controls(); }
        const bool started = stage == 1 ? m_solver->startCompressionFit() : stage == 2 ? m_solver->startTna() : m_solver->startCableFit();
        apply(snapshot(*m_solver));
        if (!started) return;
        m_thread = std::jthread([this](std::stop_token token) {
            try {
                int steps = 0;
                while (m_solver->running()) {
                    const auto previous = m_solver->stage();
                    m_solver->step(token);
                    if (m_solver->stage() == previous && previous == TnaOffsetStage::Horizontal && ++steps%20) continue;
                    auto latest = snapshot(*m_solver);
                    std::lock_guard lock(m_mutex); m_latest = std::move(latest);
                }
            } catch (const std::exception& e) {
                std::stop_source cancelled; cancelled.request_stop(); m_solver->step(cancelled.get_token());
                std::lock_guard lock(m_mutex); m_error = std::string("Stage failed: ") + e.what();
            }
        });
    }
    void frame() {
        if (m_result.mesh.vertices.empty() || !Application::getInstance()) return;
        Vec3 lo, hi;
        m_result.mesh.updateBounds(lo, hi);
        if (m_showForceDiagram) {
            const auto [scale,offset] = forceTransform();
            for (const auto& v : m_force.mesh.vertices) {
                const auto p = v.position*scale+offset;
                lo.x = std::min(lo.x,p.x); lo.y = std::min(lo.y,p.y); lo.z = std::min(lo.z,p.z);
                hi.x = std::max(hi.x,p.x); hi.y = std::max(hi.y,p.y); hi.z = std::max(hi.z,p.z);
            }
        }
        Application::getInstance()->getCameraController().focusOnBounds(lo, hi);
    }
    void exportNetwork() {
        if (!m_network.form.success) throw std::runtime_error("Load a valid network first");
        nlohmann::json out;
        out["equilibrated"] = m_stage == TnaOffsetStage::Complete && m_result.success;
        out["compression_equilibrated"] = !m_offsetBuiltFromTna && m_result.success &&
            (m_stage == TnaOffsetStage::CompressionFitted || m_stage == TnaOffsetStage::TnaComplete);
        out["procedure"] = "compression best fit, fitted-density horizontal + vertical TNA, then normal-offset cable best fit";
        out["stage"] = m_stage == TnaOffsetStage::Ready ? "ready" :
            m_stage == TnaOffsetStage::CompressionFitting ? "compression_fit" : m_stage == TnaOffsetStage::CompressionFitted ? "compression_fitted" :
            m_stage == TnaOffsetStage::TnaComplete ? "tna_complete" : m_stage == TnaOffsetStage::Horizontal ? "compression_horizontal" :
            m_stage == TnaOffsetStage::Vertical ? "compression_vertical" : m_stage == TnaOffsetStage::CableFitting ? "mixed_fit" :
            m_stage == TnaOffsetStage::Complete ? "complete" : m_stage == TnaOffsetStage::Cancelled ? "cancelled" : "failed";
        out["reference_description"] = m_offsetBuiltFromTna
            ? "compression TNA surface plus normal-offset cable nodes; positional reference for mixed fit"
            : m_force.mesh.vertices.empty() ? "original input for compression best fit" : "fitted compression shape used to initialise TNA";
        out["tna_horizontal_iterations"] = int(m_horizontalIterations);
        out["tna_form_weight"] = m_formWeight;
        out["tna_force_scale"] = m_forceScale;
        out["compression_tna_residual"] = m_compressionReport.residual;
        out["compression_tna_members"] = nlohmann::json::array();
        for (size_t i = 0; i < m_compressionEdges.size() && i < m_compressionQ.size(); ++i) {
            const auto& e = m_compressionEdges[i];
            out["compression_tna_members"].push_back({{"a",e.vertexA},{"b",e.vertexB},{"force_density",m_compressionQ[i]}});
        }
        out["mixed_residual"] = m_offsetBuiltFromTna && m_result.report.finite ? nlohmann::json(m_result.report.residual) : nlohmann::json(nullptr);
        out["compression_tna_positions"] = nlohmann::json::array();
        for (const auto& v : m_compression.vertices)
            out["compression_tna_positions"].push_back({v.position.x,v.position.y,v.position.z});
        out["compression_bestfit_positions"] = nlohmann::json::array();
        for (const auto& v : m_fittedCompression.vertices)
            out["compression_bestfit_positions"].push_back({v.position.x,v.position.y,v.position.z});
        out["force_diagram"]["note"] = "Compression TNA reciprocal only; not updated by the spatial cable fit";
        out["force_diagram"]["vertices"] = nlohmann::json::array();
        for (const auto& v : m_force.mesh.vertices)
            out["force_diagram"]["vertices"].push_back({v.position.x,v.position.y,v.position.z});
        out["force_diagram"]["edges"] = nlohmann::json::array();
        for (const auto& e : m_force.mesh.edges) out["force_diagram"]["edges"].push_back({e.vertexA,e.vertexB});
        out["input_positions"] = nlohmann::json::array();
        for (const auto& v : m_input.vertices)
            out["input_positions"].push_back({v.position.x,v.position.y,v.position.z});
        out["force_density_convention"] = "q = axial_force / length; positive compression, negative tension";
        out["fit_iteration"] = m_result.iteration;
        out["diagnostic"] = m_result.diagnostic;
        out["initial_offset"] = m_offset;
        out["sliding_movement_penalty"] = m_slidingMovementPenalty;
        out["yellow_movement_penalty"] = m_yellowMovementPenalty;
        out["yellow_supports"] = m_network.yellowSupports;
        out["vertical_only_reactions_required"] = m_offsetBuiltFromTna;
        out["surface_vertex_count"] = m_network.surfaceVertexCount;
        out["source_to_cable_node"] = m_offsetBuiltFromTna ? m_network.cableNode : std::vector<int>(m_input.vertices.size(),-1);
        out["support_force_convention"] = "reaction is the support force on the network, matching the green arrows; force_on_support is its opposite";
        out["support_forces"] = nlohmann::json::array();
        std::vector<int> supportIds = m_network.form.supportVertices;
        for (int v : m_settings.loads.additionalFixedVertices)
            if (std::find(supportIds.begin(), supportIds.end(), v) == supportIds.end()) supportIds.push_back(v);
        out["fixed_xyz"] = m_offsetBuiltFromTna ? m_network.form.supportVertices : supportIds;
        out["fixed_z"] = m_offsetBuiltFromTna ? m_settings.loads.additionalFixedVertices : std::vector<int>{};
        for (int v : supportIds) {
            const auto& p = m_result.mesh.vertices[v].position;
            const bool fixed = std::find(m_network.form.supportVertices.begin(), m_network.form.supportVertices.end(), v)
                != m_network.form.supportVertices.end();
            const bool yellow = std::find(m_network.yellowSupports.begin(), m_network.yellowSupports.end(), v)
                != m_network.yellowSupports.end();
            nlohmann::json support = {{"node_id", v}, {"position", {p.x, p.y, p.z}},
                {"type", fixed ? "fixed_xyz" : yellow ? (m_offsetBuiltFromTna ? "yellow_sliding" : "yellow_thrust")
                    : (m_offsetBuiltFromTna ? "red_sliding" : "red_thrust")},
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
        for (const auto& f : m_input.faces) out["faces"].push_back(f.vertices);
        const auto& reference = m_offsetBuiltFromTna ? m_network.form.mesh :
            m_force.mesh.vertices.empty() ? m_input : m_fittedCompression;
        for (size_t i = 0; i < m_result.mesh.vertices.size(); ++i) {
            const auto& p = m_result.mesh.vertices[i].position;
            const auto& original = reference.vertices[i].position;
            nlohmann::json node = {{"id", i}, {"position", {p.x, p.y, p.z}},
                {"reference", {original.x, original.y, original.z}}};
            if (m_result.report.finite) {
                const auto& r = m_result.report.reactions[i];
                node["reaction"] = {r.x, r.y, r.z};
                node["vertical_load"] = m_result.report.loads[i];
            }
            out["nodes"].push_back(node);
        }
        for (size_t i = 0; i < m_result.edges.size(); ++i) {
            const auto& e = m_result.edges[i];
            nlohmann::json member = {{"a", e.vertexA}, {"b", e.vertexB},
                {"kind", m_offsetBuiltFromTna ? m_network.memberKinds[i] : "compression"}, {"tension", e.isTension}};
            // Export the displayed iterate even if it has not equilibrated.
            // Keep unavailable values explicit rather than silently omitting q.
            member["force_density"] = nullptr;
            if (i < m_result.forceDensities.size() && std::isfinite(m_result.forceDensities[i]))
                member["force_density"] = m_result.forceDensities[i];
            out["members"].push_back(member);
        }
        const auto path = dataPath("tna_offset_bestfit.json");
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path);
        if (!file || !(file << out.dump(2))) throw std::runtime_error("Cannot write tna_offset_bestfit.json");
        file.close();
        if (!file) throw std::runtime_error("Cannot finish tna_offset_bestfit.json");

        const auto objPath = dataPath("tna_offset_bestfit.obj");
        std::ofstream obj(objPath);
        if (!obj) throw std::runtime_error("Cannot write tna_offset_bestfit.obj");
        obj << "# Surface mesh and tension network; shared 1-based vertex indices\n"
            << "# mixed equilibrium verified: " << (m_stage == TnaOffsetStage::Complete && m_result.success ? "true" : "false") << '\n'
            << std::setprecision(9);
        for (const auto& vertex : m_result.mesh.vertices) {
            const auto& p = vertex.position;
            obj << "v " << p.x << ' ' << p.y << ' ' << p.z << '\n';
        }
        obj << "g surface\n";
        for (const auto& face : m_input.faces) {
            obj << 'f';
            for (int v : face.vertices) obj << ' ' << v + 1;
            obj << '\n';
        }
        for (const std::string kind : {"cable", "connector"}) {
            obj << "g " << (kind == "cable" ? "cables" : "tension_connectors") << '\n';
            for (size_t i = 0; m_offsetBuiltFromTna && i < m_network.form.activeFormEdges.size(); ++i) {
                if (m_network.memberKinds[i] != kind) continue;
                const auto& edge = m_network.form.activeFormEdges[i];
                obj << "l " << edge.vertexA + 1 << ' ' << edge.vertexB + 1 << '\n';
            }
        }
        obj.close();
        if (!obj) throw std::runtime_error("Cannot finish tna_offset_bestfit.obj");
        m_status = "Exported data/tna_offset_bestfit.json and .obj";
    }
    std::unique_ptr<SimpleUI> m_ui;
    OffsetCableNetwork m_network;
    BestFitForceDensitySettings m_settings;
    BestFitForceDensityResult m_result;
    float m_offset{-.05f}, m_iterations{50}, m_attraction{1}, m_load{.001f}, m_density{1};
    float m_supportForceScale{.4f}, m_slidingMovementPenalty{0.0f}, m_yellowMovementPenalty{50};
    MeshData m_input, m_compression, m_fittedCompression;
    TnaForceDiagram m_force;
    std::unique_ptr<TnaOffsetSolver> m_solver;
    std::vector<TnaEdge> m_compressionEdges;
    std::vector<float> m_compressionQ;
    TnaEquilibriumReport m_compressionReport;
    TnaOffsetStage m_stage{TnaOffsetStage::Ready};
    float m_horizontalIterations{10000}, m_formWeight{1}, m_forceScale{1};
    bool m_offsetBuiltFromTna{false};
    bool m_showTna{false}, m_showForceDiagram{true};
    bool m_showForces{false}, m_showInput{true};
    std::array<float, 10> m_applied{};
    std::string m_status;
    std::mutex m_mutex;
    std::optional<Snapshot> m_latest;
    std::string m_error;
    std::jthread m_thread;
};

ALICE2_REGISTER_SKETCH_AUTO(TnaOffsetBestFitSketch)

#endif

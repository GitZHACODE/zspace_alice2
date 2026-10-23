
#include <alice2.h>
#include <sketches/SketchRegistry.h>
#include <tna/BestFitTnaSolver.h>
#include <tna/BestFitTnaWorker.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace alice2;

class BestFitTnaSketch : public ISketch {
public:
    std::string getName() const override { return "Best-fit Shape + Fixed-XY TNA"; }
    std::string getDescription() const override { return "1: fit densities and XYZ shape | 2: equilibrate with fitted XY fixed"; }
    std::string getAuthor() const override { return "alice2 User"; }

    void setup() override {
        scene().setBackgroundColor(Color(0.96f, 0.96f, 0.96f, 1));
        scene().setShowGrid(false); scene().setShowAxes(false); scene().setAxesLength(1.2f);
        m_ui = std::make_unique<SimpleUI>(input());
        m_ui->setTheme(SimpleUI::UITheme::Dark);
        m_ui->addSlider("Fit iterations", Vec2{10, 200}, 210, 1, 100, m_fitIterations);
        m_ui->addSlider("Design attraction", Vec2{10, 228}, 210, 0, 2, m_attraction);
        m_ui->addSlider("H max iterations", Vec2{10, 256}, 210, 1, 5000, m_horizontalIterations);
        m_ui->addSlider("H target angle", Vec2{10, 284}, 210, 0, 10, m_angleTolerance);
        m_ui->addSlider("Nodal load", Vec2{10, 312}, 210, -0.01f, 10, m_verticalLoad);
        m_ui->addSlider("Self-weight density", Vec2{10, 340}, 210, 0, 10, m_density);
        m_ui->addSlider("Tension force distance (step 2)", Vec2{10, 368}, 240, -1, 10, m_tensionDistance);
        m_ui->addSlider("Support force scale", Vec2{10, 396}, 240, 0, 5, m_supportForceScale);
        m_ui->addSlider("Sliding support penalty", Vec2{10, 424}, 240, 0.01f, 500.0f, m_slidingPenalty);
        reload();
    }
    void update(float) override {
        if (fitControls() != m_appliedFitControls) reload();
        try {
            if (auto snapshot = m_fitWorker.poll()) {
                m_solver = std::move(*snapshot);
                sync();
            }
        } catch (const std::exception& error) {
            m_fitWorker.discard();
            m_solver = {};
            m_status = std::string("Fit failed: ") + error.what();
        }
        if (tnaControls() != m_appliedTnaControls) {
            m_appliedTnaControls = tnaControls();
            if (m_solver.stage() != BestFitTnaStage::Fitting) { m_solver.restoreFit(); sync(); }
        }
        if (!m_solver.running()) return;
        if (m_solver.stage() == BestFitTnaStage::Fitting) return;
        // Fitting runs on the worker; reciprocal updates remain inexpensive.
        const int steps = m_solver.stage() == BestFitTnaStage::Horizontal ? 2 : 1;
        for (int i = 0; i < steps && m_solver.running(); ++i) m_solver.step();
        sync();
    }
    void cleanup() override { m_fitWorker.discard(); }
    void draw(Renderer& renderer, Camera&) override {
        if (m_showInput) drawMesh(renderer, m_inputMesh, Color(0.72f, 0.72f, 0.72f, 1), 0.8f);
        drawFormDiagram(renderer); drawForceDensities(renderer); drawForceDiagram(renderer); drawSupports(renderer);
        renderer.setColor(Color(0.06f, 0.06f, 0.06f, 1));
        renderer.drawString("q fit XYZ | c cancel fit | w fixed-XY TNA | r reload | e export | f frame | i original | a angles | d densities | o near zero", 10, 30);
        renderer.drawString(m_status, 10, 52);
        renderer.drawString("Blue compression | pink tension | green support force | black fixed XYZ | red fixed Z", 10, 74);
        renderer.drawString(std::string("t boundary tension: ") + (m_tensionEnabled ? "ON" : "OFF") +
            " | b scope: " + (m_allBoundaryTension ? "ALL" : "OUTER") +
            " | g grow: " + (m_growBoundaryTension ? "ON" : "OFF") +
            " | o near zero: " + (m_zeroGrowthDensity ? "ON" : "OFF"), 10, 96);
        renderer.drawString("Step 2 locks the XY coordinates produced by step 1.", 10, 118);
        const auto& report = m_solver.report();
        if (report.finite) {
            std::ostringstream metrics;
            metrics << std::scientific << std::setprecision(3)
                    << "Residual H " << report.horizontalResidual << " | V " << report.verticalResidual
                    << " | design RMS " << report.rmsDesignDistance << " | max " << report.maximumDesignDistance;
            renderer.drawString(metrics.str(), 10, 144);
        }
        renderer.drawString(m_solver.running() ? "Solving..." : "Force diagram fitted to view; solver values unchanged", 10, 174);
        if (m_ui) m_ui->draw(renderer);
    }
    bool onKeyPress(unsigned char key, int, int) override {
        switch (key) {
            case 'q':
                if (fitControls() != m_appliedFitControls) reload();
                m_fitWorker.discard();
                if (m_solver.startFit()) m_fitWorker.start(m_solver);
                sync(); return true;
            case 'c': case 'C':
                if (m_solver.stage() == BestFitTnaStage::Fitting) {
                    m_fitWorker.requestStop();
                    m_status = "Cancelling fit...";
                }
                return true;
            case 'w':
                if (fitControls() != m_appliedFitControls) reload();
                m_appliedTnaControls = tnaControls();
                m_solver.startTna(std::max(1, int(std::lround(m_horizontalIterations))), m_angleTolerance, m_tensionDistance); sync(); return true;
            case 'r': case 'R': reload(); return true;
            case 'f': case 'F': frameDiagrams(); return true;
            case 'e': case 'E':
                try { exportFormFoundMesh(); }
                catch (const std::exception& e) { m_status = std::string("Export failed: ") + e.what(); }
                return true;
            case 'i': case 'I': m_showInput = !m_showInput; return true;
            case 'a': case 'A': m_showForceAngles = !m_showForceAngles; return true;
            case 'd': case 'D': m_showForceDensities = !m_showForceDensities; return true;
            case 't': case 'T': m_tensionEnabled = !m_tensionEnabled; reload(); return true;
            case 'b': case 'B': m_allBoundaryTension = !m_allBoundaryTension; reload(); return true;
            case 'g': case 'G': m_growBoundaryTension = !m_growBoundaryTension; reload(); return true;
            case 'o': case 'O':
                m_zeroGrowthDensity = !m_zeroGrowthDensity;
                if (m_zeroGrowthDensity && !m_growBoundaryTension) m_growBoundaryTension = true;
                reload();
                return true;
            default: return false;
        }
    }
    bool onMousePress(int b, int s, int x, int y) override { return m_ui && m_ui->onMousePress(b, s, x, y); }
    bool onMouseMove(int x, int y) override { return m_ui && m_ui->onMouseMove(x, y); }

private:
    static std::filesystem::path dataPath(const std::string& file) {
        const std::filesystem::path requested(file);
        if (requested.is_absolute() || std::filesystem::exists(requested)) return requested;
        const auto local = std::filesystem::path("data") / file;
        return local;
    }
    std::array<float, 5> fitControls() const { return {m_fitIterations, m_attraction, m_verticalLoad, m_density, m_slidingPenalty}; }
    std::array<float, 3> tnaControls() const { return {m_horizontalIterations, m_angleTolerance, m_tensionDistance}; }
    void sync() {
        if (!m_inputMesh || !m_result.success) return;
        m_formMesh = std::make_shared<MeshData>(m_solver.mesh());
        m_forceResult = m_solver.forceDiagram();
        m_forceMesh = m_forceResult.success ? std::make_shared<MeshData>(m_forceResult.mesh) : nullptr;
        m_status = m_solver.diagnostic();
    }
    void frameDiagrams() {
        if (!m_inputMesh || m_inputMesh->vertices.empty() || !Application::getInstance()) return;
        Vec3 minimum = m_inputMesh->vertices.front().position, maximum = minimum;
        const auto include = [&](const std::shared_ptr<MeshData>& mesh, const Vec3& offset, float scale = 1.0f) {
            if (!mesh) return;
            for (const auto& v : mesh->vertices) {
                const Vec3 p = v.position * scale + offset;
                minimum.x = std::min(minimum.x, p.x); maximum.x = std::max(maximum.x, p.x);
                minimum.y = std::min(minimum.y, p.y); maximum.y = std::max(maximum.y, p.y);
                minimum.z = std::min(minimum.z, p.z); maximum.z = std::max(maximum.z, p.z);
            }
        };
        include(m_inputMesh, Vec3(0, 0, 0)); include(m_formMesh, Vec3(0, 0, 0));
        include(m_forceMesh, forceDiagramOffset(), forceDiagramScale());
        Application::getInstance()->getCameraController().focusOnBounds(minimum, maximum);
    }
    void reload() {
        m_fitWorker.discard();
        m_solver = {}; m_forceResult = {}; m_result = {};
        m_formMesh.reset(); m_forceMesh.reset(); m_inputMesh.reset();
        m_growthEdgeIndices.clear();
        m_appliedFitControls = fitControls(); m_appliedTnaControls = tnaControls();
        try {
            m_inputObject = std::make_shared<MeshObject>("bestfit_tna_input");
            m_inputObject->readFromObj(dataPath("tna.obj").string());
            m_inputObject->weld(1e-3f);
            m_inputMesh = m_inputObject->getMeshData();
            m_verticalOnlySupports = redVertexSupports(*m_inputMesh);
            TnaSolver topology;
            m_result = topology.makeFormDiagram(*m_inputMesh, blackVertexSupports(*m_inputMesh), false);
            if (m_result.success && m_tensionEnabled) {
                if (m_allBoundaryTension) topology.setBoundaryEdgesTension(m_result);
                else {
                    // In this sketch the black-supported boundary is outer;
                    // all remaining loops are inner, regardless of mesh order.
                    // The selector's default (-1) means the first loop, which
                    // may be a hole, so always seed it with a black support.
                    if (m_result.supportVertices.empty()) {
                        m_status = "Outer ring needs a black boundary support";
                        return;
                    }
                    if (!topology.setOuterBoundaryEdgesTension(m_result, m_result.supportVertices.front())) {
                        m_status = m_result.diagnostic;
                        return;
                    }
                }
                if (m_growBoundaryTension) growBoundaryTensionMark();
            }
            BestFitForceDensitySettings settings;
            settings.maximumIterations = std::max(1, int(std::lround(m_fitIterations)));
            settings.designAttraction = m_attraction;
            settings.loads.nodalLoad = m_verticalLoad;
            settings.loads.density = m_density;
            settings.loads.additionalFixedVertices = m_verticalOnlySupports;
            settings.loads.unloadedFaces = m_result.exteriorFormFaces;
            settings.allowSlidingTensionSupports = m_allowSlidingTensionSupports;
            settings.slidingSupportPenalty = m_slidingPenalty;
            if (m_zeroGrowthDensity && m_growBoundaryTension) {
                settings.nearZeroEdges = m_growthEdgeIndices;
            }
            m_solver.reset(m_result, settings);
            m_status = m_solver.diagnostic();
            sync();
        } catch (const std::exception& error) {
            m_inputMesh.reset(); m_status = std::string("Failed to load tna.obj: ") + error.what();
        }
    }

    bool growBoundaryTensionMark() {
        m_growthEdgeIndices.clear();
        std::vector<bool> tensionVertices(m_result.mesh.vertices.size(), false);
        std::vector<bool> supports(m_result.mesh.vertices.size(), false);
        for (const int vertex : m_result.supportVertices) {
            if (vertex >= 0 && vertex < static_cast<int>(supports.size())) supports[vertex] = true;
        }
        // Growth must not add tension edges incident to red (fixed Z) supports either.
        for (const int vertex : m_verticalOnlySupports) {
            if (vertex >= 0 && vertex < static_cast<int>(supports.size())) supports[vertex] = true;
        }
        for (const TnaEdge& edge : m_result.activeFormEdges) {
            if (!edge.isTension) continue;
            if (edge.vertexA >= 0 && edge.vertexA < static_cast<int>(tensionVertices.size())) {
                tensionVertices[edge.vertexA] = true;
            }
            if (edge.vertexB >= 0 && edge.vertexB < static_cast<int>(tensionVertices.size())) {
                tensionVertices[edge.vertexB] = true;
            }
        }

        std::vector<TnaEdge> growthEdges;
        for (int i = 0; i < static_cast<int>(m_result.activeFormEdges.size()); ++i) {
            const TnaEdge& edge = m_result.activeFormEdges[i];
            if (edge.isTension || edge.vertexA < 0 || edge.vertexB < 0 ||
                edge.vertexA >= static_cast<int>(tensionVertices.size()) ||
                edge.vertexB >= static_cast<int>(tensionVertices.size())) continue;
            if (supports[edge.vertexA] || supports[edge.vertexB]) continue;
            if (tensionVertices[edge.vertexA] || tensionVertices[edge.vertexB]) {
                growthEdges.push_back(edge);
                m_growthEdgeIndices.push_back(i);
            }
        }
        return TnaSolver{}.setTensionEdges(m_result, growthEdges);
    }

    void exportFormFoundMesh() {
        if (!m_formMesh || m_formMesh->faces.empty() ||
            (!m_solver.exportable() && !m_solver.fitResult().success)) {
            m_status = "Finish a valid equilibrium stage before exporting";
            return;
        }

        // The form diagram includes exterior n-gons solely to complete the
        // topology needed for the reciprocal force diagram. They are not part
        // of the physical form-found surface, so omit them from the OBJ and mesh JSON.
        std::vector<bool> exteriorFaces(m_formMesh->faces.size(), false);
        for (const int face : m_result.exteriorFormFaces) {
            if (face >= 0 && face < static_cast<int>(exteriorFaces.size())) {
                exteriorFaces[face] = true;
            }
        }
        MeshData exportMesh = *m_formMesh;
        exportMesh.faces.clear();
        exportMesh.faces.reserve(m_formMesh->faces.size());
        for (int face = 0; face < static_cast<int>(m_formMesh->faces.size()); ++face) {
            if (!exteriorFaces[face]) exportMesh.faces.push_back(m_formMesh->faces[face]);
        }
        exportMesh.edges.clear();
        exportMesh.calculateNormals();
        exportMesh.triangulationDirty = true;

        const auto objPath = dataPath("bestfit_tna_formfound.obj");
        MeshObject output("bestfit_tna_formfound");
        output.setMeshData(std::make_shared<MeshData>(exportMesh));
        output.writeToObj(objPath.string());

        // Extract unique undirected edges from clean surface mesh faces
        std::set<std::pair<int, int>> uniqueEdges;
        for (const auto& face : exportMesh.faces) {
            const int k = static_cast<int>(face.vertices.size());
            for (int i = 0; i < k; ++i) {
                int u = face.vertices[i];
                int v = face.vertices[(i + 1) % k];
                if (u > v) std::swap(u, v);
                uniqueEdges.emplace(u, v);
            }
        }

        using json = nlohmann::json;

        // 1. Mesh JSON (V, E, F)
        json meshJson;
        meshJson["V"] = json::array();
        for (const auto& vertex : exportMesh.vertices) {
            meshJson["V"].push_back({vertex.position.x, vertex.position.y, vertex.position.z});
        }
        meshJson["E"] = json::array();
        for (const auto& edge : uniqueEdges) {
            meshJson["E"].push_back({edge.first, edge.second});
        }
        meshJson["F"] = json::array();
        for (const auto& face : exportMesh.faces) {
            meshJson["F"].push_back(face.vertices);
        }

        const auto exportDir = objPath.parent_path();
        const auto meshJsonPath = exportDir / "bestfit_tna_mesh.json";
        {
            std::ofstream meshFile(meshJsonPath);
            if (meshFile.is_open()) {
                meshFile << meshJson.dump(2);
            }
        }

        // 2. Diagrams JSON: form & force diagrams, constraints regions, resolved densities, residual angles
        const auto sliding = slidingSupports(m_result.supportVertices, m_solver.edges(),
                                             static_cast<int>(m_formMesh->vertices.size()),
                                             m_allowSlidingTensionSupports);
        std::vector<bool> isSliding(m_formMesh->vertices.size(), false);
        for (int v : sliding) if (v >= 0 && v < static_cast<int>(isSliding.size())) isSliding[v] = true;

        std::vector<int> fixedSupports;
        for (int v : m_result.supportVertices) {
            if (!isSliding[v]) fixedSupports.push_back(v);
        }

        auto safeFloat = [](float val) -> json {
            if (!std::isfinite(val)) return nullptr;
            return val;
        };

        const auto& densities = m_solver.densities();
        const auto& edges = m_solver.edges();
        const auto& force = m_solver.forceDiagram();

        json diagramsJson;

        // Constraint regions
        json constraintsJson;
        constraintsJson["fixed_supports"] = fixedSupports;
        constraintsJson["sliding_supports"] = sliding;
        constraintsJson["vertical_supports"] = m_verticalOnlySupports;
        diagramsJson["constraints"] = constraintsJson;
        diagramsJson["fixed_supports"] = fixedSupports;
        diagramsJson["sliding_supports"] = sliding;
        diagramsJson["vertical_supports"] = m_verticalOnlySupports;

        // Support reaction forces for all support types (fixed, sliding, vertical)
        json supportForcesJson = json::array();
        auto addSupportForce = [&](int v, const std::string& type) {
            Vec3 f = supportReactionForce(v);
            json s;
            s["vertex"] = v;
            s["type"] = type;
            s["force"] = {safeFloat(f.x), safeFloat(f.y), safeFloat(f.z)};
            s["magnitude"] = safeFloat(f.length());
            supportForcesJson.push_back(s);
        };
        for (int v : fixedSupports) addSupportForce(v, "fixed");
        for (int v : sliding) addSupportForce(v, "sliding");
        for (int v : m_verticalOnlySupports) addSupportForce(v, "vertical");
        diagramsJson["support_forces"] = supportForcesJson;

        // Resolved force densities
        json resolvedDensitiesJson = json::array();
        for (float q : densities) resolvedDensitiesJson.push_back(safeFloat(q));
        diagramsJson["resolved_force_densities"] = resolvedDensitiesJson;

        // Residual angles
        json residualAnglesJson = json::array();
        for (float angle : force.edgeDirectionDeviationsDegrees) residualAnglesJson.push_back(safeFloat(angle));
        diagramsJson["residual_angles"] = residualAnglesJson;
        diagramsJson["residual_angles_degrees"] = residualAnglesJson;

        // Form diagram
        json formDiagramJson;
        formDiagramJson["vertices"] = json::array();
        for (const auto& v : m_formMesh->vertices) {
            formDiagramJson["vertices"].push_back({v.position.x, v.position.y, v.position.z});
        }
        formDiagramJson["edges"] = json::array();
        for (std::size_t i = 0; i < edges.size(); ++i) {
            json eJson;
            eJson["index"] = static_cast<int>(i);
            eJson["u"] = edges[i].vertexA;
            eJson["v"] = edges[i].vertexB;
            eJson["is_tension"] = edges[i].isTension;
            eJson["force_density"] = i < densities.size() ? safeFloat(densities[i]) : nullptr;
            formDiagramJson["edges"].push_back(eJson);
        }
        formDiagramJson["active_edges"] = json::array();
        for (const auto& e : edges) {
            formDiagramJson["active_edges"].push_back({e.vertexA, e.vertexB});
        }
        formDiagramJson["exterior_faces"] = m_result.exteriorFormFaces;
        formDiagramJson["resolved_force_densities"] = resolvedDensitiesJson;
        diagramsJson["form_diagram"] = formDiagramJson;

        // Force diagram
        json forceDiagramJson;
        forceDiagramJson["success"] = force.success;
        forceDiagramJson["vertices"] = json::array();
        forceDiagramJson["edges"] = json::array();
        forceDiagramJson["reciprocal_form_edges"] = json::array();
        forceDiagramJson["reciprocal_angles_degrees"] = json::array();
        forceDiagramJson["residual_angles_degrees"] = residualAnglesJson;

        if (force.success) {
            for (const auto& v : force.mesh.vertices) {
                forceDiagramJson["vertices"].push_back({v.position.x, v.position.y, v.position.z});
            }
            for (const auto& e : force.mesh.edges) {
                forceDiagramJson["edges"].push_back({e.vertexA, e.vertexB});
            }
            for (const auto& e : force.reciprocalFormEdges) {
                forceDiagramJson["reciprocal_form_edges"].push_back({e.vertexA, e.vertexB});
            }
            for (float angle : force.edgeAnglesDegrees) {
                forceDiagramJson["reciprocal_angles_degrees"].push_back(safeFloat(angle));
            }
        }
        diagramsJson["force_diagram"] = forceDiagramJson;

        // Equilibrium report metrics
        const auto& report = m_solver.report();
        if (report.finite) {
            json eqJson;
            eqJson["residual"] = safeFloat(static_cast<float>(report.residual));
            eqJson["horizontal_residual"] = safeFloat(static_cast<float>(report.horizontalResidual));
            eqJson["vertical_residual"] = safeFloat(static_cast<float>(report.verticalResidual));
            eqJson["rms_design_distance"] = safeFloat(static_cast<float>(report.rmsDesignDistance));
            eqJson["max_design_distance"] = safeFloat(static_cast<float>(report.maximumDesignDistance));
            diagramsJson["equilibrium"] = eqJson;
        }

        const auto diagramsJsonPath = exportDir / "bestfit_tna_diagrams.json";
        {
            std::ofstream diagFile(diagramsJsonPath);
            if (diagFile.is_open()) {
                diagFile << diagramsJson.dump(2);
            }
        }

        m_status = "Exported OBJ, bestfit_tna_mesh.json, and bestfit_tna_diagrams.json";
    }

    static std::vector<int> blackVertexSupports(const MeshData& mesh) {
        constexpr float blackThreshold = 0.02f;
        std::vector<int> supports;
        for (int vertex = 0; vertex < static_cast<int>(mesh.vertices.size()); ++vertex) {
            const Color& color = mesh.vertices[vertex].color;
            if (color.r <= blackThreshold && color.g <= blackThreshold && color.b <= blackThreshold) {
                supports.push_back(vertex);
            }
        }
        return supports;
    }

    static std::vector<int> redVertexSupports(const MeshData& mesh) {
        constexpr float redMinimum = 0.8f;
        constexpr float otherMaximum = 0.2f;
        std::vector<int> supports;
        for (int vertex = 0; vertex < static_cast<int>(mesh.vertices.size()); ++vertex) {
            const Color& color = mesh.vertices[vertex].color;
            if (color.r >= redMinimum && color.g <= otherMaximum && color.b <= otherMaximum) {
                supports.push_back(vertex);
            }
        }
        return supports;
    }

    static void drawMesh(Renderer& renderer, const std::shared_ptr<MeshData>& mesh,
                         const Color& color, float width) {
        if (!mesh) return;
        std::vector<Vec3> segments;
        segments.reserve(mesh->edges.size() * 2);
        for (const MeshEdge& edge : mesh->edges) {
            if (edge.vertexA < 0 || edge.vertexB < 0 ||
                edge.vertexA >= static_cast<int>(mesh->vertices.size()) ||
                edge.vertexB >= static_cast<int>(mesh->vertices.size())) continue;
            segments.push_back(mesh->vertices[edge.vertexA].position);
            segments.push_back(mesh->vertices[edge.vertexB].position);
        }
        if (!segments.empty()) renderer.drawLines(segments.data(), static_cast<int>(segments.size()), color, width);
    }

    void drawFormDiagram(Renderer& renderer) const {
        if (!m_formMesh) return;
        // The appended exterior faces are topology required for the dual.
        // Their closing support chords are explicitly non-active and have no
        // reciprocal force edge, so do not render them as form members.
        std::vector<Vec3> segments;
        segments.reserve(m_result.activeFormEdges.size() * 2);
        std::vector<Vec3> tensionSegments;
        for (const TnaEdge& edge : m_result.activeFormEdges) {
            if (edge.vertexA < 0 || edge.vertexB < 0 ||
                edge.vertexA >= static_cast<int>(m_formMesh->vertices.size()) ||
                edge.vertexB >= static_cast<int>(m_formMesh->vertices.size())) continue;
            auto& selectedSegments = edge.isTension ? tensionSegments : segments;
            selectedSegments.push_back(m_formMesh->vertices[edge.vertexA].position);
            selectedSegments.push_back(m_formMesh->vertices[edge.vertexB].position);
        }
        if (!segments.empty()) {
            renderer.drawLines(segments.data(), static_cast<int>(segments.size()),
                               Color(0.05f, 0.32f, 0.82f, 1.0f), 1.8f);
        }
        if (!tensionSegments.empty()) {
            renderer.drawLines(tensionSegments.data(), static_cast<int>(tensionSegments.size()),
                               Color(0.85f, 0.08f, 0.35f, 1.0f), 3.0f);
        }
    }

    Vec3 supportReactionForce(int vertex) const {
        if (!m_formMesh || vertex < 0 || vertex >= static_cast<int>(m_formMesh->vertices.size())) {
            return Vec3(0.0f, 0.0f, 0.0f);
        }
        const auto& report = m_solver.report();
        if (report.finite && vertex < static_cast<int>(report.reactions.size())) {
            const Vec3& r = report.reactions[vertex];
            if (std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.z) &&
                (r.x != 0.0f || r.y != 0.0f || r.z != 0.0f)) {
                return r;
            }
        }
        const auto& densities = m_solver.densities();
        const auto& edges = m_solver.edges();
        if (densities.empty() || edges.empty()) return Vec3(0.0f, 0.0f, 0.0f);
        Vec3 force(0.0f, 0.0f, 0.0f);
        const Vec3& p = m_formMesh->vertices[vertex].position;
        for (std::size_t i = 0; i < edges.size() && i < densities.size(); ++i) {
            const auto& edge = edges[i];
            const float q = densities[i];
            if (!std::isfinite(q)) continue;
            if (edge.vertexA == vertex && edge.vertexB >= 0 &&
                edge.vertexB < static_cast<int>(m_formMesh->vertices.size())) {
                force += (p - m_formMesh->vertices[edge.vertexB].position) * q;
            } else if (edge.vertexB == vertex && edge.vertexA >= 0 &&
                       edge.vertexA < static_cast<int>(m_formMesh->vertices.size())) {
                force += (p - m_formMesh->vertices[edge.vertexA].position) * q;
            }
        }
        return force;
    }

    void drawSupportForces(Renderer& renderer) const {
        if (!m_formMesh || m_supportForceScale <= 0.0f) return;
        const Color greenColor(0.10f, 0.82f, 0.22f, 1.0f);
        std::vector<int> allSupports = m_result.supportVertices;
        for (const int v : m_verticalOnlySupports) {
            if (std::find(allSupports.begin(), allSupports.end(), v) == allSupports.end()) {
                allSupports.push_back(v);
            }
        }
        for (const int vertex : allSupports) {
            if (vertex < 0 || vertex >= static_cast<int>(m_formMesh->vertices.size())) continue;
            const Vec3 p = m_formMesh->vertices[vertex].position;
            const Vec3 force = supportReactionForce(vertex);
            const float forceMagnitude = force.length();
            if (!std::isfinite(forceMagnitude) || forceMagnitude < 1e-4f) continue;

            const Vec3 pEnd = p + force * m_supportForceScale;
            renderer.drawLine(p, pEnd, greenColor, 2.5f);

            // Draw arrowhead at pEnd pointing in force direction
            const Vec3 dir = force / forceMagnitude;
            Vec3 up(0.0f, 0.0f, 1.0f);
            if (std::abs(dir.z) > 0.9f) up = Vec3(1.0f, 0.0f, 0.0f);
            Vec3 side = dir.cross(up).normalized();
            Vec3 normal = dir.cross(side).normalized();

            const float arrowLength = std::min(0.25f * forceMagnitude * m_supportForceScale, 0.25f);
            const float arrowWidth = arrowLength * 0.4f;
            const Vec3 arrowBase = pEnd - dir * arrowLength;

            renderer.drawLine(pEnd, arrowBase + side * arrowWidth, greenColor, 2.0f);
            renderer.drawLine(pEnd, arrowBase - side * arrowWidth, greenColor, 2.0f);
            renderer.drawLine(pEnd, arrowBase + normal * arrowWidth, greenColor, 2.0f);
            renderer.drawLine(pEnd, arrowBase - normal * arrowWidth, greenColor, 2.0f);
            renderer.drawPoint(pEnd, greenColor, 4.0f);

            if (m_showForceDensities) {
                std::ostringstream mag;
                mag << std::fixed << std::setprecision(2) << forceMagnitude;
                renderer.setColor(greenColor);
                renderer.drawText(mag.str(), pEnd + Vec3(0.0f, 0.0f, 0.01f), 1.0f);
            }
        }
    }

    void drawSupports(Renderer& renderer) const {
        if (!m_formMesh) return;
        drawSupportForces(renderer);
        for (const int vertex : m_result.supportVertices) {
            if (vertex < 0 || vertex >= static_cast<int>(m_formMesh->vertices.size())) continue;
            renderer.drawPoint(m_formMesh->vertices[vertex].position, Color(0.0f, 0.0f, 0.0f, 1.0f), 8.0f);
        }
        for (const int vertex : m_verticalOnlySupports) {
            if (vertex < 0 || vertex >= static_cast<int>(m_formMesh->vertices.size())) continue;
            renderer.drawPoint(m_formMesh->vertices[vertex].position, Color(0.9f, 0.05f, 0.05f, 1.0f), 8.0f);
        }
    }

    void drawForceDensities(Renderer& renderer) const {
        if (!m_showForceDensities || !m_formMesh) return;
        const auto& densities = m_solver.densities();
        const auto& edges = m_solver.edges();
        for (int edgeIndex = 0;
             edgeIndex < static_cast<int>(edges.size()) &&
             edgeIndex < static_cast<int>(densities.size()); ++edgeIndex) {
            const TnaEdge& edge = edges[edgeIndex];
            if (edge.vertexA < 0 || edge.vertexB < 0 ||
                edge.vertexA >= static_cast<int>(m_formMesh->vertices.size()) ||
                edge.vertexB >= static_cast<int>(m_formMesh->vertices.size())) continue;
            const float density = densities[edgeIndex];
            if (!std::isfinite(density)) continue;
            const Vec3 midpoint = (m_formMesh->vertices[edge.vertexA].position +
                                   m_formMesh->vertices[edge.vertexB].position) * 0.5f;
            std::ostringstream label;
            label << std::fixed << std::setprecision(3) << density;
            renderer.setColor(density < 0.0f
                                  ? Color(0.85f, 0.08f, 0.35f, 1.0f)
                                  : Color(0.05f, 0.32f, 0.82f, 1.0f));
            renderer.drawText(label.str(), midpoint + Vec3(0.0f, 0.0f, 0.001f), 1.00f);
        }
    }

    float forceDiagramScale() const {
        if (!m_formMesh || !m_forceMesh || m_formMesh->vertices.empty() || m_forceMesh->vertices.empty()) return 1;
        Vec3 formMin, formMax, forceMin, forceMax;
        m_formMesh->updateBounds(formMin, formMax); m_forceMesh->updateBounds(forceMin, forceMax);
        const float formSize = std::max(formMax.x - formMin.x, formMax.y - formMin.y);
        const float forceSize = std::max(forceMax.x - forceMin.x, forceMax.y - forceMin.y);
        return forceSize > 1e-6f ? std::max(1e-6f, formSize) / forceSize : 1.0f;
    }

    Vec3 forceDiagramOffset() const {
        if (!m_formMesh || !m_forceMesh || m_formMesh->vertices.empty() || m_forceMesh->vertices.empty()) {
            return Vec3(0.0f, 0.0f, 0.0f);
        }
        Vec3 formMin, formMax, forceMin, forceMax;
        m_formMesh->updateBounds(formMin, formMax); m_forceMesh->updateBounds(forceMin, forceMax);
        const float scale = forceDiagramScale();
        const float gap = std::max(0.05f, 0.12f * (formMax.x - formMin.x));
        return Vec3(formMax.x + gap - forceMin.x * scale,
                    (formMin.y + formMax.y - (forceMin.y + forceMax.y) * scale) * 0.5f, 0);
    }

    static Color blend(const Color& first, const Color& second, float amount) {
        amount = std::clamp(amount, 0.0f, 1.0f);
        return Color(first.r + (second.r - first.r) * amount,
                     first.g + (second.g - first.g) * amount,
                     first.b + (second.b - first.b) * amount,
                     1.0f);
    }

    static Color forceAngleColor(float angleDegrees) {
        // Angle error from reciprocity: 0 -> blue, 30 -> cyan, 60 -> yellow,
        // and 90 -> red.
        const float t = std::clamp(angleDegrees / 90.0f, 0.0f, 1.0f);
        if (t < 1.0f / 3.0f) {
            return blend(Color(0.10f, 0.18f, 0.95f, 1.0f),
                         Color(0.00f, 0.85f, 0.90f, 1.0f), t * 3.0f);
        }
        if (t < 2.0f / 3.0f) {
            return blend(Color(0.00f, 0.85f, 0.90f, 1.0f),
                         Color(1.00f, 0.84f, 0.02f, 1.0f), (t - 1.0f / 3.0f) * 3.0f);
        }
        return blend(Color(1.00f, 0.84f, 0.02f, 1.0f),
                     Color(0.90f, 0.06f, 0.03f, 1.0f), (t - 2.0f / 3.0f) * 3.0f);
    }

    const std::vector<float>& forceAngles() const {
        return m_forceResult.edgeDirectionDeviationsDegrees;
    }

    void drawForceDiagram(Renderer& renderer) const {
        if (!m_forceMesh) return;
        const Vec3 offset = forceDiagramOffset();
        const float scale = forceDiagramScale();
        const std::vector<float>& angles = forceAngles();
        for (int edgeIndex = 0; edgeIndex < static_cast<int>(m_forceMesh->edges.size()); ++edgeIndex) {
            const MeshEdge& edge = m_forceMesh->edges[edgeIndex];
            if (edge.vertexA < 0 || edge.vertexB < 0 ||
                edge.vertexA >= static_cast<int>(m_forceMesh->vertices.size()) ||
                edge.vertexB >= static_cast<int>(m_forceMesh->vertices.size())) continue;
            const Color color = edgeIndex < static_cast<int>(angles.size())
                                    ? forceAngleColor(angles[edgeIndex])
                                    : Color(0.40f, 0.40f, 0.40f, 1.0f);
            renderer.drawLine(m_forceMesh->vertices[edge.vertexA].position * scale + offset,
                              m_forceMesh->vertices[edge.vertexB].position * scale + offset,
                              color, 1.8f);
        }

        if (!m_showForceAngles) return;
        for (int edgeIndex = 0; edgeIndex < static_cast<int>(m_forceMesh->edges.size()) &&
                                edgeIndex < static_cast<int>(angles.size()); ++edgeIndex) {
            const MeshEdge& edge = m_forceMesh->edges[edgeIndex];
            if (edge.vertexA < 0 || edge.vertexB < 0 ||
                edge.vertexA >= static_cast<int>(m_forceMesh->vertices.size()) ||
                edge.vertexB >= static_cast<int>(m_forceMesh->vertices.size())) continue;
            const Vec3 midpoint = (m_forceMesh->vertices[edge.vertexA].position +
                                   m_forceMesh->vertices[edge.vertexB].position) * (0.5f * scale) + offset;
            std::ostringstream angle;
            const float deviation = angles[edgeIndex];
            if (std::isfinite(deviation)) angle << std::fixed << std::setprecision(1) << deviation;
            else angle << "invalid";
            renderer.setColor(forceAngleColor(deviation));
            renderer.drawText(angle.str(), midpoint + Vec3(0.0f, 0.0f, 0.001f), 1.00f);
        }
    }

    std::shared_ptr<MeshObject> m_inputObject;
    std::shared_ptr<MeshData> m_inputMesh, m_formMesh, m_forceMesh;
    std::unique_ptr<SimpleUI> m_ui;
    BestFitTnaSolver m_solver;
    BestFitTnaWorker m_fitWorker;
    TnaFormDiagram m_result;
    TnaForceDiagram m_forceResult;
    std::vector<int> m_verticalOnlySupports;
    std::string m_status;
    float m_fitIterations{50}, m_attraction{1}, m_horizontalIterations{500}, m_angleTolerance{3};
    float m_verticalLoad{0.001f}, m_density{1}, m_tensionDistance{0};
    std::array<float, 5> m_appliedFitControls{};
    std::array<float, 3> m_appliedTnaControls{};
    bool m_tensionEnabled{true}, m_allBoundaryTension{false}, m_growBoundaryTension{false};
    bool m_zeroGrowthDensity{false};
    bool m_allowSlidingTensionSupports{true};
    float m_slidingPenalty{50.0f};
    std::vector<int> m_growthEdgeIndices;
    float m_supportForceScale{0.4f};
    bool m_showInput{true}, m_showForceAngles{false}, m_showForceDensities{false};
};

ALICE2_REGISTER_SKETCH_AUTO(BestFitTnaSketch)

#define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>
#include <tna/TnaSolver.h>

#include <filesystem>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace alice2;

class TnaSketch : public ISketch {
public:
    std::string getName() const override { return "TNA Horizontal Equilibrium"; }
    std::string getDescription() const override { return "Stages 1-3: form, force dual, and reciprocal relaxation"; }
    std::string getAuthor() const override { return "alice2 User"; }

    void setup() override {
        scene().setBackgroundColor(Color(0.96f, 0.96f, 0.96f, 1.0f));
        scene().setShowGrid(false);
        scene().setShowAxes(false);
        scene().setAxesLength(1.2f);
        m_ui = std::make_unique<SimpleUI>(input());
        m_ui->setTheme(SimpleUI::UITheme::Dark);
        m_ui->addSlider("Form weight", Vec2{10.0f, 168.0f}, 190.0f, 0.0f, 1.0f, m_formWeight);
        m_ui->addSlider("H max iterations", Vec2{10.0f, 196.0f}, 190.0f, 1.0f, 500.0f, m_horizontalMaximumIterations);
        m_ui->addSlider("H target angle", Vec2{10.0f, 224.0f}, 190.0f, 0.0f, 10.0f, m_horizontalTargetAngle);
        m_ui->addSlider("V nodal load", Vec2{10.0f, 252.0f}, 190.0f, -0.01f, 0.01f, m_verticalLoad);
        m_ui->addSlider("V self-weight density", Vec2{10.0f, 280.0f}, 190.0f, 0.0f, 10.0f, m_selfWeightDensity);
        m_ui->addSlider("Tension force vertex distance", Vec2{10.0f, 308.0f}, 220.0f,
                        -1.0f, 10.0f, m_tensionForceVertexDistance);
        reload();
    }

    void update(float) override {
        if (std::abs(m_tensionForceVertexDistance - m_appliedTensionForceVertexDistance) > 1e-6f) {
            applyTensionForceVertexDistance(true);
        }
        if (!m_horizontalRunning) return;
        const TnaHorizontalSettings settings = horizontalSettings();
        for (int step = 0; step < m_horizontalStepsPerFrame &&
                           !m_solver.horizontalEquilibrium().converged; ++step) {
            m_solver.stepHorizontalEquilibrium(settings);
        }
        const TnaHorizontalEquilibrium& horizontal = m_solver.horizontalEquilibrium();
        if (m_formMesh) *m_formMesh = horizontal.formDiagram;
        if (m_forceMesh) *m_forceMesh = horizontal.forceDiagram;
        m_status = horizontal.diagnostic;
        if (horizontal.converged) m_horizontalRunning = false;
    }

    void draw(Renderer& renderer, Camera&) override {
        if (m_showInput) drawMesh(renderer, m_inputMesh, Color(0.72f, 0.72f, 0.72f, 1.0f), 0.8f);
        drawFormDiagram(renderer);
        drawForceDensities(renderer);
        drawForceDiagram(renderer);
        drawSupports(renderer);

        renderer.setColor(Color(0.06f, 0.06f, 0.06f, 1.0f));
        renderer.drawString("r reload OBJ | h horizontal | v vertical | e export OBJ | i toggle original | a toggle angles | d toggle densities | g grow tension", 10.0f, 30.0f);
        renderer.drawString(m_status, 10.0f, 52.0f);
        renderer.setColor(Color(0.85f, 0.08f, 0.35f, 1.0f));
        renderer.drawString("Form: pink = tension | blue = compression | black = fixed XY/Z | red = fixed Z only", 10.0f, 74.0f);
        renderer.setColor(Color(0.06f, 0.06f, 0.06f, 1.0f));
        renderer.drawString(std::string("t toggle boundary tension: ") +
                                (m_tensionEnabled ? "ON" : "OFF") + " (reloads form)",
                            10.0f, 96.0f);
        renderer.drawString(std::string("b tension boundary scope: ") +
                                (m_allBoundaryTension ? "ALL" : "OUTER") + " (reloads form)",
                            10.0f, 118.0f);
        renderer.drawString(std::string("g grow tension from boundary: ") +
                                (m_growBoundaryTension ? "ON" : "OFF") + " (reloads form)",
                            10.0f, 136.0f);
        if (m_ui) m_ui->draw(renderer);
    }

    bool onKeyPress(unsigned char key, int, int) override {
        switch (key) {
            case 'r': case 'R': reload(); return true;
            case 'h': case 'H': startHorizontalEquilibrium(); return true;
            case 'v': case 'V': startVerticalEquilibrium(); return true;
            case 'e': case 'E': exportFormFoundMesh(); return true;
            case 'i': case 'I': m_showInput = !m_showInput; return true;
            case 'a': case 'A': m_showForceAngles = !m_showForceAngles; return true;
            case 'd': case 'D': m_showForceDensities = !m_showForceDensities; return true;
            case 'b': case 'B': m_allBoundaryTension = !m_allBoundaryTension; reload(); return true;
            case 'g': case 'G': m_growBoundaryTension = !m_growBoundaryTension; reload(); return true;
            case 't': case 'T': m_tensionEnabled = !m_tensionEnabled; reload(); return true;
            default: return false;
        }
    }

    bool onMousePress(int button, int state, int x, int y) override {
        return m_ui && m_ui->onMousePress(button, state, x, y);
    }

    bool onMouseMove(int x, int y) override {
        return m_ui && m_ui->onMouseMove(x, y);
    }

private:
    static std::filesystem::path dataPath(const std::string& file) {
        const std::filesystem::path requested(file);
        if (requested.is_absolute() || std::filesystem::exists(requested)) return requested;
        const std::filesystem::path local = std::filesystem::path("data") / file;
        if (std::filesystem::exists(local)) return local;
        return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "data" / file;
    }

    void reload() {
        m_horizontalRunning = false;
        m_solver = TnaSolver{};
        m_forceResult = {};
        clearTensionForceVertexOffsets();
        m_inputObject = std::make_shared<MeshObject>("tna_input");
        try {
            m_inputObject->readFromObj(dataPath(m_objPath).string());
            // Extended OBJ exports frequently duplicate face-corner vertices.
            // Welding restores the shared topology required for boundary walks.
            m_inputObject->weld(1e-3f);
            m_inputMesh = m_inputObject->getMeshData();
            m_verticalOnlySupports = redVertexSupports(*m_inputMesh);

            // The form diagram works in plan. Keep m_inputMesh untouched as
            // a grey reference and flatten only its duplicate used by TNA.
            MeshData planarInput = *m_inputMesh;
            for (MeshVertex& vertex : planarInput.vertices) vertex.position.z = 0.0f;
            planarInput.calculateNormals();
            planarInput.triangulationDirty = true;

            // Black vertex colours are explicit support tags. An untagged OBJ
            // deliberately has no supports unless this compatibility option
            // is explicitly enabled below.
            m_result = m_solver.makeFormDiagram(planarInput, blackVertexSupports(*m_inputMesh),
                                                 m_fallbackToAllBoundarySupports);
            // Boundary tension can be limited to the outer support loop or
            // applied to every boundary loop, including openings.
            if (m_result.success &&
                (!m_tensionEnabled ||
                 (m_allBoundaryTension
                      ? m_solver.setBoundaryEdgesTension(m_result)
                      : m_solver.setOuterBoundaryEdgesTension(m_result))) &&
                (!m_tensionEnabled || !m_growBoundaryTension || growBoundaryTensionMark())) {
                m_formMesh = std::make_shared<MeshData>(m_result.mesh);
                m_forceResult = m_solver.makeForceDiagram(m_result);
                if (m_forceResult.success) {
                    cacheTensionForceVertexOffsets();
                    applyTensionForceVertexDistance(false);
                    m_forceMesh = std::make_shared<MeshData>(m_forceResult.mesh);
                    m_initialFormMesh = std::make_shared<MeshData>(*m_formMesh);
                    m_status = m_result.diagnostic + " | " + m_forceResult.diagnostic;
                } else {
                    m_forceMesh.reset();
                    m_initialFormMesh.reset();
                    m_status = m_result.diagnostic + " | " + m_forceResult.diagnostic;
                }
            } else {
                m_formMesh.reset();
                m_forceMesh.reset();
                m_initialFormMesh.reset();
                m_status = m_result.diagnostic;
            }
        } catch (const std::exception& error) {
            m_inputMesh.reset();
            m_formMesh.reset();
            m_forceMesh.reset();
            m_initialFormMesh.reset();
            m_verticalOnlySupports.clear();
            m_status = std::string("Failed to load ") + m_objPath + ": " + error.what();
        }
    }

    void startHorizontalEquilibrium() {
        if (m_solver.horizontalEquilibrium().success) {
            // Keep the target field and positions, and append one batch of
            // iterations from the current state.
            m_horizontalRunning = m_solver.continueHorizontalEquilibrium(
                std::max(1, static_cast<int>(std::lround(m_horizontalMaximumIterations))));
            return;
        }
        if (!m_initialFormMesh || !m_forceResult.success) {
            m_status = "No valid form/force pair available for horizontal equilibrium";
            return;
        }
        if (!m_solver.resetHorizontalEquilibrium(*m_initialFormMesh, m_forceResult,
                                                  m_result.supportVertices)) {
            m_status = m_solver.horizontalEquilibrium().diagnostic;
            return;
        }
        const TnaHorizontalEquilibrium& horizontal = m_solver.horizontalEquilibrium();
        m_formMesh = std::make_shared<MeshData>(horizontal.formDiagram);
        m_forceMesh = std::make_shared<MeshData>(horizontal.forceDiagram);
        m_horizontalRunning = true;
        m_status = horizontal.diagnostic;
    }

    // Expand the currently marked boundary tension by exactly one topological
    // edge ring. Existing tension edges seed the band; any candidate touching
    // a black support is omitted so supports stay at the tension boundary.
    bool growBoundaryTensionMark() {
        std::vector<bool> tensionVertices(m_result.mesh.vertices.size(), false);
        std::vector<bool> supports(m_result.mesh.vertices.size(), false);
        for (const int vertex : m_result.supportVertices) {
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
        for (const TnaEdge& edge : m_result.activeFormEdges) {
            if (edge.isTension || edge.vertexA < 0 || edge.vertexB < 0 ||
                edge.vertexA >= static_cast<int>(tensionVertices.size()) ||
                edge.vertexB >= static_cast<int>(tensionVertices.size())) continue;
            if (supports[edge.vertexA] || supports[edge.vertexB]) continue;
            if (tensionVertices[edge.vertexA] || tensionVertices[edge.vertexB]) {
                growthEdges.push_back(edge);
            }
        }
        return m_solver.setTensionEdges(m_result, growthEdges);
    }

    // A force vertex is selected only when it has at least one incident
    // reciprocal edge and every one of those edges corresponds to tension in
    // the form diagram. Cache its original position and the unit vector from
    // the selected-set centroid once, so the slider is always pos + unit * d.
    void cacheTensionForceVertexOffsets() {
        clearTensionForceVertexOffsets();
        if (!m_forceResult.success || m_forceResult.mesh.vertices.empty()) return;

        const MeshData& force = m_forceResult.mesh;
        std::vector<int> valences(force.vertices.size(), 0);
        std::vector<bool> allIncidentEdgesTension(force.vertices.size(), true);
        for (int edgeIndex = 0; edgeIndex < static_cast<int>(force.edges.size()); ++edgeIndex) {
            const MeshEdge& edge = force.edges[edgeIndex];
            if (edge.vertexA < 0 || edge.vertexB < 0 ||
                edge.vertexA >= static_cast<int>(force.vertices.size()) ||
                edge.vertexB >= static_cast<int>(force.vertices.size())) continue;
            const bool isTension = edgeIndex < static_cast<int>(m_forceResult.reciprocalFormEdges.size()) &&
                                   m_forceResult.reciprocalFormEdges[edgeIndex].isTension;
            ++valences[edge.vertexA];
            ++valences[edge.vertexB];
            allIncidentEdgesTension[edge.vertexA] = allIncidentEdgesTension[edge.vertexA] && isTension;
            allIncidentEdgesTension[edge.vertexB] = allIncidentEdgesTension[edge.vertexB] && isTension;
        }

        Vec3 centroid(0.0f, 0.0f, 0.0f);
        for (int vertex = 0; vertex < static_cast<int>(force.vertices.size()); ++vertex) {
            if (valences[vertex] == 0 || !allIncidentEdgesTension[vertex]) continue;
            m_tensionForceVertexIds.push_back(vertex);
            centroid += force.vertices[vertex].position;
        }

        // These are geometric control vertices, not merely display edits.
        // resetHorizontalEquilibrium copies this list into its fixed-force
        // constraints, so parallelisation cannot move them.
        for (const int vertex : m_tensionForceVertexIds) {
            if (std::find(m_forceResult.fixedForceVertices.begin(),
                          m_forceResult.fixedForceVertices.end(), vertex) ==
                m_forceResult.fixedForceVertices.end()) {
                m_forceResult.fixedForceVertices.push_back(vertex);
            }
        }

        if (m_tensionForceVertexIds.empty()) return;
        centroid /= static_cast<float>(m_tensionForceVertexIds.size());
        m_tensionForceVertexPositions.reserve(m_tensionForceVertexIds.size());
        m_tensionForceVertexDirections.reserve(m_tensionForceVertexIds.size());
        for (const int vertex : m_tensionForceVertexIds) {
            const Vec3 position = force.vertices[vertex].position;
            m_tensionForceVertexPositions.push_back(position);
            m_tensionForceVertexDirections.push_back((position - centroid).normalized());
        }
    }

    void clearTensionForceVertexOffsets() {
        m_tensionForceVertexIds.clear();
        m_tensionForceVertexPositions.clear();
        m_tensionForceVertexDirections.clear();
        m_appliedTensionForceVertexDistance = std::numeric_limits<float>::infinity();
    }

    void applyTensionForceVertexDistance(bool invalidateEquilibrium) {
        if (!m_forceResult.success || m_tensionForceVertexIds.empty()) {
            m_appliedTensionForceVertexDistance = m_tensionForceVertexDistance;
            return;
        }
        MeshData& force = m_forceResult.mesh;
        for (int index = 0; index < static_cast<int>(m_tensionForceVertexIds.size()); ++index) {
            const int vertex = m_tensionForceVertexIds[index];
            if (vertex < 0 || vertex >= static_cast<int>(force.vertices.size())) continue;
            force.vertices[vertex].position = m_tensionForceVertexPositions[index] +
                                              m_tensionForceVertexDirections[index] *
                                                  m_tensionForceVertexDistance;
        }
        force.calculateNormals();
        force.triangulationDirty = true;
        m_appliedTensionForceVertexDistance = m_tensionForceVertexDistance;

        // The edited positions are the next horizontal solve's initial force
        // diagram. Discard an older solve rather than allowing v to use it.
        if (invalidateEquilibrium && m_solver.horizontalEquilibrium().success) {
            m_horizontalRunning = false;
            m_solver = TnaSolver{};
            if (m_initialFormMesh) m_formMesh = std::make_shared<MeshData>(*m_initialFormMesh);
            m_status = "Tension force-vertex distance changed; press h to solve horizontal equilibrium";
        }
        if (m_forceMesh) *m_forceMesh = force;
    }

    TnaHorizontalSettings horizontalSettings() const {
        TnaHorizontalSettings settings;
        settings.formWeight = m_formWeight;
        settings.angleToleranceDegrees = m_horizontalTargetAngle;
        settings.maximumIterations = std::max(1, static_cast<int>(std::lround(m_horizontalMaximumIterations)));
        settings.forceScale = m_horizontalForceScale;

        // A zero ratio leaves that side unbounded. Non-zero values generate
        // COMPAS-style per-edge bounds from the initial plan/force lengths.
        const bool hasLengthLimits = m_formEdgeMinimumRatio > 0.0f || m_formEdgeMaximumRatio > 0.0f ||
                                     m_forceEdgeMinimumRatio > 0.0f || m_forceEdgeMaximumRatio > 0.0f;
        const bool hasHorizontalForceLimits = m_horizontalForceMinimum > 0.0f ||
                                              m_horizontalForceMaximum < 1e7f;
        if (m_initialFormMesh && m_forceResult.success && (hasLengthLimits || hasHorizontalForceLimits)) {
            settings.edgeConstraints.resize(m_forceResult.reciprocalFormEdges.size());
            for (int edgeIndex = 0; edgeIndex < static_cast<int>(settings.edgeConstraints.size()); ++edgeIndex) {
                const TnaEdge& edge = m_forceResult.reciprocalFormEdges[edgeIndex];
                if (edge.vertexA < 0 || edge.vertexB < 0 ||
                    edge.vertexA >= static_cast<int>(m_initialFormMesh->vertices.size()) ||
                    edge.vertexB >= static_cast<int>(m_initialFormMesh->vertices.size())) continue;
                const float initialLength = (m_initialFormMesh->vertices[edge.vertexB].position -
                                             m_initialFormMesh->vertices[edge.vertexA].position).length();
                TnaHorizontalSettings::EdgeConstraint& constraint = settings.edgeConstraints[edgeIndex];
                if (m_formEdgeMinimumRatio > 0.0f) {
                    constraint.formLengthMinimum = initialLength * m_formEdgeMinimumRatio;
                }
                if (m_formEdgeMaximumRatio > 0.0f) {
                    constraint.formLengthMaximum = initialLength * m_formEdgeMaximumRatio;
                }
                if (edgeIndex < static_cast<int>(m_forceResult.mesh.edges.size())) {
                    const MeshEdge& forceEdge = m_forceResult.mesh.edges[edgeIndex];
                    if (forceEdge.vertexA >= 0 && forceEdge.vertexB >= 0 &&
                        forceEdge.vertexA < static_cast<int>(m_forceResult.mesh.vertices.size()) &&
                        forceEdge.vertexB < static_cast<int>(m_forceResult.mesh.vertices.size())) {
                        const float initialForceLength =
                            (m_forceResult.mesh.vertices[forceEdge.vertexB].position -
                             m_forceResult.mesh.vertices[forceEdge.vertexA].position).length();
                        if (m_forceEdgeMinimumRatio > 0.0f) {
                            constraint.forceLengthMinimum = initialForceLength * m_forceEdgeMinimumRatio;
                        }
                        if (m_forceEdgeMaximumRatio > 0.0f) {
                            constraint.forceLengthMaximum = initialForceLength * m_forceEdgeMaximumRatio;
                        }
                    }
                }
                constraint.horizontalForceMinimum = m_horizontalForceMinimum;
                constraint.horizontalForceMaximum = m_horizontalForceMaximum;
            }
        }
        return settings;
    }

    void startVerticalEquilibrium() {
        if (m_horizontalRunning || !m_inputMesh) {
            m_status = "Finish horizontal equilibrium before starting vertical equilibrium";
            return;
        }
        TnaVerticalSettings settings;
        settings.nodalLoad = m_verticalLoad;
        settings.density = m_selfWeightDensity;
        settings.thickness = m_verticalThickness;
        settings.forceScale = m_verticalForceScale;
        settings.residualTolerance = m_verticalResidualTolerance;
        settings.maximumIterations = std::max(1, static_cast<int>(std::lround(m_verticalMaximumIterations)));
        settings.supportHeights.reserve(m_inputMesh->vertices.size());
        for (const MeshVertex& vertex : m_inputMesh->vertices) {
            settings.supportHeights.push_back(vertex.position.z);
        }
        settings.additionalFixedVertices = m_verticalOnlySupports;
        settings.unloadedFaces = m_result.exteriorFormFaces;

        if (!m_solver.solveVerticalEquilibrium(settings) || !m_solver.verticalEquilibrium().converged) {
            m_status = m_solver.verticalEquilibrium().diagnostic;
            return;
        }
        const TnaVerticalEquilibrium& vertical = m_solver.verticalEquilibrium();
        m_formMesh = std::make_shared<MeshData>(vertical.formDiagram);
        m_status = vertical.diagnostic;
    }

    void exportFormFoundMesh() {
        if (!m_formMesh || m_formMesh->faces.empty()) {
            m_status = "No form-found mesh available to export";
            return;
        }

        // The form diagram includes exterior n-gons solely to complete the
        // topology needed for the reciprocal force diagram. They are not part
        // of the physical form-found surface, so omit them from the OBJ.
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

        MeshObject output("tna_formfound");
        output.setMeshData(std::make_shared<MeshData>(std::move(exportMesh)));
        output.writeToObj(dataPath("tna_formfound.obj").string());
        m_status = "Exported clean form-found mesh to data/tna_formfound.obj";
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

    void drawSupports(Renderer& renderer) const {
        if (!m_formMesh) return;
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
        const TnaHorizontalEquilibrium& horizontal = m_solver.horizontalEquilibrium();
        for (int edgeIndex = 0;
             edgeIndex < static_cast<int>(horizontal.reciprocalFormEdges.size()) &&
             edgeIndex < static_cast<int>(horizontal.forceDensities.size()); ++edgeIndex) {
            const TnaEdge& edge = horizontal.reciprocalFormEdges[edgeIndex];
            if (edge.vertexA < 0 || edge.vertexB < 0 ||
                edge.vertexA >= static_cast<int>(m_formMesh->vertices.size()) ||
                edge.vertexB >= static_cast<int>(m_formMesh->vertices.size())) continue;
            const float density = horizontal.forceDensities[edgeIndex];
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

    Vec3 forceDiagramOffset() const {
        if (!m_formMesh || !m_forceMesh || m_formMesh->vertices.empty() || m_forceMesh->vertices.empty()) {
            return Vec3(0.0f, 0.0f, 0.0f);
        }
        float formMinX = m_formMesh->vertices.front().position.x;
        float formMaxX = formMinX;
        float forceMinX = m_forceMesh->vertices.front().position.x;
        for (const MeshVertex& vertex : m_formMesh->vertices) {
            formMinX = std::min(formMinX, vertex.position.x);
            formMaxX = std::max(formMaxX, vertex.position.x);
        }
        for (const MeshVertex& vertex : m_forceMesh->vertices) {
            forceMinX = std::min(forceMinX, vertex.position.x);
        }
        const float gap = std::max(0.05f, 0.12f * (formMaxX - formMinX));
        return Vec3(formMaxX + gap - forceMinX, 0.0f, 0.0f);
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
        const TnaHorizontalEquilibrium& horizontal = m_solver.horizontalEquilibrium();
        return horizontal.success ? horizontal.edgeDirectionDeviationsDegrees
                                  : m_forceResult.edgeDirectionDeviationsDegrees;
    }

    void drawForceDiagram(Renderer& renderer) const {
        if (!m_forceMesh) return;
        const Vec3 offset = forceDiagramOffset();
        const std::vector<float>& angles = forceAngles();
        for (int edgeIndex = 0; edgeIndex < static_cast<int>(m_forceMesh->edges.size()); ++edgeIndex) {
            const MeshEdge& edge = m_forceMesh->edges[edgeIndex];
            if (edge.vertexA < 0 || edge.vertexB < 0 ||
                edge.vertexA >= static_cast<int>(m_forceMesh->vertices.size()) ||
                edge.vertexB >= static_cast<int>(m_forceMesh->vertices.size())) continue;
            const Color color = edgeIndex < static_cast<int>(angles.size())
                                    ? forceAngleColor(angles[edgeIndex])
                                    : Color(0.40f, 0.40f, 0.40f, 1.0f);
            renderer.drawLine(m_forceMesh->vertices[edge.vertexA].position + offset,
                              m_forceMesh->vertices[edge.vertexB].position + offset,
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
                                   m_forceMesh->vertices[edge.vertexB].position) * 0.5f + offset;
            std::ostringstream angle;
            const float deviation = angles[edgeIndex];
            if (std::isfinite(deviation)) angle << std::fixed << std::setprecision(1) << deviation;
            else angle << "invalid";
            renderer.setColor(forceAngleColor(deviation));
            renderer.drawText(angle.str(), midpoint + Vec3(0.0f, 0.0f, 0.001f), 1.00f);
        }
    }

    std::string m_objPath{"tna.obj"};
    std::shared_ptr<MeshObject> m_inputObject;
    std::shared_ptr<MeshData> m_inputMesh;
    std::shared_ptr<MeshData> m_formMesh;
    std::shared_ptr<MeshData> m_forceMesh;
    std::shared_ptr<MeshData> m_initialFormMesh;
    std::unique_ptr<SimpleUI> m_ui;
    TnaSolver m_solver;
    TnaFormDiagram m_result;
    TnaForceDiagram m_forceResult;
    std::vector<int> m_verticalOnlySupports;
    std::string m_status{"Loading TNA mesh"};
    float m_formWeight{0.5f};
    float m_horizontalTargetAngle{3.0f};

    // Horizontal controls not shown as sliders. Edit these values directly
    // to reproduce COMPAS per-edge constraints. A zero length ratio means
    // unbounded; hmin/hmax are force units before division by forceScale.
    float m_horizontalMaximumIterations{100.0f};
    float m_horizontalForceScale{1.0f};
    float m_formEdgeMinimumRatio{0.0f};
    float m_formEdgeMaximumRatio{0.0f};
    // Conservative anti-collapse band for the initially constructed dual.
    // Set either to 0.0f to remove that side of the constraint.
    float m_forceEdgeMinimumRatio{0.10f};
    float m_forceEdgeMaximumRatio{5.0f};
    float m_horizontalForceMinimum{0.0f};
    float m_horizontalForceMaximum{1e7f};

    // Vertical controls not shown as sliders.
    float m_verticalLoad{0.001f};
    float m_selfWeightDensity{1.0f};
    float m_verticalThickness{1.0f};
    float m_verticalForceScale{1.0f};
    float m_verticalResidualTolerance{1e-3f};
    float m_verticalMaximumIterations{100.0f};
    // Offset applied to force vertices incident exclusively to tension edges.
    float m_tensionForceVertexDistance{0.0f};
    float m_appliedTensionForceVertexDistance{std::numeric_limits<float>::infinity()};
    std::vector<int> m_tensionForceVertexIds;
    std::vector<Vec3> m_tensionForceVertexPositions;
    std::vector<Vec3> m_tensionForceVertexDirections;
    int m_horizontalStepsPerFrame{2};
    bool m_horizontalRunning{false};
    // Set true only for legacy untagged OBJs whose full boundary is intended
    // to be fixed. The default leaves an untagged form support-free.
    bool m_fallbackToAllBoundarySupports{false};
    bool m_tensionEnabled{true};
    bool m_allBoundaryTension{false};
    bool m_growBoundaryTension{false};
    bool m_showInput{true};
    bool m_showForceAngles{true};
    bool m_showForceDensities{false};
};

ALICE2_REGISTER_SKETCH_AUTO(TnaSketch)

#endif

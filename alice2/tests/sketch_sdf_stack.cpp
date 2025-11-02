// #define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>
#include <computeGeom/scalarField.h>
#include <objects/GraphObject.h>
#include <computeGeom/scalarField3D.h>
#include <objects/MeshObject.h>

#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include <cmath>

using namespace alice2;
using json = nlohmann::json;

class FieldStackFromJson : public ISketch {
public:
    FieldStackFromJson() = default;

    std::string getName() const override        { return "JSON Field Stack"; }
    std::string getDescription() const override { return "Visualize multiple scalar-field slices from JSON"; }
    std::string getAuthor() const override      { return "alice2 User"; }

    void setup() override {
        scene().setBackgroundColor(Color(0.9f, 0.9f, 0.9f));
        scene().setShowAxes(false);
        scene().setShowGrid(false);

        m_meshObject = std::make_shared<MeshObject>("StackIsoMesh");
        m_meshObject->setVisible(false);
        m_meshObject->setRenderMode(MeshRenderMode::NormalShaded);
        m_meshObject->setShowEdges(false);
        m_meshObject->setShowVertices(false);
        scene().addObject(m_meshObject);

        bool loaded = loadFields(m_inputJsonName);
        if (!loaded) {
            const std::filesystem::path fallback = std::filesystem::path("alice2") / "data" / "inFieldStack.json";
            loadFields(fallback.string());
        }

        // UI setup
        m_ui = std::make_unique<SimpleUI>(input());
        m_ui->setTheme(SimpleUI::UITheme::Dark);
        // Slice setting group
        m_ui->addSlider("Floor Height",  Vec2{10, 150}, 240.0f, 0.05f, 5.0f, m_sliceSpacing);
        m_ui->addToggle("Smooth*5",        UIRect{10, 160, 100, 22},  m_btnSmooth);
        m_ui->addToggle("Laplacian*5",     UIRect{120, 160, 120, 22}, m_btnLaplacian);
        m_ui->addToggle("Build Mesh",    UIRect{10, 190, 100, 22}, m_btnBuildMesh);
        m_ui->addToggle("Display Mesh",  UIRect{120, 190, 120, 22}, m_meshVisible);

        // Column generation group
        m_ui->addSlider("Sample Dist",    Vec2{10, 270}, 240.0f, 1.0f, 20.0f, m_sampleDistance);
        m_ui->addSlider("Column Offset",  Vec2{10, 300}, 240.0f, -5.0f, 0.0f, m_columnOffset);
        m_ui->addSlider("Gravity Weight", Vec2{10, 330}, 240.0f, 0.0f, 1.0f, m_gravityWeight);
        m_ui->addToggle("Init Columns",   UIRect{10, 350, 120, 22}, m_btnInitColumns);
        m_ui->addToggle("Run Columns",    UIRect{10, 380, 120, 22}, m_makeColumns);

        // Export at bottom
        m_ui->addToggle("ReloadSDF",      UIRect{10, 430, 100, 22}, m_btnReload);
        m_ui->addToggle("Export",         UIRect{10, 460, 100, 22}, m_btnExport);

        // Track last values to detect changes from UI
        m_lastSliceSpacing   = m_sliceSpacing;
        m_lastSampleDistance = m_sampleDistance;
        m_lastColumnOffset   = m_columnOffset;
        m_lastGravityWeight  = m_gravityWeight;
        m_lastMeshVisible    = m_meshVisible;
    }

    void update(float time) override {
        // Apply UI actions and sync values/state
        postUpdateUI();

        if (!m_makeColumns) {
            return;
        }

        if (m_columnParticles.empty()) {
            initColumnParticles();
            initColumnLines();
        }

        if (!m_volumeField) {
            return;
        }

        if (m_columnTrajectories.size() != m_columnParticles.size()) {
            m_columnTrajectories.resize(m_columnParticles.size());
        }

        if (m_columnLines.size() != m_columnParticles.size()) {
            initColumnLines();
        }

        for (size_t i = 0; i < m_columnParticles.size(); ++i) {
            auto& particle = m_columnParticles[i];
            auto& history = m_columnTrajectories[i];
            auto& graph = m_columnLines[i];

            if (!graph) {
                graph = std::make_shared<GraphObject>("StackColumn_" + std::to_string(i));
                graph->setShowVertices(false);
                graph->setShowEdges(true);
                graph->setColor(Color(1.0f, 1.0f, 1.0f));
                graph->setEdgeWidth(2.0f);
                scene().addObject(graph);
            }

            if (history.empty()) {
                history.emplace_back(particle.x, particle.y, particle.z + 0.1f);
                history.emplace_back(particle);
                refreshColumnGraph(i);
            }

            Vec3 gravity(0.0f, 0.0f, -1.0f);
            Vec3 pullForce = m_volumeField->project_onto_isosurface(particle, m_columnOffset) - particle;

            Vec3 totalForce = gravity * m_gravityWeight + pullForce * (1.0f - m_gravityWeight);
            float verticalComponent = totalForce.dot(gravity);
            if (verticalComponent < 0.0f) {
                totalForce -= gravity * verticalComponent;
            }

            Vec3 step = totalForce * m_particleStep;
            if (step.lengthSquared() <= 1e-8f) {
                continue;
            }

            Vec3 nextPosition = particle + step;
            if(m_volumeField->value_at(nextPosition) > 0.0f)
                nextPosition = m_volumeField->project_onto_isosurface(particle, m_columnOffset);

            if (nextPosition.z < 0.0f) {
                nextPosition.z = 0.0f;
            }

            if (!history.empty()) {
                Vec3& lastPosition = history.back();
                if ((nextPosition - lastPosition).lengthSquared() < 1e-6f) {
                    particle = nextPosition;
                    lastPosition = nextPosition;
                    refreshColumnGraph(i);
                    continue;
                }
            }

            particle = nextPosition;
            history.push_back(nextPosition);
            refreshColumnGraph(i);
        }
    }

    void draw(Renderer& renderer, Camera& /*camera*/) override {
        renderer.setColor(Color(0.1f, 0.1f, 0.1f));
        // Header and status
        renderer.drawString("SDF Tower Sketch", 10, 20);
        renderer.drawString("Slices: " + std::to_string(m_contours.size()), 10, 40);
        renderer.drawString("Mesh visible: " + std::string(m_meshVisible ? "yes" : "no"), 10, 60);
        renderer.drawString(m_statusMessage, 10, 80);

        // Group headings
        renderer.drawString("Slice Setting", 10, 120);
        renderer.drawString("Column Generation", 10, 240);

        if (m_columnParticles.size() > 0) {
            for(auto m_pt : m_columnParticles)
                renderer.drawPoint(m_pt, Color(1.0f, 1.0f, 1.0f), 10.0f);
        }

        updateContourPlacement();
        if (m_ui) m_ui->draw(renderer);
    }
    
    bool onKeyPress(unsigned char key, int /*x*/, int /*y*/) override {
        // Keep J/K shortcuts (UI provides buttons too)
        if (key == 'j' || key == 'J') {
            smooth();
            return true;
        }
        if (key == 'k' || key == 'K') {
            applyStackLaplacian();
            return true;
        }
        return false;
    }

    bool onMousePress(int button, int state, int x, int y) override {
        if (m_ui && m_ui->onMousePress(button, state, x, y)) return true;
        return false;
    }

    bool onMouseMove(int x, int y) override {
        if (m_ui && m_ui->onMouseMove(x, y)) return true;
        return false;
    }

private:
    bool loadFields(const std::string& path) {
        m_jsonLoaded = false;

        std::ifstream file(path);
        if (!file.is_open()) {
            m_statusMessage = "Failed to open " + path;
            return false;
        }

        json j;
        file >> j;

        const int resX = j.value("scalar_field_XCount", 0);
        const int resY = j.value("scalar_field_YCount", 0);
        const auto xSizes = j.value("scalar_field_XSize", std::vector<double>{-1.0, 1.0});
        const auto ySizes = j.value("scalar_field_YSize", std::vector<double>{-1.0, 1.0});

        if (resX <= 0 || resY <= 0 || xSizes.size() != 2 || ySizes.size() != 2) {
            m_statusMessage = "Invalid grid metadata in " + path;
            return false;
        }

        const Vec3 bmin(static_cast<float>(xSizes[0]),
                        static_cast<float>(ySizes[0]),
                        0.0f);
        const Vec3 bmax(static_cast<float>(xSizes[1]),
                        static_cast<float>(ySizes[1]),
                        0.0f);
        m_boundsMin = bmin;
        m_boundsMax = bmax;

        std::vector<std::pair<std::string, std::vector<float>>> fieldData;
        for (auto& [key, value] : j.items()) {
            if ((key.rfind("out_scalar_field_", 0) == 0) && value.is_array()) {
                std::vector<float> samples = value.get<std::vector<float>>();
                if (static_cast<int>(samples.size()) != resX * resY) {
                    m_statusMessage = "Size mismatch in " + key;
                    continue;
                }
                fieldData.emplace_back(key, std::move(samples));
            }
        }

        if (fieldData.empty()) {
            m_statusMessage = "No scalar fields found in " + path;
            invalidateVolumeMesh();
            return false;
        }

        std::sort(fieldData.begin(), fieldData.end(), [](const auto& a, const auto& b) {
            return a.first < b.first;
        });

        m_fields.clear();
        m_fieldKeys.clear();
        clearContourObjects();
        for (auto& entry : fieldData) {
            ScalarField2D slice(bmin, bmax, resX, resY);
            slice.set_values(entry.second);
            m_fieldKeys.emplace_back(entry.first);
            m_fields.emplace_back(std::move(slice));
        }

        regenerateContours();
        invalidateVolumeMesh();

        m_loadedJson = j;
        m_jsonLoaded = true;
        m_loadedJsonPath = path;
        m_statusMessage = "Loaded " + std::to_string(m_fields.size()) + " slices";
        return true;
    }

    void applyStackLaplacian() {
        if (m_fields.size() < 3) {
            m_statusMessage = "Need at least three slices for Laplacian smoothing";
            return;
        }

        const size_t sliceCount = m_fields.size();
        std::vector<std::vector<float>> smoothed(sliceCount);

        for (size_t i = 0; i < sliceCount; ++i) {
            smoothed[i] = m_fields[i].get_values();
        }

        for (size_t i = 1; i + 1 < sliceCount; ++i) {
            const auto& prev = m_fields[i - 1].get_values();
            const auto& next = m_fields[i + 1].get_values();
            auto& dest = smoothed[i];

            for (size_t v = 0; v < dest.size(); ++v) {
                dest[v] = 0.5f * (prev[v] + next[v]);
            }
        }

        for (size_t i = 0; i < sliceCount; ++i) {
            m_fields[i].set_values(smoothed[i]);
        }

        regenerateContours();
        invalidateVolumeMesh();
        m_statusMessage = "Stack Laplacian applied";
    }

    void smooth() {
        if (m_fields.empty()) {
            m_statusMessage = "No slices loaded";
            return;
        }

        for (auto& field : m_fields) {
            smoothField(field);
        }

        regenerateContours();
        invalidateVolumeMesh();
        m_statusMessage = "In-plane smoothing applied";
    }

    void smoothField(ScalarField2D& field) {
        const auto& values = field.get_values();
        if (values.empty()) {
            return;
        }

        const auto resolution = field.get_resolution();
        const int resX = resolution.first;
        const int resY = resolution.second;
        if (resX <= 0 || resY <= 0) {
            return;
        }

        std::vector<float> filtered(values.size(), 0.0f);
        auto index = [resX](int x, int y) { return y * resX + x; };

        for (int y = 0; y < resY; ++y) {
            for (int x = 0; x < resX; ++x) {
                float sum = 0.0f;
                int count = 0;
                const int yStart = std::max(0, y - 1);
                const int yEnd = std::min(resY - 1, y + 1);
                const int xStart = std::max(0, x - 1);
                const int xEnd = std::min(resX - 1, x + 1);

                for (int ny = yStart; ny <= yEnd; ++ny) {
                    for (int nx = xStart; nx <= xEnd; ++nx) {
                        sum += values[index(nx, ny)];
                        ++count;
                    }
                }

                filtered[index(x, y)] = count > 0 ? sum / static_cast<float>(count) : values[index(x, y)];
            }
        }

        field.set_values(filtered);
    }

    bool buildVolumeMeshFromStack() {
        if (m_fields.empty()) {
            m_statusMessage = "Load fields before generating mesh";
            return false;
        }

        const auto resolution = m_fields.front().get_resolution();
        const int resX = resolution.first;
        const int resY = resolution.second;
        const int resZ = static_cast<int>(m_fields.size());
        if (resX <= 0 || resY <= 0 || resZ <= 0) {
            m_statusMessage = "Invalid field resolution";
            return false;
        }

        Vec3 minBound = Vec3(m_boundsMin.x, m_boundsMin.y, 0.0f);
        Vec3 maxBound = Vec3(m_boundsMax.x, m_boundsMax.y, resZ > 1 ? m_sliceSpacing * static_cast<float>(resZ - 1) : m_sliceSpacing);

        m_volumeField = std::make_unique<ScalarField3D>(minBound, maxBound, resX, resY, resZ);

        const size_t layerSize = static_cast<size_t>(resX) * static_cast<size_t>(resY);
        std::vector<float> volumeValues(layerSize * static_cast<size_t>(resZ), 0.0f);
        for (int z = 0; z < resZ; ++z) {
            const auto& sliceValues = m_fields[static_cast<size_t>(z)].get_values();
            std::copy(sliceValues.begin(), sliceValues.end(), volumeValues.begin() + layerSize * static_cast<size_t>(z));
        }

        m_volumeField->set_values(volumeValues);

        auto meshData = m_volumeField->generate_mesh(m_isoLevel);
        if (!meshData || meshData->vertices.empty()) {
            m_statusMessage = "3D mesh generation yielded no geometry";
            m_meshGenerated = false;
            m_meshVisible = false;
            if (m_meshObject) {
                m_meshObject->setVisible(false);
            }
            return false;
        }

        if (!m_meshObject) {
            m_meshObject = std::make_shared<MeshObject>("StackIsoMesh");
            m_meshObject->setVisible(false);
            m_meshObject->setShowEdges(false);
            m_meshObject->setShowVertices(false);
            scene().addObject(m_meshObject);
        }

        m_meshObject->setMeshData(meshData);
        m_meshObject->setRenderMode(MeshRenderMode::NormalShaded);
        m_meshObject->setNormalShadingColors(Color(0.1f, 0.1f, 0.1f), Color(1.0f, 1.0f, 1.0f)); // White to magenta

        m_meshGenerated = true;
        m_meshVisible = true;
        m_meshObject->setVisible(true);
        m_statusMessage = "3D mesh generated";
        return true;
    }

    void toggleMeshVisibility() {
        if (!m_meshGenerated || !m_meshObject) {
            m_statusMessage = "Generate the mesh first (press P)";
            return;
        }

        m_meshVisible = !m_meshVisible;
        m_meshObject->setVisible(m_meshVisible);
        m_statusMessage = m_meshVisible ? "Mesh visible" : "Mesh hidden";
    }

    void exportFieldsAndMesh() {
        if (!m_jsonLoaded || m_fields.empty()) {
            m_statusMessage = "Nothing to export";
            return;
        }

        updateJsonWithCurrentFields();

        std::ofstream outJson(m_outputJsonName);
        if (!outJson.is_open()) {
            m_statusMessage = "Failed to write " + m_outputJsonName;
            return;
        }
        outJson << m_loadedJson.dump(2);

        if (!m_meshGenerated) {
            if (!buildVolumeMeshFromStack()) {
                m_statusMessage = "Fields saved but mesh generation failed";
                return;
            }
        }

        if (!m_meshObject) {
            m_statusMessage = "Fields saved but mesh object missing";
            return;
        }
        auto m_graphObject = std::make_shared<GraphObject>("ExportGraph");

        for(int i = 0; i < m_columnLines.size(); ++i){
            m_graphObject->combineWith(*m_columnLines[i]);
        }
        m_graphObject->weld();

        m_graphObject->writeToObj(m_outputGraphName);

        m_meshObject->writeToObj(m_outputMeshName);
        m_statusMessage = "Exported fields -> " + m_outputJsonName + ", mesh -> " + m_outputMeshName;
    }

    void updateJsonWithCurrentFields() {
        const auto resolution = m_fields.front().get_resolution();
        m_loadedJson["scalar_field_XCount"] = resolution.first;
        m_loadedJson["scalar_field_YCount"] = resolution.second;
        m_loadedJson["scalar_field_XSize"] = { m_boundsMin.x, m_boundsMax.x };
        m_loadedJson["scalar_field_YSize"] = { m_boundsMin.y, m_boundsMax.y };

        for (size_t i = 0; i < m_fields.size(); ++i) {
            std::string key;
            if (i < m_fieldKeys.size()) {
                key = m_fieldKeys[i];
            } else {
                key = fieldKeyForIndex(i);
                m_fieldKeys.push_back(key);
            }
            m_loadedJson[key] = m_fields[i].get_values();
        }
    }

    std::string fieldKeyForIndex(size_t idx) const {
        std::ostringstream oss;
        oss << "out_scalar_field_" << std::setw(2) << std::setfill('0') << idx;
        return oss.str();
    }

    void regenerateContours() {
        clearContourObjects();
        m_contours.reserve(m_fields.size());

        for (size_t idx = 0; idx < m_fields.size(); ++idx) {
            GraphObject contourGraph = m_fields[idx].get_contours(m_isoLevel);
            contourGraph.setShowVertices(false);
            contourGraph.setShowEdges(true);
            contourGraph.setEdgeWidth(1.0f);

            auto contour = std::make_shared<GraphObject>(std::move(contourGraph));
            contour->setName("FieldSliceContour_" + std::to_string(idx));
            scene().addObject(contour);
            m_contours.emplace_back(std::move(contour));
        }

        updateContourPlacement();
    }

    void clearContourObjects() {
        for (auto& contour : m_contours) {
            if (contour) {
                scene().removeObject(contour);
            }
        }
        m_contours.clear();
    }

    void updateContourPlacement() {
        const size_t count = m_contours.size();
        if (count == 0) {
            return;
        }

        for (size_t idx = 0; idx < count; ++idx) {
            auto& contour = m_contours[idx];
            if (!contour) {
                continue;
            }

            const float hue = 360.0f * (static_cast<float>(idx) / std::max<size_t>(1, count));
            float r, g, b;
            ScalarFieldUtils::get_hsv_color(hue / 360.0f * 2.0f - 1.0f, r, g, b);
            contour->setEdgeColor(Color(r, g, b));
            contour->setColor(Color(r, g, b));
            contour->getTransform().setTranslation(Vec3(0, 0, static_cast<float>(idx) * m_sliceSpacing));
        }
    }

    void invalidateVolumeMesh() {
        m_volumeField.reset();
        m_meshGenerated = false;
        m_meshVisible = false;
        if (m_meshObject) {
            m_meshObject->setVisible(false);
        }
    }

    void initColumnParticles(){
        m_columnParticles.clear();
        m_columnTrajectories.clear();

        auto m_lastContour = m_fields[m_fields.size() - 1].get_contours(m_columnOffset);
        m_lastContour.weld();
        auto m_separated = m_lastContour.separate();
        
        for(auto graph : m_separated){
            int sampleCount = static_cast<int>(graph.getLength() / m_sampleDistance);
            graph.resampleByCount(sampleCount);
            auto vertices = graph.getGraphData()->vertices;

            for(auto vertex : vertices){
                Vec3 particlePos(
                    vertex.position.x, 
                    vertex.position.y, 
                    static_cast<float>(m_contours.size() - 1) * m_sliceSpacing
                );
                m_columnParticles.push_back(particlePos);

                std::vector<Vec3> trajectory;
                trajectory.emplace_back(vertex.position.x, vertex.position.y, particlePos.z + 0.1f);
                trajectory.emplace_back(particlePos);
                m_columnTrajectories.emplace_back(std::move(trajectory));
            }
        }
    }

    void initColumnLines(){
        for(auto& graph : m_columnLines){
            if (graph){
                scene().removeObject(graph);
            }
        }
        m_columnLines.clear();
        m_columnLines.reserve(m_columnParticles.size());

        for(size_t idx = 0; idx < m_columnParticles.size(); ++idx){
            auto columnGraph = std::make_shared<GraphObject>("StackColumn_" + std::to_string(idx));
            columnGraph->setShowVertices(false);
            columnGraph->setShowEdges(true);
            columnGraph->setEdgeColor(Color(0.0f, 0.0f, 0.0f));
            columnGraph->setEdgeWidth(4.0f);
            scene().addObject(columnGraph);
            m_columnLines.emplace_back(std::move(columnGraph));
            refreshColumnGraph(idx);
        }
    }

    void refreshColumnGraph(size_t idx){
        if (idx >= m_columnLines.size() || idx >= m_columnTrajectories.size()) {
            return;
        }

        auto& graph = m_columnLines[idx];
        if (!graph) {
            return;
        }

        const auto& trajectory = m_columnTrajectories[idx];
        if (trajectory.empty()) {
            return;
        }

        std::vector<std::pair<int, int>> edges;
        if (trajectory.size() > 1) {
            edges.reserve(trajectory.size() - 1);
            for (size_t j = 1; j < trajectory.size(); ++j) {
                edges.emplace_back(static_cast<int>(j - 1), static_cast<int>(j));
            }
        }

        graph->createFromPositionsAndEdges(trajectory, edges);
        graph->setShowVertices(false);
        graph->setShowEdges(true);
    }

    // Handle UI-triggered actions and apply param/state changes
    void postUpdateUI() {
        // Momentary actions triggered by toggles
        if (m_btnBuildMesh) { buildVolumeMeshFromStack(); m_btnBuildMesh = false; }
        if (m_btnExport)    { exportFieldsAndMesh();      m_btnExport = false; }
        if (m_btnReload)    { loadFields(m_inputJsonName);      m_btnReload = false; }

        if (m_btnInitColumns) { initColumnParticles(); initColumnLines(); m_btnInitColumns = false; }
        if (m_btnSmooth)    { for(size_t i = 0; i < 5; ++i) { smooth(); } m_btnSmooth = false; }
        if (m_btnLaplacian) { for(size_t i = 0; i < 5; ++i) { applyStackLaplacian(); } m_btnLaplacian = false; }

        // Apply mesh visibility changes via UI toggle
        if (m_meshVisible != m_lastMeshVisible && m_meshObject) {
            m_meshObject->setVisible(m_meshVisible);
            m_statusMessage = m_meshVisible ? "Mesh visible" : "Mesh hidden";
            m_lastMeshVisible = m_meshVisible;
        }

        // Clamp slider ranges (respect updated slider values)
        auto clampf = [](float v, float a, float b){ return std::max(a, std::min(v, b)); };
        m_sliceSpacing   = clampf(m_sliceSpacing,   0.05f, 5.0f);
        m_sampleDistance = clampf(m_sampleDistance, 1.0f, 20.0f);
        m_columnOffset   = clampf(m_columnOffset,  -5.0f, 1.0f);
        m_gravityWeight  = clampf(m_gravityWeight,  0.0f, 1.0f);

        // React to value changes
        if (std::abs(m_sliceSpacing - m_lastSliceSpacing) > 1e-6f) {
            invalidateVolumeMesh();
            updateContourPlacement();
            m_lastSliceSpacing = m_sliceSpacing;
        }
        if (std::abs(m_sampleDistance - m_lastSampleDistance) > 1e-6f) {
            if (!m_columnParticles.empty()) {
                initColumnParticles();
                initColumnLines();
            }
            m_lastSampleDistance = m_sampleDistance;
        }
        if (std::abs(m_columnOffset - m_lastColumnOffset) > 1e-6f) {
            if (!m_columnParticles.empty()) {
                initColumnParticles();
                initColumnLines();
            }
            m_lastColumnOffset = m_columnOffset;
        }
        if (std::abs(m_gravityWeight - m_lastGravityWeight) > 1e-6f) {
            m_lastGravityWeight = m_gravityWeight;
        }
    }


    const std::string m_inputJsonName = "inFieldStack.json";
    const std::string m_outputJsonName = "outFieldStack.json";
    const std::string m_outputMeshName = "outMesh.obj";
    const std::string m_outputGraphName = "outColumns.obj";

    std::vector<ScalarField2D> m_fields;
    std::vector<std::shared_ptr<GraphObject>> m_contours;
    std::vector<std::string> m_fieldKeys;
    float m_isoLevel = 0.01f;
    float m_sliceSpacing = 3.5f;
    std::string m_statusMessage = "Awaiting load";

    Vec3 m_boundsMin = Vec3(-1, -1, 0);
    Vec3 m_boundsMax = Vec3(1, 1, 0);

    std::unique_ptr<ScalarField3D> m_volumeField;
    std::shared_ptr<MeshObject> m_meshObject;
    bool m_meshVisible = false;
    bool m_meshGenerated = false;

    json m_loadedJson;
    bool m_jsonLoaded = false;
    std::string m_loadedJsonPath;

    std::vector<Vec3> m_columnParticles;
    std::vector<std::shared_ptr<GraphObject>> m_columnLines;
    std::vector<std::vector<Vec3>> m_columnTrajectories;
    bool m_makeColumns = false;
    float m_sampleDistance = 10.0f;
    float m_particleStep = 0.55f;
    float m_gravityWeight = 1.0f;
    float m_columnOffset = -0.2f;

    // UI
    std::unique_ptr<SimpleUI> m_ui;
    bool m_btnBuildMesh{false};
    bool m_btnExport{false};
    bool m_btnReload{false};
    bool m_btnInitColumns{false};
    bool m_btnSmooth{false};
    bool m_btnLaplacian{false};

    // Change tracking for UI-bound values
    float m_lastSliceSpacing{0.0f};
    float m_lastSampleDistance{0.0f};
    float m_lastColumnOffset{0.0f};
    float m_lastGravityWeight{0.0f};
    bool  m_lastMeshVisible{false};
};

ALICE2_REGISTER_SKETCH_AUTO(FieldStackFromJson)

#endif // __MAIN__






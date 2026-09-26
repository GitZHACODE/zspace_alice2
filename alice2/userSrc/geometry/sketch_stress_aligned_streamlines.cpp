// #define __MAIN__

#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace alice2;

enum class FieldSource {
    Stress,
    Curvature
};

class StressAlignedMiqSketch : public ISketch {
public:
    std::string getName() const override { return "Stress Aligned MIQ Grid"; }
    std::string getDescription() const override { return "MIQ grid aligned to stress or curvature fields"; }
    std::string getAuthor() const override { return "alice2 User"; }

    void setup() override {
        scene().setBackgroundColor(Color(0.96f, 0.96f, 0.96f, 1.0f));
        scene().setShowGrid(false);
        scene().setShowAxes(false);
        m_ui = std::make_unique<SimpleUI>(input());
        m_ui->addSlider("Grid Spacing", Vec2{10.0f, 92.0f}, 180.0f, 0.02f, 2.0f, m_spacing);
        m_ui->addSlider("Smooth Steps", Vec2{10.0f, 124.0f}, 180.0f, 0.0f, 50.0f, m_smoothSteps);
        loadAndSolve();
    }

    void update(float) override {
        m_smoothSteps = std::round(m_smoothSteps);
        if (std::abs(m_spacing - m_lastSpacing) > 1e-4f || smoothingSteps() != m_lastSmoothSteps) {
            runMiq();
            m_lastSpacing = m_spacing;
            m_lastSmoothSteps = smoothingSteps();
        }
    }

    void draw(Renderer& renderer, Camera&) override {
        if (!hasMesh()) {
            renderer.setColor(Color(0.1f, 0.1f, 0.1f, 1.0f));
            renderer.drawString(m_status, 10.0f, 30.0f);
            if (m_ui) m_ui->draw(renderer);
            return;
        }

        drawActiveField(renderer);
        if (m_drawMiqGrid) drawMiqGrid(renderer);

        renderer.setColor(Color(0.05f, 0.05f, 0.05f, 1.0f));
        renderer.drawString("m stress/curvature | c stress colour | x crosses | o/p cross size | q contours | e export | r reload", 10.0f, 30.0f);
        renderer.drawString(m_status, 10.0f, 52.0f);
        renderer.drawString(m_miqStatus, 10.0f, 74.0f);
        if (m_ui) m_ui->draw(renderer);
    }

    bool onKeyPress(unsigned char key, int, int) override {
        switch (key) {
            case 'c': case 'C': m_drawStress = !m_drawStress; return true;
            case 'x': case 'X': m_drawCrosses = !m_drawCrosses; return true;
            case 'o': case 'O': m_crossScale *= 1.2f; return true;
            case 'p': case 'P': m_crossScale /= 1.2f; return true;
            case 'q': case 'Q': m_drawMiqGrid = !m_drawMiqGrid; return true;
            case 'e': case 'E': exportRemesh(); return true;
            case 'm': case 'M':
                m_fieldSource = m_fieldSource == FieldSource::Stress ? FieldSource::Curvature : FieldSource::Stress;
                runMiq();
                return true;
            case 'r': case 'R': loadAndSolve(); return true;
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
    bool hasMesh() const {
        const auto data = m_mesh ? m_mesh->getMeshData() : nullptr;
        return data && !data->vertices.empty() && !data->faces.empty();
    }

    void loadAndSolve() {
        // Never retain a field or exported grid from a previous input mesh.
        m_analyzer = StressAnalyzer{};
        m_miqResult = MiqRemeshResult{};
        m_curvatureField.clear();
        m_smoothedField.clear();
        m_supportVertices.clear();
        m_loadVertices.clear();
        m_miqStatus.clear();
        m_lastSpacing = m_spacing;
        m_lastSmoothSteps = smoothingSteps();
        m_mesh = std::make_shared<MeshObject>("slab_long");
        try {
            m_mesh->readFromObj(m_objPath);
            const size_t importedVertices = m_mesh->getMeshData()->vertices.size();
            // Maya's face-vertex colour export splits shared corners. Weld
            // before triangulation and retain black markers from any duplicate.
            m_mesh->weld(1e-6f, true);
            std::printf("[StressMiq] Welded vertices %zu -> %zu (preserving black supports)\n",
                        importedVertices, m_mesh->getMeshData()->vertices.size());
            triangulateFaces();
            m_mesh->generateEdgesFromFaces();
            m_mesh->recalculateNormals();
            m_mesh->setShowFaces(false);
        } catch (const std::exception& error) {
            m_status = std::string("Failed to load ") + m_objPath + ": " + error.what();
            m_miqStatus.clear();
            return;
        }
        if (!hasMesh()) {
            m_status = "Loaded mesh has no valid triangular faces";
            return;
        }

        selectSupportAndLoadVertices();
        m_analyzer.clearBoundaryConditions();
        m_analyzer.clearForces();
        m_analyzer.setFixedVertices(m_supportVertices);
        if (m_supportVertices.empty()) {
            m_status = "No black supports: export OBJ vertex colours as v x y z r g b (black = 0 0 0)";
            std::printf("[StressMiq] %s\n", m_status.c_str());
            return;
        }
        if (m_loadVertices.empty()) {
            m_status = "No free load vertices: all vertices are black supports";
            return;
        }
        m_analyzer.setForces(m_loadVertices, Vec3(0.0f, 0.0f, -0.03f));
        // Smooth the active source field below, without repeating the solve.
        m_analyzer.setFieldSmoothingIterations(0);
        m_analyzer.setStressMagnitudeThreshold(1e-8);
        if (!m_analyzer.solveVerticalSlab(*m_mesh)) {
            m_status = "Stress solve failed";
            m_miqStatus.clear();
            return;
        }

        m_analyzer.colorMeshByMagnitude(*m_mesh);
        buildCurvatureField();
        float maxStress = 0.0f;
        for (float value : m_analyzer.getStressMagnitudes()) maxStress = std::max(maxStress, value);
        char buffer[128];
        std::snprintf(buffer, sizeof(buffer), "Stress field | black supports %zu | loads %zu | max %.5f",
                      m_supportVertices.size(), m_loadVertices.size(), maxStress);
        m_status = buffer;
        std::printf("[StressMiq] %s\n", m_status.c_str());
        runMiq();
        m_lastSpacing = m_spacing;
    }

    void triangulateFaces() {
        auto data = m_mesh->getMeshData();
        if (!data) return;
        std::vector<MeshFace> triangles;
        for (const MeshFace& face : data->faces) {
            for (size_t i = 1; i + 1 < face.vertices.size(); ++i) {
                triangles.emplace_back(std::vector<int>{face.vertices[0], face.vertices[i], face.vertices[i + 1]}, face.normal, face.color);
            }
        }
        data->faces = std::move(triangles);
        data->triangulationDirty = true;
    }

    void selectSupportAndLoadVertices() {
        m_supportVertices.clear();
        m_loadVertices.clear();
        const auto data = m_mesh->getMeshData();
        constexpr float blackThreshold = 0.02f;
        for (int id = 0; id < static_cast<int>(data->vertices.size()); ++id) {
            // Read source colours before colorMeshByMagnitude overwrites them.
            const MeshVertex& vertex = data->vertices[id];
            const Color& color = vertex.color;
            if (color.r <= blackThreshold && color.g <= blackThreshold && color.b <= blackThreshold) {
                m_supportVertices.push_back(id);
            } else {
                m_loadVertices.push_back(id);
            }
        }
    }

    void runMiq() {
        m_miqResult = MiqRemeshResult{};
        m_smoothedField.clear();
        if (!hasMesh()) return;
        const TensorField* field = &m_analyzer.getSmoothedCrossField();
        if (m_fieldSource == FieldSource::Curvature) {
            if (m_curvatureField.empty()) buildCurvatureField();
            field = &m_curvatureField;
        }
        if (field->empty()) {
            m_miqStatus = std::string(fieldName()) + " field is unavailable";
            return;
        }
        m_smoothedField = *field;
        smoothActiveField();
        MiqRemeshOptions options;
        options.targetSpacing = m_spacing;
        m_miqResult = m_miqRemesher.parameterize(*m_mesh->getMeshData(), m_smoothedField, options);
        m_miqStatus = m_miqResult.success ? std::string(fieldName()) + " MIQ: " + m_miqResult.diagnostic
                                           : std::string(fieldName()) + " MIQ rejected: " + m_miqResult.diagnostic;
        m_miqStatus += " | smooth steps " + std::to_string(smoothingSteps());
        std::printf("[StressMiq] %s\n", m_miqStatus.c_str());
    }

    int smoothingSteps() const { return static_cast<int>(std::round(m_smoothSteps)); }

    void smoothActiveField() {
        const auto data = m_mesh->getMeshData();
        std::vector<Vec3> normals(data->faces.size());
        std::vector<std::vector<int>> neighbors(data->faces.size());
        std::map<std::pair<int, int>, std::vector<int>> edgeFaces;
        for (int fi = 0; fi < static_cast<int>(data->faces.size()); ++fi) {
            const auto& face = data->faces[fi];
            normals[fi] = data->calculateFaceNormal(face).normalized();
            // Even at zero steps MIQ needs an orthonormal tangent frame.
            Vec3 major = m_smoothedField[fi].majorDirection;
            major -= normals[fi] * major.dot(normals[fi]);
            if (major.lengthSquared() <= 1e-10f)
                major = data->vertices[face.vertices[1]].position - data->vertices[face.vertices[0]].position;
            m_smoothedField[fi].majorDirection = major.normalized();
            m_smoothedField[fi].minorDirection = normals[fi].cross(major).normalized();
            for (int i = 0; i < 3; ++i) {
                const int a = face.vertices[i], b = face.vertices[(i + 1) % 3];
                edgeFaces[{std::min(a, b), std::max(a, b)}].push_back(fi);
            }
        }
        for (const auto& entry : edgeFaces) {
            for (int a : entry.second)
                for (int b : entry.second)
                    if (a != b) neighbors[a].push_back(b);
        }
        for (int step = 0; step < smoothingSteps(); ++step) {
            TensorField next = m_smoothedField;
            for (int fi = 0; fi < static_cast<int>(data->faces.size()); ++fi) {
                const Vec3 base = m_smoothedField[fi].majorDirection;
                Vec3 sum = base;
                for (int neighbor : neighbors[fi]) {
                    Vec3 direction = m_smoothedField[neighbor].majorDirection;
                    direction -= normals[fi] * direction.dot(normals[fi]);
                    if (direction.lengthSquared() <= 1e-10f) continue;
                    direction.normalize();
                    if (base.dot(direction) < 0.0f) direction = -direction;
                    sum += direction;
                }
                sum -= normals[fi] * sum.dot(normals[fi]);
                if (sum.lengthSquared() <= 1e-10f) continue;
                next[fi].majorDirection = sum.normalized();
                next[fi].minorDirection = normals[fi].cross(next[fi].majorDirection).normalized();
            }
            m_smoothedField = std::move(next);
        }
    }

    void exportRemesh() {
        if (!m_miqResult.success || !m_miqResult.quadMesh || m_miqResult.quadMesh->faces.empty()) {
            m_miqStatus = "No MIQ remesh available to export";
            return;
        }
        MeshObject output("miq_remesh");
        output.setMeshData(m_miqResult.quadMesh);
        output.writeToObj("data/remesh.obj");
        m_miqStatus = "Exported MIQ remesh to remesh.obj";
    }

    void buildCurvatureField() {
        m_curvatureField.clear();
        const auto data = m_mesh ? m_mesh->getMeshData() : nullptr;
        if (!data || data->faces.empty()) return;
        const auto curvature = m_mesh->principalCurvature(2, false);
        if (curvature.principalDirections.size() != data->vertices.size()) return;

        m_curvatureField.resize(data->faces.size());
        for (int fi = 0; fi < static_cast<int>(data->faces.size()); ++fi) {
            const MeshFace& face = data->faces[fi];
            const Vec3 normal = data->calculateFaceNormal(face).normalized();
            Vec3 major;
            float k1 = 0.0f;
            float k2 = 0.0f;
            int count = 0;
            for (int id : face.vertices) {
                Vec3 direction = curvature.principalDirections[id];
                direction -= normal * direction.dot(normal);
                if (direction.lengthSquared() <= 1e-10f) continue;
                direction.normalize();
                if (major.lengthSquared() > 1e-10f && major.dot(direction) < 0.0f) direction = -direction;
                major += direction;
                k1 += curvature.k1[id];
                k2 += curvature.k2[id];
                ++count;
            }
            if (major.lengthSquared() <= 1e-10f) {
                major = data->vertices[face.vertices[1]].position - data->vertices[face.vertices[0]].position;
                major -= normal * major.dot(normal);
            }
            major.normalize();
            FaceStressTensor& tensor = m_curvatureField[fi];
            tensor.majorDirection = major;
            tensor.minorDirection = normal.cross(major).normalized();
            if (count > 0) {
                tensor.majorValue = k1 / static_cast<float>(count);
                tensor.minorValue = k2 / static_cast<float>(count);
            }
            tensor.magnitude = std::abs(tensor.majorValue - tensor.minorValue);
        }
    }

    const char* fieldName() const {
        return m_fieldSource == FieldSource::Stress ? "Stress" : "Curvature";
    }

    void drawActiveField(Renderer& renderer) const {
        StressAnalysisDrawSettings settings;
        settings.drawColoredMesh = m_drawStress && m_fieldSource == FieldSource::Stress;
        settings.drawMeshEdges = true;
        settings.drawBoundaryConditions = true;
        settings.fixedVertexColor = Color(0.0f, 0.0f, 0.0f, 1.0f);
        settings.drawCrossField = false;
        settings.crossScale = m_crossScale;
        settings.edgeColor = Color(0.78f, 0.78f, 0.78f, 1.0f);
        if (m_fieldSource == FieldSource::Stress) {
            m_analyzer.draw(renderer, *m_mesh, settings);
            if (m_drawCrosses) drawTensorCrosses(renderer, *m_mesh->getMeshData(), m_smoothedField, settings);
            return;
        }
        const auto data = m_mesh->getMeshData();
        if (!data) return;
        m_analyzer.drawMeshEdges(renderer, *data, settings.edgeColor, settings.edgeWidth);
        if (m_drawCrosses) drawTensorCrosses(renderer, *data, m_smoothedField, settings);
    }

    void drawTensorCrosses(Renderer& renderer,
                           const MeshData& mesh,
                           const TensorField& field,
                           const StressAnalysisDrawSettings& settings) const {
        const int count = std::min(static_cast<int>(mesh.faces.size()), static_cast<int>(field.size()));
        for (int fi = 0; fi < count; ++fi) {
            const MeshFace& face = mesh.faces[fi];
            Vec3 center;
            for (int id : face.vertices) center += mesh.vertices[id].position;
            center /= static_cast<float>(face.vertices.size());
            renderer.drawLine(center - field[fi].majorDirection * settings.crossScale,
                              center + field[fi].majorDirection * settings.crossScale,
                              settings.majorColor,
                              1.3f);
            renderer.drawLine(center - field[fi].minorDirection * settings.crossScale,
                              center + field[fi].minorDirection * settings.crossScale,
                              settings.minorColor,
                              1.0f);
        }
    }

    void drawMiqGrid(Renderer& renderer) const {
        drawLineSet(renderer, m_miqResult.gridLines.v, Color(0.95f, 0.52f, 0.02f, 1.0f), 1.6f);
    }

    void drawLineSet(Renderer& renderer, const std::vector<std::vector<Vec3>>& lines, const Color& color, float width) const {
        std::vector<Vec3> segments;
        for (const auto& line : lines) {
            for (size_t i = 0; i + 1 < line.size(); ++i) {
                segments.push_back(line[i]);
                segments.push_back(line[i + 1]);
            }
        }
        if (!segments.empty()) renderer.drawLines(segments.data(), static_cast<int>(segments.size()), color, width);
    }

    std::string m_objPath{"data/ffMesh.obj"};
    std::shared_ptr<MeshObject> m_mesh;
    StressAnalyzer m_analyzer;
    MiqFieldRemesher m_miqRemesher;
    MiqRemeshResult m_miqResult;
    TensorField m_curvatureField;
    TensorField m_smoothedField;
    std::unique_ptr<SimpleUI> m_ui;
    std::vector<int> m_supportVertices;
    std::vector<int> m_loadVertices;
    std::string m_status{"loading"};
    std::string m_miqStatus;
    float m_spacing{0.25f};
    float m_lastSpacing{-1.0f};
    float m_smoothSteps{12.0f};
    int m_lastSmoothSteps{-1};
    bool m_drawStress{true};
    bool m_drawCrosses{true};
    bool m_drawMiqGrid{true};
    FieldSource m_fieldSource{FieldSource::Stress};
    float m_crossScale{0.055f};
};

ALICE2_REGISTER_SKETCH_AUTO(StressAlignedMiqSketch)

#endif

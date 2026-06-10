// #define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>

#include <algorithm>
#include <cstdio>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace alice2;

enum class StreamlineFieldMode {
    Stress,
    Curvature
};

class StressAlignedStreamlineSketch : public ISketch {
public:
    std::string getName() const override { return "Stress Aligned Streamlines"; }
    std::string getDescription() const override { return "Equal-spaced stress direction streamlines"; }
    std::string getAuthor() const override { return "alice2 User"; }

    void setup() override {
        scene().setBackgroundColor(Color(0.96f, 0.96f, 0.96f, 1.0f));
        scene().setShowGrid(false);
        scene().setShowAxes(false);
        scene().setAxesLength(1.0f);

        m_ui = std::make_unique<SimpleUI>(input());
        m_ui->addSlider("Spacing", Vec2{10.0f, 92.0f}, 180.0f, 0.02f, 10.0f, m_spacing);
        m_ui->addSlider("Primary Offset", Vec2{10.0f, 132.0f}, 180.0f, -1.0f, 1.0f, m_primaryOffset);
        m_ui->addSlider("Secondary Offset", Vec2{10.0f, 172.0f}, 180.0f, -1.0f, 1.0f, m_secondaryOffset);

        loadMesh();
        solveAndExtract();
    }

    void update(float) override {
        if (std::abs(m_spacing - m_lastSpacing) > 1e-4f ||
            std::abs(m_primaryOffset - m_lastPrimaryOffset) > 1e-4f ||
            std::abs(m_secondaryOffset - m_lastSecondaryOffset) > 1e-4f) {
            extract();
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

        if (m_drawStreamlines) {
            drawStreamlines(renderer);
        }

        renderer.setColor(Color(0.05f, 0.05f, 0.05f, 1.0f));
        renderer.drawString("m mode | c colour | x crosses | l streamlines | s smooth | o/p cross scale | r reload", 10.0f, 30.0f);
        renderer.drawString(m_status, 10.0f, 52.0f);
        if (m_ui) m_ui->draw(renderer);
    }

    bool onKeyPress(unsigned char key, int, int) override {
        switch (key) {
            case 'c':
            case 'C':
                m_drawColoredMesh = !m_drawColoredMesh;
                return true;
            case 'x':
            case 'X':
                m_drawCrosses = !m_drawCrosses;
                return true;
            case 's':
            case 'S':
                m_smoothingIterations += 2;
                solveAndExtract();
                return true;
            case 'l':
            case 'L':
                m_drawStreamlines = !m_drawStreamlines;
                return true;
            case 'o':
            case 'O':
                m_crossScale *= 1.2f;
                return true;
            case 'p':
            case 'P':
                m_crossScale /= 1.2f;
                return true;
            case 'm':
            case 'M':
                m_fieldMode = (m_fieldMode == StreamlineFieldMode::Stress) ? StreamlineFieldMode::Curvature : StreamlineFieldMode::Stress;
                extract();
                return true;
            case 'r':
            case 'R':
                loadMesh();
                solveAndExtract();
                return true;
            default:
                return false;
        }
    }

    bool onMousePress(int button, int state, int x, int y) override {
        return m_ui && m_ui->onMousePress(button, state, x, y);
    }

    bool onMouseMove(int x, int y) override {
        return m_ui && m_ui->onMouseMove(x, y);
    }

private:
    void loadMesh() {
        m_loadVertices.clear();
        m_mesh = std::make_shared<MeshObject>("slab_long");

        try {
            m_mesh->readFromObj(m_objPath);
            triangulateFaces();
            m_mesh->generateEdgesFromFaces();
            m_mesh->recalculateNormals();
            m_mesh->setShowFaces(false);

            auto data = m_mesh->getMeshData();
            if (data) {
                m_loadVertices.reserve(data->vertices.size());
                for (int i = 0; i < static_cast<int>(data->vertices.size()); ++i) {
                    m_loadVertices.push_back(i);
                }
            }
        } catch (const std::exception& e) {
            m_status = std::string("Failed to load ") + m_objPath + ": " + e.what();
            std::printf("[StressAligned] %s\n", m_status.c_str());
        }
    }

    bool hasMesh() const {
        auto data = m_mesh ? m_mesh->getMeshData() : nullptr;
        return data && !data->vertices.empty() && !data->faces.empty();
    }

    void triangulateFaces() {
        if (!hasMesh()) return;
        auto data = m_mesh->getMeshData();
        std::vector<MeshFace> triangles;
        triangles.reserve(data->faces.size());

        for (const MeshFace& face : data->faces) {
            if (face.vertices.size() == 3) {
                triangles.push_back(face);
                continue;
            }
            for (size_t i = 1; i + 1 < face.vertices.size(); ++i) {
                MeshFace tri({face.vertices[0], face.vertices[i], face.vertices[i + 1]}, face.normal, face.color);
                triangles.push_back(tri);
            }
        }

        data->faces = std::move(triangles);
        data->calculateNormals();
        data->triangulationDirty = true;
        m_mesh->generateEdgesFromFaces();
    }

    void solveAndExtract() {
        if (!hasMesh()) return;

        m_analyzer.clearBoundaryConditions();
        m_analyzer.clearForces();
        m_analyzer.setFixedVertices(m_columnVertexIds);
        m_analyzer.setForces(m_loadVertices, Vec3(0.0f, 0.0f, -m_loadMagnitude));
        m_analyzer.setFieldSmoothingIterations(m_smoothingIterations);
        m_analyzer.setStressMagnitudeThreshold(1e-8);

        if (!m_analyzer.solveVerticalSlab(*m_mesh)) {
            m_status = "Stress solve failed";
            std::printf("[StressAligned] %s\n", m_status.c_str());
            return;
        }

        m_analyzer.colorMeshByMagnitude(*m_mesh);
        buildCurvatureField();
        extract();
    }

    void extract() {
        if (!hasMesh()) return;
        m_remesher.setSpacing(m_spacing);
        m_remesher.setPrimaryOffset(m_primaryOffset);
        m_remesher.setSecondaryOffset(m_secondaryOffset);
        m_remesher.setMaxSteps(240);
        auto data = m_mesh->getMeshData();
        if (m_fieldMode == StreamlineFieldMode::Curvature) {
            if (m_curvatureField.empty()) buildCurvatureField();
            m_remesher.extractStreamlines(*data, m_curvatureField);
        } else {
            m_remesher.extractStreamlines(*m_mesh, m_analyzer);
        }
        m_lastSpacing = m_spacing;
        m_lastPrimaryOffset = m_primaryOffset;
        m_lastSecondaryOffset = m_secondaryOffset;

        const auto& lines = m_remesher.getStreamlines();
        char buffer[160];
        float maxStress = 0.0f;
        for (float value : m_analyzer.getStressMagnitudes()) maxStress = std::max(maxStress, value);
        std::snprintf(buffer,
                      sizeof(buffer),
                      "%s | smooth %d | spacing %.3f | offsets %.2f %.2f | primary %zu | secondary %zu | max stress %.5f",
                      fieldModeName(),
                      m_smoothingIterations,
                      m_spacing,
                      m_primaryOffset,
                      m_secondaryOffset,
                      lines.primary.size(),
                      lines.secondary.size(),
                      maxStress);
        m_status = buffer;
        std::printf("[StressAligned] %s\n", m_status.c_str());
    }

    void buildCurvatureField() {
        m_curvatureField.clear();
        if (!hasMesh()) return;

        auto data = m_mesh->getMeshData();
        auto curvature = m_mesh->principleCurvature(false);
        if (curvature.principalDirections.size() != data->vertices.size() ||
            curvature.otherDirections.size() != data->vertices.size()) {
            return;
        }

        m_curvatureField.resize(data->faces.size());
        for (int fi = 0; fi < static_cast<int>(data->faces.size()); ++fi) {
            const MeshFace& face = data->faces[fi];
            FaceStressTensor tensor;
            Vec3 major;
            Vec3 minor;
            bool hasMajor = false;
            bool hasMinor = false;
            float k1 = 0.0f;
            float k2 = 0.0f;
            int count = 0;

            for (int id : face.vertices) {
                if (id < 0 || id >= static_cast<int>(data->vertices.size())) continue;

                Vec3 d1 = curvature.principalDirections[id];
                Vec3 d2 = curvature.otherDirections[id];
                d1.z = 0.0f;
                d2.z = 0.0f;
                if (d1.lengthSquared() > 1e-10f) {
                    d1.normalize();
                    if (hasMajor && major.dot(d1) < 0.0f) d1 = -d1;
                    major += d1;
                    hasMajor = true;
                }
                if (d2.lengthSquared() > 1e-10f) {
                    d2.normalize();
                    if (hasMinor && minor.dot(d2) < 0.0f) d2 = -d2;
                    minor += d2;
                    hasMinor = true;
                }
                k1 += curvature.k1[id];
                k2 += curvature.k2[id];
                ++count;
            }

            if (major.lengthSquared() <= 1e-10f && face.vertices.size() >= 2) {
                major = data->vertices[face.vertices[1]].position - data->vertices[face.vertices[0]].position;
                major.z = 0.0f;
            }
            if (major.lengthSquared() <= 1e-10f) major = Vec3(1.0f, 0.0f, 0.0f);
            major.normalize();

            if (minor.lengthSquared() <= 1e-10f) minor = Vec3(-major.y, major.x, 0.0f);
            minor.z = 0.0f;
            if (minor.lengthSquared() <= 1e-10f) minor = Vec3(0.0f, 1.0f, 0.0f);
            minor.normalize();

            const float invCount = count > 0 ? 1.0f / static_cast<float>(count) : 1.0f;
            tensor.majorValue = k1 * invCount;
            tensor.minorValue = k2 * invCount;
            tensor.magnitude = std::abs(tensor.majorValue - tensor.minorValue);
            tensor.majorDirection = major;
            tensor.minorDirection = minor;
            m_curvatureField[fi] = tensor;
        }

        smoothTensorField(*data, m_curvatureField, m_smoothingIterations);
    }

    void smoothTensorField(const MeshData& mesh, TensorField& field, int iterations) const {
        if (iterations <= 0 || field.empty()) return;

        std::map<std::pair<int, int>, std::vector<int>> edgeFaces;
        for (int fi = 0; fi < static_cast<int>(mesh.faces.size()); ++fi) {
            const MeshFace& face = mesh.faces[fi];
            for (int i = 0; i < static_cast<int>(face.vertices.size()); ++i) {
                int a = face.vertices[i];
                int b = face.vertices[(i + 1) % static_cast<int>(face.vertices.size())];
                if (a > b) std::swap(a, b);
                edgeFaces[{a, b}].push_back(fi);
            }
        }

        std::vector<std::vector<int>> neighbors(mesh.faces.size());
        for (const auto& item : edgeFaces) {
            for (int a : item.second) {
                for (int b : item.second) {
                    if (a != b) neighbors[a].push_back(b);
                }
            }
        }

        for (int iter = 0; iter < iterations; ++iter) {
            TensorField next = field;
            const int count = std::min(static_cast<int>(mesh.faces.size()), static_cast<int>(field.size()));
            for (int fi = 0; fi < count; ++fi) {
                Vec3 sum = field[fi].majorDirection;
                if (sum.lengthSquared() <= 1e-10f) continue;

                for (int nb : neighbors[fi]) {
                    if (nb < 0 || nb >= count) continue;
                    Vec3 d = field[nb].majorDirection;
                    if (d.lengthSquared() <= 1e-10f) continue;
                    if (sum.dot(d) < 0.0f) d = -d;
                    sum += d;
                }

                if (sum.lengthSquared() <= 1e-10f) continue;
                Vec3 major = sum.normalized();
                major.z = 0.0f;
                if (major.lengthSquared() <= 1e-10f) continue;
                major.normalize();

                next[fi].majorDirection = major;
                next[fi].minorDirection = Vec3(-major.y, major.x, 0.0f);
            }
            field = next;
        }
    }

    const char* fieldModeName() const {
        return m_fieldMode == StreamlineFieldMode::Stress ? "stress" : "curvature";
    }

    Vec3 faceCenter(const MeshData& mesh, const MeshFace& face) const {
        Vec3 center;
        int count = 0;
        for (int id : face.vertices) {
            if (id < 0 || id >= static_cast<int>(mesh.vertices.size())) continue;
            center += mesh.vertices[id].position;
            ++count;
        }
        return count > 0 ? center / static_cast<float>(count) : center;
    }

    Color lerpColor(const Color& a, const Color& b, float t) const {
        t = std::clamp(t, 0.0f, 1.0f);
        return Color(a.r + (b.r - a.r) * t,
                     a.g + (b.g - a.g) * t,
                     a.b + (b.b - a.b) * t,
                     a.a + (b.a - a.a) * t);
    }

    void drawActiveField(Renderer& renderer) {
        StressAnalysisDrawSettings settings;
        settings.drawColoredMesh = m_drawColoredMesh;
        settings.drawMeshEdges = true;
        settings.drawBoundaryConditions = false;
        settings.drawCrossField = m_drawCrosses;
        settings.drawStreamlines = false;
        settings.crossScale = m_crossScale;
        settings.edgeColor = Color(0.78f, 0.78f, 0.78f, 1.0f);

        if (m_fieldMode == StreamlineFieldMode::Stress) {
            m_analyzer.draw(renderer, *m_mesh, settings);
            return;
        }

        if (m_curvatureField.empty()) buildCurvatureField();
        auto data = m_mesh->getMeshData();
        if (!data) return;

        if (settings.drawColoredMesh) {
            drawTensorColoredMesh(renderer, *data, m_curvatureField, settings);
        }
        if (settings.drawMeshEdges) {
            m_analyzer.drawMeshEdges(renderer, *data, settings.edgeColor, settings.edgeWidth);
        }
        if (settings.drawCrossField) {
            drawTensorCrosses(renderer, *data, m_curvatureField, settings);
        }
    }

    void drawTensorColoredMesh(Renderer& renderer,
                               const MeshData& mesh,
                               const TensorField& field,
                               const StressAnalysisDrawSettings& settings) const {
        if (field.empty()) return;

        float minMagnitude = std::numeric_limits<float>::max();
        float maxMagnitude = std::numeric_limits<float>::lowest();
        for (const auto& tensor : field.tensors()) {
            minMagnitude = std::min(minMagnitude, tensor.magnitude);
            maxMagnitude = std::max(maxMagnitude, tensor.magnitude);
        }
        const float range = std::max(1e-8f, maxMagnitude - minMagnitude);

        std::vector<Vec3> vertices;
        std::vector<Vec3> normals;
        std::vector<Color> colors;
        const int count = std::min(static_cast<int>(mesh.faces.size()), static_cast<int>(field.size()));
        for (int fi = 0; fi < count; ++fi) {
            const MeshFace& face = mesh.faces[fi];
            if (face.vertices.size() < 3) continue;

            const float t = (field[fi].magnitude - minMagnitude) / range;
            const Color color = lerpColor(settings.lowMagnitudeColor, settings.highMagnitudeColor, t);
            const Vec3 normal = mesh.calculateFaceNormal(face);
            for (size_t i = 1; i + 1 < face.vertices.size(); ++i) {
                int tri[3] = {face.vertices[0], face.vertices[i], face.vertices[i + 1]};
                for (int id : tri) {
                    if (id < 0 || id >= static_cast<int>(mesh.vertices.size())) continue;
                    vertices.push_back(mesh.vertices[id].position);
                    normals.push_back(normal);
                    colors.push_back(color);
                }
            }
        }

        if (!vertices.empty()) {
            renderer.drawMesh(vertices.data(), normals.data(), colors.data(), static_cast<int>(vertices.size()), nullptr, 0, false);
        }
    }

    void drawTensorCrosses(Renderer& renderer,
                           const MeshData& mesh,
                           const TensorField& field,
                           const StressAnalysisDrawSettings& settings) const {
        const int count = std::min(static_cast<int>(mesh.faces.size()), static_cast<int>(field.size()));
        for (int fi = 0; fi < count; ++fi) {
            const MeshFace& face = mesh.faces[fi];
            if (face.vertices.size() < 3) continue;

            const Vec3 center = faceCenter(mesh, face);
            Vec3 major = field[fi].majorDirection;
            Vec3 minor = field[fi].minorDirection;
            major.z = 0.0f;
            minor.z = 0.0f;
            if (major.lengthSquared() <= 1e-10f || minor.lengthSquared() <= 1e-10f) continue;
            major.normalize();
            minor.normalize();

            renderer.drawLine(center - major * settings.crossScale,
                              center + major * settings.crossScale,
                              settings.majorColor,
                              1.4f);
            renderer.drawLine(center - minor * settings.crossScale,
                              center + minor * settings.crossScale,
                              settings.minorColor,
                              1.0f);
        }
    }

    void drawStreamlines(Renderer& renderer) const {
        const auto& lines = m_remesher.getStreamlines();
        drawLineSet(renderer, lines.primary, Color(0.90f, 0.05f, 0.12f, 1.0f), 2.0f);
        drawLineSet(renderer, lines.secondary, Color(0.0f, 0.28f, 0.95f, 1.0f), 1.5f);
    }

    void drawLineSet(Renderer& renderer,
                     const std::vector<TensorStreamline>& lines,
                     const Color& color,
                     float width) const {
        std::vector<Vec3> segments;
        for (const TensorStreamline& line : lines) {
            for (size_t i = 0; i + 1 < line.size(); ++i) {
                segments.push_back(line[i]);
                segments.push_back(line[i + 1]);
            }
        }
        if (!segments.empty()) {
            renderer.drawLines(segments.data(), static_cast<int>(segments.size()), color, width);
        }
    }

    std::string m_objPath = "slab_long.obj";
    std::vector<int> m_columnVertexIds{0, 1, 2, 3};
    std::vector<int> m_loadVertices;
    std::shared_ptr<MeshObject> m_mesh;
    StressAnalyzer m_analyzer;
    StressAlignedRemesher m_remesher;
    TensorField m_curvatureField;
    std::unique_ptr<SimpleUI> m_ui;
    std::string m_status = "loading";
    StreamlineFieldMode m_fieldMode{StreamlineFieldMode::Stress};
    bool m_drawColoredMesh{true};
    bool m_drawCrosses{true};
    bool m_drawStreamlines{true};
    float m_spacing{1.0f};
    float m_lastSpacing{-1.0f};
    float m_primaryOffset{0.0f};
    float m_secondaryOffset{0.0f};
    float m_lastPrimaryOffset{999.0f};
    float m_lastSecondaryOffset{999.0f};
    float m_crossScale{0.055f};
    float m_loadMagnitude{0.03f};
    int m_smoothingIterations{15};
};

ALICE2_REGISTER_SKETCH_AUTO(StressAlignedStreamlineSketch)

#endif

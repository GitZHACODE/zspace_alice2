

#include <alice2.h>
#include <sketches/SketchRegistry.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace alice2;

class MiqCurvatureRemeshSketch : public ISketch {
public:
    std::string getName() const override { return "MIQ Curvature Grid"; }
    std::string getDescription() const override { return "Field-aligned MIQ grid from principal curvature directions"; }
    std::string getAuthor() const override { return "alice2 User"; }

    void setup() override {
        scene().setBackgroundColor(Color(0.96f, 0.96f, 0.96f, 1.0f));
        scene().setShowGrid(false);
        scene().setShowAxes(false);
        m_ui = std::make_unique<SimpleUI>(input());
        m_ui->addSlider("Grid Spacing", Vec2{10.0f, 92.0f}, 180.0f, 0.08f, 0.8f, m_spacing);
        rebuild();
    }

    void update(float) override {
        if (std::abs(m_spacing - m_lastSpacing) > 1e-4f) {
            runMiq();
            m_lastSpacing = m_spacing;
        }
    }

    void draw(Renderer& renderer, Camera&) override {
        if (!m_mesh || !m_mesh->getMeshData()) return;
        drawMeshEdges(renderer, *m_mesh->getMeshData());
        if (m_drawCrosses) drawCurvatureCrosses(renderer, *m_mesh->getMeshData());
        if (m_drawGrid) drawGrid(renderer);

        renderer.setColor(Color(0.05f, 0.05f, 0.05f, 1.0f));
        renderer.drawString("c curvature crosses | g MIQ grid | r rebuild curved patch", 10.0f, 30.0f);
        renderer.drawString(m_status, 10.0f, 52.0f);
        if (m_ui) m_ui->draw(renderer);
    }

    bool onKeyPress(unsigned char key, int, int) override {
        switch (key) {
            case 'c': case 'C':
                m_drawCrosses = !m_drawCrosses;
                return true;
            case 'g': case 'G':
                m_drawGrid = !m_drawGrid;
                return true;
            case 'r': case 'R':
                rebuild();
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
    void rebuild() {
        buildCurvedPatch();
        buildCurvatureField();
        runMiq();
        m_lastSpacing = m_spacing;
    }

    // An anisotropic paraboloid gives a stable, non-uniform principal-curvature
    // field while remaining the single-boundary patch supported by this spike.
    void buildCurvedPatch() {
        constexpr int columns = 27;
        constexpr int rows = 19;
        std::vector<Vec3> positions;
        std::vector<std::vector<int>> faces;
        positions.reserve(columns * rows);
        faces.reserve((columns - 1) * (rows - 1) * 2);

        for (int y = 0; y < rows; ++y) {
            const float v = static_cast<float>(y) / static_cast<float>(rows - 1);
            const float py = (v - 0.5f) * 2.8f;
            for (int x = 0; x < columns; ++x) {
                const float u = static_cast<float>(x) / static_cast<float>(columns - 1);
                const float px = (u - 0.5f) * 4.0f;
                const float pz = 0.24f * px * px + 0.055f * py * py;
                positions.emplace_back(px, py, pz);
            }
        }
        for (int y = 0; y + 1 < rows; ++y) {
            for (int x = 0; x + 1 < columns; ++x) {
                const int a = y * columns + x;
                const int b = a + 1;
                const int c = a + columns;
                const int d = c + 1;
                faces.push_back({a, b, d});
                faces.push_back({a, d, c});
            }
        }

        m_mesh = std::make_shared<MeshObject>("miq_curvature_patch");
        m_mesh->createFromVerticesAndFaces(positions, faces);
        m_mesh->generateEdgesFromFaces();
        m_mesh->recalculateNormals();
        m_mesh->setShowFaces(false);
    }

    void buildCurvatureField() {
        m_field.clear();
        auto data = m_mesh ? m_mesh->getMeshData() : nullptr;
        if (!data || data->faces.empty()) return;

        const auto curvature = m_mesh->principalCurvature(3, false);
        if (curvature.principalDirections.size() != data->vertices.size() ||
            curvature.otherDirections.size() != data->vertices.size()) {
            m_status = "Curvature calculation did not return a direction per vertex";
            return;
        }

        m_field.resize(data->faces.size());
        for (int fi = 0; fi < static_cast<int>(data->faces.size()); ++fi) {
            const MeshFace& face = data->faces[fi];
            const Vec3 normal = data->calculateFaceNormal(face).normalized();
            Vec3 major;
            float k1 = 0.0f;
            float k2 = 0.0f;
            int count = 0;
            for (int id : face.vertices) {
                if (id < 0 || id >= static_cast<int>(data->vertices.size())) continue;
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
            Vec3 minor = normal.cross(major).normalized();

            FaceStressTensor& tensor = m_field[fi];
            tensor.majorDirection = major;
            tensor.minorDirection = minor;
            if (count > 0) {
                tensor.majorValue = k1 / static_cast<float>(count);
                tensor.minorValue = k2 / static_cast<float>(count);
            }
            tensor.magnitude = std::abs(tensor.majorValue - tensor.minorValue);
        }
    }

    void runMiq() {
        auto data = m_mesh ? m_mesh->getMeshData() : nullptr;
        if (!data || m_field.empty()) return;
        MiqRemeshOptions options;
        options.targetSpacing = m_spacing;
        m_result = m_remesher.parameterize(*data, m_field, options);
        m_status = m_result.success ? "MIQ curvature grid: " + m_result.diagnostic
                                    : "MIQ rejected curvature field: " + m_result.diagnostic;
        std::printf("[MiqCurvature] %s\n", m_status.c_str());
    }

    void drawMeshEdges(Renderer& renderer, const MeshData& mesh) const {
        std::vector<Vec3> segments;
        segments.reserve(mesh.faces.size() * 6);
        for (const MeshFace& face : mesh.faces) {
            if (face.vertices.size() != 3) continue;
            for (int i = 0; i < 3; ++i) {
                segments.push_back(mesh.vertices[face.vertices[i]].position);
                segments.push_back(mesh.vertices[face.vertices[(i + 1) % 3]].position);
            }
        }
        if (!segments.empty()) renderer.drawLines(segments.data(), static_cast<int>(segments.size()), Color(0.72f, 0.72f, 0.72f, 1.0f), 0.7f);
    }

    void drawCurvatureCrosses(Renderer& renderer, const MeshData& mesh) const {
        const int count = std::min(static_cast<int>(mesh.faces.size()), static_cast<int>(m_field.size()));
        for (int fi = 0; fi < count; fi += 4) {
            const MeshFace& face = mesh.faces[fi];
            Vec3 center;
            for (int id : face.vertices) center += mesh.vertices[id].position;
            center /= static_cast<float>(face.vertices.size());
            const float scale = 0.08f;
            renderer.drawLine(center - m_field[fi].majorDirection * scale, center + m_field[fi].majorDirection * scale, Color(0.90f, 0.05f, 0.12f, 1.0f), 1.2f);
            renderer.drawLine(center - m_field[fi].minorDirection * scale, center + m_field[fi].minorDirection * scale, Color(0.0f, 0.28f, 0.95f, 1.0f), 1.0f);
        }
    }

    void drawGrid(Renderer& renderer) const {
        drawLineSet(renderer, m_result.gridLines.u, Color(0.10f, 0.72f, 0.18f, 1.0f), 1.8f);
        drawLineSet(renderer, m_result.gridLines.v, Color(0.95f, 0.52f, 0.02f, 1.0f), 1.6f);
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

    std::shared_ptr<MeshObject> m_mesh;
    TensorField m_field;
    MiqFieldRemesher m_remesher;
    MiqRemeshResult m_result;
    std::unique_ptr<SimpleUI> m_ui;
    std::string m_status{"building curvature field"};
    float m_spacing{0.28f};
    float m_lastSpacing{-1.0f};
    bool m_drawCrosses{true};
    bool m_drawGrid{true};
};

ALICE2_REGISTER_SKETCH_AUTO(MiqCurvatureRemeshSketch)

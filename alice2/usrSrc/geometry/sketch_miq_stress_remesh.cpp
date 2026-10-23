

#include <alice2.h>
#include <sketches/SketchRegistry.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace alice2;

class MiqStressRemeshSketch : public ISketch {
public:
    std::string getName() const override { return "MIQ Stress Grid"; }
    std::string getDescription() const override { return "Field-aligned MIQ grid from a plane-stress cross field"; }
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
        if (m_drawCrosses) drawStressCrosses(renderer, *m_mesh->getMeshData());
        if (m_drawGrid) drawGrid(renderer);

        renderer.setColor(Color(0.05f, 0.05f, 0.05f, 1.0f));
        renderer.drawString("c stress crosses | g MIQ grid | r resolve cantilever", 10.0f, 30.0f);
        renderer.drawString(m_status, 10.0f, 52.0f);
        if (m_ui) m_ui->draw(renderer);
    }

    bool onKeyPress(unsigned char key, int, int) override {
        switch (key) {
            case 'c': case 'C': m_drawCrosses = !m_drawCrosses; return true;
            case 'g': case 'G': m_drawGrid = !m_drawGrid; return true;
            case 'r': case 'R': rebuild(); return true;
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
    void rebuild() {
        buildCantilever();
        solveStress();
        runMiq();
        m_lastSpacing = m_spacing;
    }

    void buildCantilever() {
        constexpr int columns = 33;
        constexpr int rows = 17;
        std::vector<Vec3> positions;
        std::vector<std::vector<int>> faces;
        m_fixedVertices.clear();
        m_loadedVertices.clear();
        positions.reserve(columns * rows);
        faces.reserve((columns - 1) * (rows - 1) * 2);

        for (int y = 0; y < rows; ++y) {
            const float py = -1.0f + 2.0f * static_cast<float>(y) / static_cast<float>(rows - 1);
            for (int x = 0; x < columns; ++x) {
                const float px = 4.0f * static_cast<float>(x) / static_cast<float>(columns - 1);
                const int id = static_cast<int>(positions.size());
                positions.emplace_back(px, py, 0.0f);
                if (x == 0) m_fixedVertices.push_back(id);
                if (x == columns - 1) m_loadedVertices.push_back(id);
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
        m_mesh = std::make_shared<MeshObject>("miq_stress_cantilever");
        m_mesh->createFromVerticesAndFaces(positions, faces);
        m_mesh->generateEdgesFromFaces();
        m_mesh->recalculateNormals();
        m_mesh->setShowFaces(false);
    }

    void solveStress() {
        m_analyzer.clearBoundaryConditions();
        m_analyzer.clearForces();
        m_analyzer.setFixedVertices(m_fixedVertices);
        m_analyzer.setForces(m_loadedVertices, Vec3(0.0f, -0.008f, 0.0f));
        m_analyzer.setFieldSmoothingIterations(8);
        m_analyzer.setStressMagnitudeThreshold(1e-8);
        if (!m_analyzer.solveLinearPlaneStress(*m_mesh)) {
            m_status = "Plane-stress solve failed";
        }
    }

    void runMiq() {
        auto data = m_mesh ? m_mesh->getMeshData() : nullptr;
        if (!data || m_analyzer.getSmoothedCrossField().empty()) return;
        MiqRemeshOptions options;
        options.targetSpacing = m_spacing;
        m_result = m_remesher.parameterize(*data, m_analyzer.getSmoothedCrossField(), options);
        m_status = m_result.success ? "MIQ stress grid: " + m_result.diagnostic
                                    : "MIQ rejected stress field: " + m_result.diagnostic;
        std::printf("[MiqStress] %s\n", m_status.c_str());
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

    void drawStressCrosses(Renderer& renderer, const MeshData& mesh) const {
        const TensorField& field = m_analyzer.getSmoothedCrossField();
        const int count = std::min(static_cast<int>(mesh.faces.size()), static_cast<int>(field.size()));
        for (int fi = 0; fi < count; fi += 4) {
            const MeshFace& face = mesh.faces[fi];
            Vec3 center;
            for (int id : face.vertices) center += mesh.vertices[id].position;
            center /= static_cast<float>(face.vertices.size());
            const float scale = 0.08f;
            renderer.drawLine(center - field[fi].majorDirection * scale, center + field[fi].majorDirection * scale, Color(0.90f, 0.05f, 0.12f, 1.0f), 1.2f);
            renderer.drawLine(center - field[fi].minorDirection * scale, center + field[fi].minorDirection * scale, Color(0.0f, 0.28f, 0.95f, 1.0f), 1.0f);
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
    StressAnalyzer m_analyzer;
    MiqFieldRemesher m_remesher;
    MiqRemeshResult m_result;
    std::unique_ptr<SimpleUI> m_ui;
    std::vector<int> m_fixedVertices;
    std::vector<int> m_loadedVertices;
    std::string m_status{"solving plane stress"};
    float m_spacing{0.28f};
    float m_lastSpacing{-1.0f};
    bool m_drawCrosses{true};
    bool m_drawGrid{true};
};

ALICE2_REGISTER_SKETCH_AUTO(MiqStressRemeshSketch)

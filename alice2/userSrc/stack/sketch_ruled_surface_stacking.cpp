// #define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>
#include "stack/RuledSurfaceStackSolver.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <memory>
#include <random>
#include <sstream>

using namespace alice2;
using namespace alice2::stack;

namespace {
Vec3 toVec3(const Eigen::Vector3d& p) {
    return Vec3(static_cast<float>(p.x()), static_cast<float>(p.y()), static_cast<float>(p.z()));
}
std::string fixed(double value, int precision = 3) {
    std::ostringstream out; out << std::fixed << std::setprecision(precision) << value; return out.str();
}
} // namespace

class RuledSurfaceStackingSketch final : public ISketch {
public:
    std::string getName() const override { return "Ruled Surface Fast Stack"; }
    std::string getDescription() const override { return "Exact direct-Z ruled-surface placement with interval constraints"; }
    std::string getAuthor() const override { return "alice2 User"; }

    void setup() override {
        scene().setBackgroundColor(Color(1.0f, 1.0f, 1.0f));
        scene().setShowGrid(true); scene().setGridSize(14.0f); scene().setGridDivisions(14);
        scene().setShowAxes(true); scene().setAxesLength(2.0f);
        m_ui = std::make_unique<SimpleUI>(input());
        m_ui->setTheme(SimpleUI::UITheme::Dark);
        m_ui->addSlider("Surfaces", Vec2{14, 94}, 240.0f, 2.0f, 50.0f, m_surfaceCount);
        m_ui->addSlider("Planes per surface", Vec2{14, 124}, 240.0f, 2.0f, 24.0f, m_planeCount);
        m_ui->addSlider("Random variation", Vec2{14, 154}, 240.0f, 0.0f, 1.0f, m_randomVariation);
        m_ui->addSlider("Length variation", Vec2{14, 184}, 240.0f, 0.0f, 1.0f, m_lengthVariation);
        m_ui->addSlider("Ruling turn", Vec2{14, 214}, 240.0f, 0.0f, 1.0f, m_rulingTurn);
        m_ui->addToggle("Optimise physical flips", UIRect{14, 248, 220, 22}, m_optimiseFlips);
        m_ui->addToggle("Hot-wire collision", UIRect{14, 276, 190, 22}, m_hotWireCollision);
        m_ui->addToggle("Show face normals", UIRect{14, 304, 190, 22}, m_showFaceNormals);
        rebuild();
    }

    void update(float) override {
        const bool geometryChanged = m_surfaceCount != m_lastSurfaceCount || m_planeCount != m_lastPlaneCount ||
            m_randomVariation != m_lastRandomVariation || m_lengthVariation != m_lastLengthVariation ||
            m_rulingTurn != m_lastRulingTurn;
        if (geometryChanged) { m_geometryDirty = true; syncUiState(); }
        if (m_optimiseFlips != m_lastOptimiseFlips || m_hotWireCollision != m_lastHotWireCollision) {
            m_solverDirty = true; syncUiState();
        }
        if (m_geometryDirty || m_solverDirty) rebuild();
    }

    void draw(Renderer& renderer, Camera&) override {
        drawSurfaces(renderer);
        renderer.setColor(Color(0.94f, 0.94f, 0.96f));
        renderer.drawString("Ruled Surface Stacking — direct Z interval solver", 14, 24);
        renderer.drawString("r rebuild / re-solve    n new random seed", 14, 46);
        renderer.drawString(m_status, 14, 68);
        if (m_ui) m_ui->draw(renderer);
    }

    bool onKeyPress(unsigned char key, int, int) override {
        if (key == 'r' || key == 'R') { m_solverDirty = true; return true; }
        if (key == 'n' || key == 'N') {
            ++m_seed; m_geometryDirty = true; return true;
        }
        return false;
    }
    bool onMousePress(int button, int state, int x, int y) override {
        return m_ui && m_ui->onMousePress(button, state, x, y);
    }
    bool onMouseMove(int x, int y) override { return m_ui && m_ui->onMouseMove(x, y); }

private:
    void syncUiState() {
        m_lastSurfaceCount = m_surfaceCount; m_lastPlaneCount = m_planeCount;
        m_lastRandomVariation = m_randomVariation; m_lastLengthVariation = m_lengthVariation;
        m_lastRulingTurn = m_rulingTurn; m_lastOptimiseFlips = m_optimiseFlips;
        m_lastHotWireCollision = m_hotWireCollision;
    }
    void rebuild() {
        try {
            if (m_geometryDirty || m_surfaces.empty()) {
                RuledSurfaceProceduralSettings generator;
                generator.surfaceCount = static_cast<int>(std::lround(m_surfaceCount));
                generator.planesPerSurface = static_cast<int>(std::lround(m_planeCount));
                generator.randomVariation = m_randomVariation;
                generator.lengthVariation = m_lengthVariation;
                generator.rulingTurn = m_rulingTurn;
                generator.seed = m_seed;
                m_surfaces = makeProceduralRuledSurfaces(generator);
                const auto group = ruledSurfaceGroupBoundsXY(m_surfaces);
                const double scale = std::max({group.max.x() - group.min.x(), group.max.y() - group.min.y(), 1.0});
                m_geometrySettings.geomEpsilon = 1e-10 * scale;
                m_geometrySettings.mergeEpsilon = 1e-9 * scale;
                m_geometrySettings.clearance = 0.06;
                RuledSurfaceBounds2D foam = group;
                foam.min.array() -= 0.05 * scale; foam.max.array() += 0.05 * scale;
                m_variants = makeStackSurfaceVariants(m_surfaces, foam, m_geometrySettings.geomEpsilon);
                m_geometryStats = {};
                m_pairs = buildPairConstraintData(m_variants, m_geometrySettings, &m_geometryStats);
                m_geometryDirty = false;
            }
            m_solverSettings.numericalEpsilon = std::max(1e-10, m_geometrySettings.geomEpsilon);
            m_solverSettings.optimiseFlips = m_optimiseFlips;
            m_solverSettings.useHotWireCollision = m_hotWireCollision;
            m_solverStats = {};
            m_solution = solveRuledSurfaceStackFast(m_variants, m_pairs, m_solverSettings, &m_solverStats);
            m_solverDirty = false;
            m_status = "height: " + fixed(m_solution.totalHeight) +
                "  exact-Z: " + (m_solution.exactForOrientationState ? "yes" : "no") +
                "  flip-search: " + (m_optimiseFlips ? "local+pair" : "off") +
                "  nodes: " + std::to_string(m_solverStats.nodesVisited) +
                "  pruned: " + std::to_string(m_solverStats.boundPrunes) +
                "  clipped: " + std::to_string(m_geometryStats.trianglePairClipped);
            syncUiState();
        } catch (const std::exception& error) {
            m_status = std::string("Solver error: ") + error.what();
            m_geometryDirty = m_solverDirty = false;
        }
    }
    void drawSurfaces(Renderer& renderer) const {
        if (!m_solution.feasible) return;
        for (size_t i = 0; i < m_variants.size(); ++i) {
            const bool flipped = i < m_solution.flippedBySurface.size() && m_solution.flippedBySurface[i];
            const auto& surface = flipped ? *m_variants[i].flipped : m_variants[i].normal;
            const double z = m_solution.zBySurface[i];
            const float hue = static_cast<float>((i * 37) % 100) / 100.0f;
            const Color color(0.25f + 0.60f * hue, 0.76f - 0.36f * hue, 0.95f - 0.45f * hue, 0.72f);
            for (const auto& face : surface.geometry.faces) {
                std::array<Vec3, 4> vertices;
                for (int k = 0; k < 4; ++k) {
                    Eigen::Vector3d p = face.vertices[k]; p.z() += z; vertices[k] = toVec3(p);
                }
                renderer.drawQuad(vertices[0], vertices[1], vertices[2], vertices[3], color);
                renderer.drawLine(vertices[0], vertices[1], Color(0.04f, 0.04f, 0.05f), 1.0f);
                renderer.drawLine(vertices[1], vertices[2], Color(0.04f, 0.04f, 0.05f), 1.0f);
                renderer.drawLine(vertices[2], vertices[3], Color(0.04f, 0.04f, 0.05f), 1.0f);
                renderer.drawLine(vertices[3], vertices[0], Color(0.04f, 0.04f, 0.05f), 1.0f);
                if (m_showFaceNormals) {
                    Eigen::Vector3d c = 0.25 * (face.vertices[0] + face.vertices[1] + face.vertices[2] + face.vertices[3]);
                    c.z() += z;
                    renderer.drawLine(toVec3(c), toVec3(c + 0.35 * face.plane.normal), Color(1.0f, 0.8f, 0.15f), 1.0f);
                }
            }
        }
    }

    std::unique_ptr<SimpleUI> m_ui;
    float m_surfaceCount = 18.0f, m_planeCount = 8.0f, m_randomVariation = 0.35f;
    float m_lengthVariation = 0.35f, m_rulingTurn = 0.45f;
    bool m_optimiseFlips = false, m_hotWireCollision = true, m_showFaceNormals = false;
    float m_lastSurfaceCount = m_surfaceCount, m_lastPlaneCount = m_planeCount;
    float m_lastRandomVariation = m_randomVariation, m_lastLengthVariation = m_lengthVariation, m_lastRulingTurn = m_rulingTurn;
    bool m_lastOptimiseFlips = m_optimiseFlips, m_lastHotWireCollision = m_hotWireCollision;
    std::uint32_t m_seed = 1;
    bool m_geometryDirty = true, m_solverDirty = true;
    std::vector<RuledSurface> m_surfaces;
    std::vector<SurfaceOrientationVariants> m_variants;
    std::vector<PairConstraintData> m_pairs;
    StackGeometrySettings m_geometrySettings;
    StackSolveSettings m_solverSettings;
    StackGeometryStats m_geometryStats;
    StackSolveStats m_solverStats;
    RuledSurfaceStackSolution m_solution;
    std::string m_status;
};

ALICE2_REGISTER_SKETCH_AUTO(RuledSurfaceStackingSketch)

#endif // __MAIN__

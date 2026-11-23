#define __MAIN__
#ifdef __MAIN__

#include <zspace/interface.h>
#include <zspace/io.h>
#include <zspace/toolsets.h>

#include <alice2.h>
#include <sketches/SketchRegistry.h>
#include <utils/OpenGL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace alice2;

namespace {

constexpr const char* kExportUsdaPath      = "data/carbcomn_design_output.usda";
constexpr const char* kClipboardUsdaPath   = "data/carbcomn_clipboard.usda";
constexpr const char* kExportPrecastObj    = "data/carbcomn_precast_beams.obj";
constexpr const char* kExport3DPSlabObj    = "data/carbcomn_3dp_slabs.obj";
constexpr const char* kExportCantObj       = "data/carbcomn_cantilever.obj";
constexpr const char* kExportColObj        = "data/carbcomn_columns.obj";
constexpr const char* kExportCableJson     = "data/carbcomn_tension_cables.json";

std::string trimQuotesAndWhitespace(const std::string& raw)
{
    std::string s = raw;
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n' || s.front() == '"')) {
        s.erase(s.begin());
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n' || s.back() == '"')) {
        s.pop_back();
    }
    return s;
}

void drawFilledPanel2D(float x, float y, float w, float h, const Color& fill, const Color& border, float borderW = 2.0f)
{
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_LINE_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);

    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, vp[2], vp[3], 0, -1, 1);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glColor4f(fill.r, fill.g, fill.b, fill.a);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();

    glLineWidth(borderW);
    glColor4f(border.r, border.g, border.b, border.a);
    glBegin(GL_LINE_LOOP);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopAttrib();
}

Vec3 toAliceVec3(const zSpace::zPoint& p)
{
    return Vec3(p.x, p.y, p.z);
}

Color toAliceColor(const zSpace::zColor& c)
{
    return Color(c.r, c.g, c.b, c.a);
}

Color evalDirectionalLitColor(const Vec3& n, const Color& baseCol)
{
    const Vec3 keyDir(-0.48f, -0.58f, 0.66f);
    const Vec3 fillDir(0.62f, 0.35f, 0.70f);
    const float kDot = std::max(0.0f, n.x * keyDir.x + n.y * keyDir.y + n.z * keyDir.z);
    const float fDot = std::max(0.0f, n.x * fillDir.x + n.y * fillDir.y + n.z * fillDir.z);
    const float shade = std::min(1.12f, 0.44f + 0.48f * kDot + 0.20f * fDot);
    return Color(
        std::min(1.0f, baseCol.r * shade),
        std::min(1.0f, baseCol.g * shade),
        std::min(1.0f, baseCol.b * shade),
        baseCol.a);
}

void drawLitSolidZSpaceMesh(
    Renderer& renderer,
    zSpace::zObjectMesh& mesh,
    const Color& baseColor,
    bool showEdges,
    const Color& edgeColor,
    float edgeWidth = 1.25f,
    bool translucent = false)
{
    zSpace::zFnMesh fn(mesh);
    if (fn.numVertices() == 0 || fn.numPolygons() == 0) return;

    std::vector<Vec3> tris;
    std::vector<Color> triCols;
    tris.reserve(static_cast<std::size_t>(fn.numPolygons() * 6));
    triCols.reserve(static_cast<std::size_t>(fn.numPolygons() * 6));

    for (zSpace::zItMeshFace face(mesh); !face.end(); face++) {
        zSpace::zPointArray pts;
        face.getVertexPositions(pts);
        if (pts.size() < 3) continue;

        const Vec3 p0 = toAliceVec3(pts[0]);
        const Vec3 p1 = toAliceVec3(pts[1]);
        const Vec3 pLast = toAliceVec3(pts.back());
        const Vec3 e1 = p1 - p0;
        const Vec3 e2 = pLast - p0;
        Vec3 n(
            e1.y * e2.z - e1.z * e2.y,
            e1.z * e2.x - e1.x * e2.z,
            e1.x * e2.y - e1.y * e2.x);
        const float nLen = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (nLen > 1e-7f) {
            n.x /= nLen;
            n.y /= nLen;
            n.z /= nLen;
        }
        const Color lit = evalDirectionalLitColor(n, baseColor);

        for (std::size_t i = 1; i + 1 < pts.size(); ++i) {
            tris.push_back(p0);
            tris.push_back(toAliceVec3(pts[i]));
            tris.push_back(toAliceVec3(pts[i + 1]));
            triCols.push_back(lit);
            triCols.push_back(lit);
            triCols.push_back(lit);
        }
    }

    if (!tris.empty()) {
        if (translucent) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
        }
        renderer.drawMesh(tris.data(), nullptr, triCols.data(), static_cast<int>(tris.size()), nullptr, 0, false);
        if (translucent) {
            glDepthMask(GL_TRUE);
        }
    }

    if (showEdges) {
        for (zSpace::zItMeshEdge edge(mesh); !edge.end(); edge++) {
            zSpace::zPointArray ep;
            edge.getVertexPositions(ep);
            if (ep.size() == 2) {
                renderer.drawLine(toAliceVec3(ep[0]), toAliceVec3(ep[1]), edgeColor, edgeWidth);
            }
        }
    }
}

void drawColoredZSpaceGraph(
    Renderer& renderer,
    zSpace::zObjectGraph& graph,
    float lineWidth,
    bool showNodes,
    float nodeSize,
    float zLift = 0.0f)
{
    zSpace::zFnGraph fn(graph);
    if (fn.numVertices() == 0 || fn.numEdges() == 0) return;

    zSpace::zColorArray edgeCols;
    fn.getEdgeColors(edgeCols);
    const Vec3 lift(0.0f, 0.0f, zLift);

    for (zSpace::zItGraphEdge edge(graph); !edge.end(); edge++) {
        zSpace::zPointArray pts;
        edge.getVertexPositions(pts);
        if (pts.size() != 2) continue;
        const int eid = edge.getId();
        const Color col = (eid >= 0 && eid < static_cast<int>(edgeCols.size()))
            ? toAliceColor(edgeCols[static_cast<std::size_t>(eid)])
            : Color(0.12f, 0.44f, 0.96f, 1.0f);
        renderer.drawLine(toAliceVec3(pts[0]) + lift, toAliceVec3(pts[1]) + lift, col, lineWidth);
    }

    if (showNodes) {
        zSpace::zColorArray vertCols;
        fn.getVertexColors(vertCols);
        for (zSpace::zItGraphVertex v(graph); !v.end(); v++) {
            const int vid = v.getId();
            const Color col = (vid >= 0 && vid < static_cast<int>(vertCols.size()))
                ? toAliceColor(vertCols[static_cast<std::size_t>(vid)])
                : Color(0.95f, 0.85f, 0.25f, 1.0f);
            renderer.drawPoint(toAliceVec3(v.getPosition()) + lift, col, nodeSize);
        }
    }
}

std::string fmtDouble(double v, int decimals = 2)
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(decimals) << v;
    return oss.str();
}

} // namespace

class zSpaceCarbcomnDesignSketch : public ISketch {
public:
    std::string getName() const override { return "zSpace Carbcomn Design"; }
    std::string getDescription() const override { return "Stepped Columns + Precast Concrete Beams + 3DP Concrete Slabs + Cantilever & Tension Cable"; }
    std::string getAuthor() const override { return "zSpace"; }

    void setup() override
    {
        scene().setBackgroundColor(Color(0.32f, 0.33f, 0.35f, 1.0f));
        scene().setShowGrid(false);
        scene().setShowAxes(false);

        buildUI();

        if (const char* envCapture = std::getenv("CARBCOMN_DESIGN_CAPTURE_DIR")) {
            if (envCapture[0] != '\0') {
                m_captureDir = envCapture;
                std::filesystem::create_directories(m_captureDir);
                m_captureStep = 0;
            }
        }

        loadPreset(0);
    }

    void update(float /*deltaTime*/) override
    {
        auto& p = m_carbcomn.params();

        const int targetNumBaysX  = std::max(1, static_cast<int>(std::lround(m_numBaysXUI)));
        const int targetNumBaysY  = std::max(1, static_cast<int>(std::lround(m_numBaysYUI)));
        const double targetBayX   = std::max(0.25, static_cast<double>(m_bayXUI));
        const double targetBayY   = std::max(0.25, static_cast<double>(m_bayYUI));
        const double targetOffX   = std::max(0.0, static_cast<double>(m_offsetXUI));
        const double targetOffY   = std::max(0.0, static_cast<double>(m_offsetYUI));

        const double targetColW   = std::max(0.04, static_cast<double>(m_colWidthUI));
        const double targetColL   = std::max(0.06, static_cast<double>(m_colLengthUI));
        const double targetPreW   = std::max(0.04, static_cast<double>(m_precastWidthUI));
        const double targetPreH   = std::max(0.04, static_cast<double>(m_precastHeightUI));
        const double targetSlabT  = std::max(0.02, static_cast<double>(m_slabThickUI));
        const double targetCantH  = std::max(0.04, static_cast<double>(m_cantileverHeightUI));

        bool lvlChanged = false;
        if (!p.levels.empty()) {
            const int nLvls = static_cast<int>(p.levels.size());
            m_selectedLevel = std::max(0, std::min(nLvls - 1, m_selectedLevel));
            auto& sel = p.levels[static_cast<std::size_t>(m_selectedLevel)];

            if (sel.isHalfModule != m_selectedLevelHalfUI) {
                sel.isHalfModule = m_selectedLevelHalfUI;
                lvlChanged = true;
            }
            if (sel.hasCantilever != m_selectedLevelCantUI) {
                sel.hasCantilever = m_selectedLevelCantUI;
                lvlChanged = true;
            }
            if (sel.hasOffset != m_selectedLevelOffsetUI) {
                sel.hasOffset = m_selectedLevelOffsetUI;
                lvlChanged = true;
            }
            if (sel.hasPrecast != m_selectedLevelPrecastUI) {
                sel.hasPrecast = m_selectedLevelPrecastUI;
                lvlChanged = true;
            }

            const bool selZChanged  = std::abs(m_selectedLevelZUI - m_lastSelectedZUI) > 1e-4f;
            const bool stepZChanged = std::abs(m_nextStepZUI - m_lastStepZUI) > 1e-4f;

            if (selZChanged) {
                const double minAllowedZ = (m_selectedLevel > 0)
                    ? (p.levels[static_cast<std::size_t>(m_selectedLevel - 1)].z + 0.05)
                    : 0.0;
                const double newSelZ = std::max(minAllowedZ, static_cast<double>(m_selectedLevelZUI));
                const double dz = newSelZ - sel.z;
                sel.z = newSelZ;
                for (int k = m_selectedLevel + 1; k < nLvls; ++k) {
                    p.levels[static_cast<std::size_t>(k)].z += dz;
                }
                m_selectedLevelZUI = static_cast<float>(newSelZ);
                m_lastSelectedZUI = m_selectedLevelZUI;
                lvlChanged = true;
            }

            if (stepZChanged) {
                const double newStep = std::max(0.10, static_cast<double>(m_nextStepZUI));
                if (m_selectedLevel + 1 < nLvls) {
                    const double curStep =
                        p.levels[static_cast<std::size_t>(m_selectedLevel + 1)].z - sel.z;
                    const double dStep = newStep - curStep;
                    for (int k = m_selectedLevel + 1; k < nLvls; ++k) {
                        p.levels[static_cast<std::size_t>(k)].z += dStep;
                    }
                    lvlChanged = true;
                } else if (m_selectedLevel > 0) {
                    sel.z = p.levels[static_cast<std::size_t>(m_selectedLevel - 1)].z + newStep;
                    lvlChanged = true;
                }
                m_lastStepZUI = m_nextStepZUI;
            }
        }

        const bool baysCountChanged = (targetNumBaysX != p.numBaysX) || (targetNumBaysY != p.numBaysY);

        const bool geomChanged =
            lvlChanged ||
            baysCountChanged ||
            (std::abs(targetBayX  - p.bayX)             > 1e-5) ||
            (std::abs(targetBayY  - p.bayY)             > 1e-5) ||
            (std::abs(targetOffX  - p.offsetX)          > 1e-5) ||
            (std::abs(targetOffY  - p.offsetY)          > 1e-5) ||
            (std::abs(targetColW  - p.columnWidth)      > 1e-5) ||
            (std::abs(targetColL  - p.columnLength)     > 1e-5) ||
            (std::abs(targetPreW  - p.precastWidth)     > 1e-5) ||
            (std::abs(targetPreH  - p.precastHeight)    > 1e-5) ||
            (std::abs(targetSlabT - p.slabThickness)    > 1e-5) ||
            (std::abs(targetCantH - p.cantileverHeight) > 1e-5) ||
            (m_angledCorners != p.angledCorners);

        if (geomChanged) {
            p.numBaysX         = targetNumBaysX;
            p.numBaysY         = targetNumBaysY;
            p.bayX             = targetBayX;
            p.bayY             = targetBayY;
            p.offsetX          = targetOffX;
            p.offsetY          = targetOffY;
            p.columnWidth      = targetColW;
            p.columnLength     = targetColL;
            p.precastWidth     = targetPreW;
            p.precastHeight    = targetPreH;
            p.slabThickness    = targetSlabT;
            p.cantileverHeight = targetCantH;
            p.angledCorners    = m_angledCorners;

            m_carbcomn.compute();
            syncSelectedLevelFromComputed();
            updateStatusStrings();
            if (baysCountChanged) {
                frameCameraOnGeometry();
            }
        }
    }

    void draw(Renderer& renderer, Camera& /*cam*/) override
    {
        drawGeometry(renderer);
        drawHUD(renderer);

        if (m_ui) {
            m_ui->draw(renderer);
        }

        if (m_captureStep >= 0) {
            processCaptureFrame();
        }
    }

    bool onKeyPress(unsigned char key, int /*x*/, int /*y*/) override
    {
        if (input().isCtrlPressed()) {
            if (key == 'c' || key == 'C') {
                copyUsd();
                return true;
            }
            if (key == 'v' || key == 'V') {
                pasteUsd();
                return true;
            }
        }

        switch (key) {
            case 'b':
            case 'B':
                m_view3DTabIdx = (m_view3DTabIdx == 0) ? 1 : 0;
                return true;
            case 'c':
            case 'C':
                m_angledCorners = !m_angledCorners;
                return true;
            case 'k':
            case 'K':
                m_showColumns = !m_showColumns;
                return true;
            case 'p':
            case 'P':
                toggleSelectedLevelPrecast();
                return true;
            case 'o':
            case 'O':
                toggleSelectedLevelOffset();
                return true;
            case 'd':
            case 'D':
                m_show3DPSlab = !m_show3DPSlab;
                return true;
            case 'v':
            case 'V':
                toggleSelectedLevelCantilever();
                return true;
            case 'g':
            case 'G':
                m_showTensionCables = !m_showTensionCables;
                return true;
            case '+':
            case '=':
            case 'a':
            case 'A':
                addNextSlab(/*isHalf=*/false);
                return true;
            case 'h':
            case 'H':
                addNextSlab(/*isHalf=*/true);
                return true;
            case '-':
            case '_':
                removeTopSlab();
                return true;
            case 'l':
            case 'L':
            case ']':
                selectNextLevel(1);
                return true;
            case '[':
                selectNextLevel(-1);
                return true;
            case 't':
            case 'T':
                toggleSelectedLevelHalf();
                return true;
            case 's':
            case 'S':
                m_showUpperSlabMeshes = !m_showUpperSlabMeshes;
                return true;
            case 'm':
            case 'M':
                loadPreset((m_presetIndex + 1) % 3);
                return true;
            case 'r':
            case 'R':
                loadPreset(m_presetIndex);
                return true;
            case 'u':
            case 'U':
                exportUsd();
                return true;
            case 'e':
            case 'E':
                exportMeshesAndGraphs();
                return true;
            case 'f':
            case 'F':
                frameCameraOnGeometry();
                return true;
            default:
                return false;
        }
    }

    bool onFileDrop(const std::vector<std::string>& paths) override
    {
        if (paths.empty()) return false;
        return loadExternalUsdFile(paths.front(), "Dropped USD");
    }

    bool onMousePress(int button, int state, int x, int y) override
    {
        return m_ui && m_ui->onMousePress(button, state, x, y);
    }

    bool onMouseMove(int x, int y) override
    {
        return m_ui && m_ui->onMouseMove(x, y);
    }

private:
    void buildUI()
    {
        m_ui = std::make_unique<SimpleUI>(input());
        m_ui->setTheme(SimpleUI::UITheme::Light);

        const float btnW = 196.0f;
        const float btnH = 22.0f;
        const float rowStep = 25.0f;
        const float colGap = 14.0f;

        const float xCol1 = 14.0f;
        const float xCol2 = xCol1 + btnW + colGap;
        const float yStart = 114.0f;
        const float sliderW = 104.0f;
        const float sliderStep = 31.0f;

        // Column 1: Slab Level Actions + USD Copy/Paste/Export + Bay Grid & Offset Sliders
        float y1 = yStart;
        m_ui->addButton("Add Full Slab [A/+]", UIRect{ xCol1, y1, btnW, btnH }, [this]() {
            addNextSlab(false);
        });
        y1 += rowStep;

        m_ui->addButton("Add Half Slab [H]", UIRect{ xCol1, y1, btnW, btnH }, [this]() {
            addNextSlab(true);
        });
        y1 += rowStep;

        m_ui->addButton("Remove Top Slab [-]", UIRect{ xCol1, y1, btnW, btnH }, [this]() {
            removeTopSlab();
        });
        y1 += rowStep;

        m_ui->addButton("Cycle Level [L]", UIRect{ xCol1, y1, btnW, btnH }, [this]() {
            selectNextLevel(1);
        });
        y1 += rowStep;

        m_ui->addButton("Copy USD [Ctrl+C]", UIRect{ xCol1, y1, btnW, btnH }, [this]() {
            copyUsd();
        });
        y1 += rowStep;

        m_ui->addButton("Paste USD [Ctrl+V]", UIRect{ xCol1, y1, btnW, btnH }, [this]() {
            pasteUsd();
        });
        y1 += rowStep;

        m_ui->addButton("Export USD [U]", UIRect{ xCol1, y1, btnW, btnH }, [this]() {
            exportUsd();
        });
        y1 += rowStep;

        m_ui->addButton("Export OBJ/JSON [E]", UIRect{ xCol1, y1, btnW, btnH }, [this]() {
            exportMeshesAndGraphs();
        });
        y1 += rowStep;

        m_ui->addButton("Switch Preset [M]", UIRect{ xCol1, y1, btnW, btnH }, [this]() {
            loadPreset((m_presetIndex + 1) % 3);
        });
        y1 += rowStep;

        m_ui->addButton("Frame Camera [F]", UIRect{ xCol1, y1, btnW, btnH }, [this]() {
            frameCameraOnGeometry();
        });

        float ys1 = y1 + btnH + 20.0f;
        m_ui->addSlider("Num Bays X",   Vec2{ xCol1, ys1 }, sliderW, 1.0f, 6.0f, m_numBaysXUI,   1.0f);
        ys1 += sliderStep;
        m_ui->addSlider("Num Bays Y",   Vec2{ xCol1, ys1 }, sliderW, 1.0f, 6.0f, m_numBaysYUI,   1.0f);
        ys1 += sliderStep;
        m_ui->addSlider("Bay bx",       Vec2{ xCol1, ys1 }, sliderW, 0.50f, 8.00f, m_bayXUI,      0.01f);
        ys1 += sliderStep;
        m_ui->addSlider("Bay by",       Vec2{ xCol1, ys1 }, sliderW, 0.50f, 8.00f, m_bayYUI,      0.01f);
        ys1 += sliderStep;
        m_ui->addSlider("Offset X",     Vec2{ xCol1, ys1 }, sliderW, 0.00f, 1.50f, m_offsetXUI,   0.01f);
        ys1 += sliderStep;
        m_ui->addSlider("Offset Y",     Vec2{ xCol1, ys1 }, sliderW, 0.00f, 1.50f, m_offsetYUI,   0.01f);
        ys1 += sliderStep;
        m_ui->addSlider("Col Width x",  Vec2{ xCol1, ys1 }, sliderW, 0.05f, 0.60f, m_colWidthUI,  0.01f);
        ys1 += sliderStep;
        m_ui->addSlider("Col Length y", Vec2{ xCol1, ys1 }, sliderW, 0.10f, 1.00f, m_colLengthUI, 0.01f);

        // Column 2: 3D Components ON/OFF Tab + Selected Level Toggles + Part Toggles + Sliders
        float y2 = yStart;
        m_ui->addButtonGroup(
            { "3D ON [B]", "3D OFF" },
            Vec2{ xCol2, y2 },
            (btnW - 4.0f) * 0.5f,
            btnH,
            4.0f,
            m_view3DTabIdx);
        y2 += rowStep;

        m_ui->addToggle("Lvl Half Slab [T]", UIRect{ xCol2, y2, btnW, btnH }, m_selectedLevelHalfUI);
        y2 += rowStep;

        m_ui->addToggle("Lvl Offset XY [O]", UIRect{ xCol2, y2, btnW, btnH }, m_selectedLevelOffsetUI);
        y2 += rowStep;

        m_ui->addToggle("Lvl Precast [P]", UIRect{ xCol2, y2, btnW, btnH }, m_selectedLevelPrecastUI);
        y2 += rowStep;

        m_ui->addToggle("Lvl Cantilever [V]", UIRect{ xCol2, y2, btnW, btnH }, m_selectedLevelCantUI);
        y2 += rowStep;

        m_ui->addToggle("Tension Cable [G]", UIRect{ xCol2, y2, btnW, btnH }, m_showTensionCables);
        y2 += rowStep;

        m_ui->addToggle("Columns [K]", UIRect{ xCol2, y2, btnW, btnH }, m_showColumns);
        y2 += rowStep;

        m_ui->addToggle("3DP Slab [D]", UIRect{ xCol2, y2, btnW, btnH }, m_show3DPSlab);
        y2 += rowStep;

        m_ui->addToggle("Angled Corners [C]", UIRect{ xCol2, y2, btnW, btnH }, m_angledCorners);
        y2 += rowStep;

        m_ui->addToggle("Upper Slabs [S]", UIRect{ xCol2, y2, btnW, btnH }, m_showUpperSlabMeshes);

        float ys2 = y2 + btnH + 20.0f;
        m_ui->addSlider("Precast W",     Vec2{ xCol2, ys2 }, sliderW, 0.05f,  0.80f, m_precastWidthUI,     0.01f);
        ys2 += sliderStep;
        m_ui->addSlider("Precast Hz",    Vec2{ xCol2, ys2 }, sliderW, 0.05f,  0.60f, m_precastHeightUI,    0.01f);
        ys2 += sliderStep;
        m_ui->addSlider("3DP Slab dz",   Vec2{ xCol2, ys2 }, sliderW, 0.04f,  0.50f, m_slabThickUI,        0.01f);
        ys2 += sliderStep;
        m_ui->addSlider("Cantilever Hz", Vec2{ xCol2, ys2 }, sliderW, 0.05f,  0.60f, m_cantileverHeightUI, 0.01f);
        ys2 += sliderStep;
        m_ui->addSlider("Selected Z",    Vec2{ xCol2, ys2 }, sliderW, 0.00f, 15.00f, m_selectedLevelZUI,   0.01f);
        ys2 += sliderStep;
        m_ui->addSlider("Next Step dZ",  Vec2{ xCol2, ys2 }, sliderW, 0.20f,  6.00f, m_nextStepZUI,        0.01f);
    }

    void loadPreset(int index)
    {
        m_presetIndex = ((index % 3) + 3) % 3;
        if (m_presetIndex == 0) {
            m_carbcomn.createDefaultPreset();
            m_presetLabel = "Default 2x1 Bays (0 / 0.9 Half / 3.5 Full+Off+Cant)";
            m_showUpperSlabMeshes = true;
            m_selectedLevel = 2;
            m_nextStepZUI = 2.60f;
        } else if (m_presetIndex == 1) {
            m_carbcomn.createSingleStoreyPreset();
            m_presetLabel = "Single Storey 2x1 Bays (0 -> 2.6 Full+Off+Cant)";
            m_showUpperSlabMeshes = true;
            m_selectedLevel = 1;
            m_nextStepZUI = 2.60f;
        } else {
            m_carbcomn.createMultiLevelPreset();
            m_presetLabel = "2-Module Stack (0 / 0.9 / 3.5 / 4.4 / 7.0+Off+Cant)";
            m_showUpperSlabMeshes = true;
            m_selectedLevel = 4;
            m_nextStepZUI = 2.60f;
        }

        syncUIFromParams();
        syncSelectedLevelFromComputed();
        updateStatusStrings();
        frameCameraOnGeometry();
    }

    void syncUIFromParams()
    {
        const auto& p = m_carbcomn.params();
        m_numBaysXUI         = static_cast<float>(p.numBaysX);
        m_numBaysYUI         = static_cast<float>(p.numBaysY);
        m_bayXUI             = static_cast<float>(p.bayX);
        m_bayYUI             = static_cast<float>(p.bayY);
        m_offsetXUI          = static_cast<float>(p.offsetX);
        m_offsetYUI          = static_cast<float>(p.offsetY);
        m_colWidthUI         = static_cast<float>(p.columnWidth);
        m_colLengthUI        = static_cast<float>(p.columnLength);
        m_precastWidthUI     = static_cast<float>(p.precastWidth);
        m_precastHeightUI    = static_cast<float>(p.precastHeight);
        m_slabThickUI        = static_cast<float>(p.slabThickness);
        m_cantileverHeightUI = static_cast<float>(p.cantileverHeight);
        m_angledCorners      = p.angledCorners;
    }

    void syncSelectedLevelFromComputed()
    {
        const auto& lvls = m_carbcomn.computedLevels();
        m_carbcomn.params().levels = lvls;
        if (lvls.empty()) {
            m_selectedLevel = 0;
            m_selectedLevelZUI = 0.0f;
            m_lastSelectedZUI = 0.0f;
            m_lastStepZUI = m_nextStepZUI;
            m_selectedLevelHalfUI = false;
            m_selectedLevelCantUI = false;
            m_selectedLevelOffsetUI = false;
            m_selectedLevelPrecastUI = false;
            return;
        }
        const int nLvls = static_cast<int>(lvls.size());
        m_selectedLevel = std::max(0, std::min(nLvls - 1, m_selectedLevel));
        const auto& sel = lvls[static_cast<std::size_t>(m_selectedLevel)];
        m_selectedLevelZUI       = static_cast<float>(sel.z);
        m_selectedLevelHalfUI    = sel.isHalfModule;
        m_selectedLevelCantUI    = sel.hasCantilever;
        m_selectedLevelOffsetUI  = sel.hasOffset;
        m_selectedLevelPrecastUI = sel.hasPrecast;

        if (m_selectedLevel + 1 < nLvls) {
            m_nextStepZUI = static_cast<float>(
                lvls[static_cast<std::size_t>(m_selectedLevel + 1)].z -
                lvls[static_cast<std::size_t>(m_selectedLevel)].z);
        } else if (m_selectedLevel > 0) {
            m_nextStepZUI = static_cast<float>(
                lvls[static_cast<std::size_t>(m_selectedLevel)].z -
                lvls[static_cast<std::size_t>(m_selectedLevel - 1)].z);
        }
        m_lastSelectedZUI = m_selectedLevelZUI;
        m_lastStepZUI = m_nextStepZUI;
    }

    void selectNextLevel(int delta)
    {
        const int n = static_cast<int>(m_carbcomn.params().levels.size());
        if (n <= 0) return;
        m_selectedLevel = ((m_selectedLevel + delta) % n + n) % n;
        syncSelectedLevelFromComputed();
        updateStatusStrings();
    }

    void toggleSelectedLevelHalf()
    {
        if (m_carbcomn.params().levels.empty()) return;
        m_selectedLevelHalfUI = !m_selectedLevelHalfUI;
    }

    void toggleSelectedLevelCantilever()
    {
        if (m_carbcomn.params().levels.empty()) return;
        m_selectedLevelCantUI = !m_selectedLevelCantUI;
    }

    void toggleSelectedLevelOffset()
    {
        if (m_carbcomn.params().levels.empty()) return;
        m_selectedLevelOffsetUI = !m_selectedLevelOffsetUI;
    }

    void toggleSelectedLevelPrecast()
    {
        if (m_carbcomn.params().levels.empty()) return;
        m_selectedLevelPrecastUI = !m_selectedLevelPrecastUI;
    }

    void addNextSlab(bool isHalf)
    {
        double topZ = -static_cast<double>(m_nextStepZUI);
        for (const auto& l : m_carbcomn.params().levels) {
            topZ = std::max(topZ, l.z);
        }
        const double newZ = (m_carbcomn.params().levels.empty())
            ? 0.0
            : (topZ + std::max(0.2, static_cast<double>(m_nextStepZUI)));

        m_carbcomn.addLevel(newZ, isHalf);
        m_carbcomn.compute();
        m_selectedLevel = std::max(0, m_carbcomn.numComputedLevels() - 1);
        syncSelectedLevelFromComputed();
        updateStatusStrings();
        frameCameraOnGeometry();
    }

    void removeTopSlab()
    {
        if (m_carbcomn.params().levels.size() <= 1) return;
        m_carbcomn.removeTopLevel();
        m_carbcomn.compute();
        m_selectedLevel = std::min(m_selectedLevel, std::max(0, m_carbcomn.numComputedLevels() - 1));
        syncSelectedLevelFromComputed();
        updateStatusStrings();
    }

    void drawLevelPerimeterAndLabels(Renderer& renderer)
    {
        const auto& lvls = m_carbcomn.computedLevels();
        auto& slabMeshes = m_carbcomn.levelSlabMeshes();
        const std::size_t numLvls = std::min(lvls.size(), slabMeshes.size());
        if (numLvls == 0) return;

        const Color activeRingCol(0.10f, 0.96f, 0.72f, 1.0f);   // Bright Neon Cyan-Green for active level
        const Color inactiveLblCol(0.84f, 0.88f, 0.94f, 0.92f);

        glDisable(GL_DEPTH_TEST);

        for (std::size_t k = 0; k < numLvls; ++k) {
            if (k > 0 && !m_showUpperSlabMeshes) continue;

            zSpace::zFnMesh fn(slabMeshes[k]);
            zSpace::zPointArray pts;
            zSpace::zIntArray connects, counts;
            fn.getVertexPositions(pts);
            fn.getPolygonData(connects, counts);
            if (pts.empty() || counts.empty()) continue;

            const bool isSel = (static_cast<int>(k) == m_selectedLevel);

            // Anchor 3D level label at the front-right (+X, -Y) corner on the open side of the viewport (away from left UI)
            std::size_t cornerIdx = 0;
            float bestScore = -1e30f;
            for (std::size_t v = 0; v < pts.size(); ++v) {
                const float score = pts[v].x - pts[v].y;
                if (score > bestScore) {
                    bestScore = score;
                    cornerIdx = v;
                }
            }

            if (isSel) {
                std::map<std::pair<int, int>, int> edgeUseCount;
                std::map<std::pair<int, int>, std::pair<int, int>> edgeOrigVerts;
                std::size_t off = 0;
                for (int c : counts) {
                    for (int i = 0; i < c; ++i) {
                        const int u = connects[off + static_cast<std::size_t>(i)];
                        const int v = connects[off + static_cast<std::size_t>((i + 1) % c)];
                        const auto key = std::minmax(u, v);
                        edgeUseCount[key]++;
                        edgeOrigVerts[key] = { u, v };
                    }
                    off += static_cast<std::size_t>(c);
                }

                const Vec3 lift(0.0f, 0.0f, 0.008f);
                for (const auto& kv : edgeUseCount) {
                    if (kv.second != 1) continue; // Only outer perimeter boundary edges!
                    const auto& uv = edgeOrigVerts[kv.first];
                    const Vec3 a = toAliceVec3(pts[static_cast<std::size_t>(uv.first)]) + lift;
                    const Vec3 b = toAliceVec3(pts[static_cast<std::size_t>(uv.second)]) + lift;
                    renderer.drawLine(a, b, activeRingCol, 4.2f);
                    renderer.drawPoint(a, activeRingCol, 7.5f);
                }
            }

            const Vec3 anchor = toAliceVec3(pts[cornerIdx]);
            const Vec3 labelPos = anchor + Vec3(0.32f, -0.28f, 0.06f);
            std::string tag = "L" + std::to_string(k) + " (z=" + fmtDouble(lvls[k].z, 2) + "m)";
            if (isSel) {
                tag = "<< SELECTED L" + std::to_string(k) + " z=" + fmtDouble(lvls[k].z, 2) + "m";
                renderer.drawLine(anchor + Vec3(0.0f, 0.0f, 0.008f), labelPos, activeRingCol, 2.2f);
                renderer.setColor(activeRingCol);
                renderer.drawText(tag, labelPos, 0.92f);
            } else {
                renderer.setColor(inactiveLblCol);
                renderer.drawText(tag, labelPos, 0.74f);
            }
        }

        glEnable(GL_DEPTH_TEST);
    }

    void drawGeometry(Renderer& renderer)
    {
        const auto& lvls = m_carbcomn.computedLevels();
        const std::size_t numLvls = lvls.size();
        const Color stdEdgeCol(0.16f, 0.20f, 0.24f, 1.0f);
        const bool show3D = (m_view3DTabIdx == 0);

        // 1. 3D Solid Components (controlled by the `3D ON [B] | 3D OFF` tab)
        if (show3D) {
            if (m_showColumns) {
                auto& colMeshes = m_carbcomn.columnSolidMeshes();
                const auto& isHalfCol = m_carbcomn.verticalEdgeIsHalf();
                for (std::size_t s = 0; s < colMeshes.size(); ++s) {
                    const bool half = (s < isHalfCol.size()) && (isHalfCol[s] != 0);
                    const Color colBase = half
                        ? Color(0.86f, 0.62f, 0.58f, 1.0f)
                        : Color(0.72f, 0.79f, 0.88f, 1.0f);
                    drawLitSolidZSpaceMesh(renderer, colMeshes[s], colBase, true, stdEdgeCol, 1.30f);
                }
            }

            if (m_showPrecast) {
                auto& preMeshes = m_carbcomn.levelPrecastMeshes();
                for (std::size_t k = 0; k < preMeshes.size() && k < numLvls; ++k) {
                    if (k > 0 && !m_showUpperSlabMeshes) continue;
                    drawLitSolidZSpaceMesh(
                        renderer,
                        preMeshes[k],
                        Color(0.50f, 0.62f, 0.76f, 1.0f),
                        true,
                        stdEdgeCol,
                        1.25f);
                }
            }

            if (m_show3DPSlab) {
                auto& s3dMeshes = m_carbcomn.level3DPSlabMeshes();
                for (std::size_t k = 0; k < s3dMeshes.size() && k < numLvls; ++k) {
                    if (k > 0 && !m_showUpperSlabMeshes) continue;
                    const Color sCol = lvls[k].isHalfModule
                        ? Color(0.86f, 0.76f, 0.73f, 1.0f)
                        : Color(0.82f, 0.81f, 0.77f, 1.0f);
                    drawLitSolidZSpaceMesh(
                        renderer,
                        s3dMeshes[k],
                        sCol,
                        true,
                        stdEdgeCol,
                        1.25f);
                }
            }
        }

        // 2. Tension Cables (inside cantilever cross-sectional center)
        if (m_showTensionCables) {
            auto& cabGraphs = m_carbcomn.levelTensionCableGraphs();
            for (std::size_t k = 0; k < cabGraphs.size() && k < numLvls; ++k) {
                if (k > 0 && !m_showUpperSlabMeshes) continue;
                drawColoredZSpaceGraph(renderer, cabGraphs[k], 3.4f, true, 5.5f, 0.0f);
            }
        }

        // 3. Cantilever Overhang Solids (when 3D ON)
        if (show3D) {
            auto& cantMeshes = m_carbcomn.levelCantileverMeshes();
            const float cantAlpha = m_showTensionCables ? 0.56f : 1.0f;
            for (std::size_t k = 0; k < cantMeshes.size() && k < numLvls; ++k) {
                if (k > 0 && !m_showUpperSlabMeshes) continue;
                drawLitSolidZSpaceMesh(
                    renderer,
                    cantMeshes[k],
                    Color(0.90f, 0.60f, 0.32f, cantAlpha),
                    true,
                    Color(0.45f, 0.26f, 0.12f, 0.90f),
                    1.20f,
                    m_showTensionCables);
            }
        }

        // 4. ALWAYS-VISIBLE Structural Centerline Graphs (BOTH Horizontal Beams AND Vertical Columns!)
        //    Drawn with depth test disabled after 3D solids so vertical & horizontal graphs remain visible at all times.
        glDisable(GL_DEPTH_TEST);
        drawColoredZSpaceGraph(renderer, m_carbcomn.horizontalGraph(), show3D ? 2.5f : 3.2f, !show3D, 5.5f, 0.002f);
        drawColoredZSpaceGraph(renderer, m_carbcomn.verticalGraph(),   show3D ? 2.6f : 3.4f, true,    5.5f, 0.0f);
        glEnable(GL_DEPTH_TEST);

        // 5. Active Level 3D Perimeter Ring + 3D Level Billboard Labels
        drawLevelPerimeterAndLabels(renderer);
    }

    void drawHUD(Renderer& renderer)
    {
        const auto& rep = m_carbcomn.lastReport();
        const auto& lvls = m_carbcomn.computedLevels();

        renderer.setColor(Color(0.95f, 0.96f, 0.98f, 1.0f));
        renderer.drawString(
            "zTsCarbcomnDesign  |  Bays: " + std::to_string(rep.numBaysX) + " x " + std::to_string(rep.numBaysY) +
            " (" + fmtDouble(rep.bayX, 2) + " x " + fmtDouble(rep.bayY, 2) + " m)" +
            "  |  Offset: " + fmtDouble(rep.offsetX, 2) + " x " + fmtDouble(rep.offsetY, 2) + " m" +
            "  ->  Total: " + fmtDouble(rep.totalFootprintX, 2) + " x " + fmtDouble(rep.totalFootprintY, 2) + " m",
            14.0f, 22.0f);

        renderer.setColor(Color(0.62f, 0.86f, 1.0f, 1.0f));
        renderer.drawString(m_metricsLine, 14.0f, 42.0f);

        renderer.setColor(Color(0.90f, 0.92f, 0.94f, 1.0f));
        renderer.drawString(m_levelsLine, 14.0f, 62.0f);

        renderer.setColor(Color(0.76f, 0.80f, 0.84f, 1.0f));
        renderer.drawString(
            "Keys: [B] 3D ON/OFF  [L] Cycle Level  [O] Lvl Offset  [P] Lvl Precast  [V] Lvl Cantilever  [T] Half/Full  [Ctrl+C/V] USD  [M] Preset",
            14.0f, 82.0f);

        renderer.setColor(Color(0.84f, 0.88f, 0.92f, 1.0f));
        renderer.drawString("Levels, USD & Bays", 14.0f, 104.0f);
        renderer.drawString("3D Tab & Active Level Controls", 224.0f, 104.0f);

        // Prominent Active Level Banner Box (top-right) so cycling levels [L] is immediately unmistakable
        if (!lvls.empty()) {
            const int selIdx = std::max(0, std::min(static_cast<int>(lvls.size()) - 1, m_selectedLevel));
            const auto& sel = lvls[static_cast<std::size_t>(selIdx)];
            const std::string bannerText =
                "SELECTED [L]: L" + std::to_string(selIdx) + "/" + std::to_string(lvls.size() - 1) +
                " z=" + fmtDouble(sel.z, 2) + "m  |  " +
                (sel.isHalfModule ? "HALF" : "FULL") +
                "  |  OFFSET:" + (sel.hasOffset ? "ON" : "OFF") +
                "  |  PRECAST:" + (sel.hasPrecast ? "ON" : "OFF") +
                "  |  CANT:" + (sel.hasCantilever ? "ON" : "OFF");

            GLint vp[4] = { 0, 0, 1280, 720 };
            glGetIntegerv(GL_VIEWPORT, vp);
            const float boxW = 650.0f;
            const float boxH = 30.0f;
            const float boxX = std::max(436.0f, static_cast<float>(vp[2]) - boxW - 16.0f);
            const float boxY = 92.0f;
            drawFilledPanel2D(
                boxX,
                boxY,
                boxW,
                boxH,
                Color(0.06f, 0.16f, 0.18f, 0.88f),
                Color(0.10f, 0.96f, 0.72f, 1.0f),
                2.2f);
            renderer.setColor(Color(0.20f, 1.00f, 0.80f, 1.0f));
            renderer.drawString(bannerText, boxX + 12.0f, boxY + 20.0f);
        }

        // Color Legend bottom-left
        renderer.setColor(Color(0.10f, 0.96f, 0.72f, 1.0f));
        renderer.drawString("Cyan Ring = Active Selected Level [L]", 14.0f, 646.0f);
        renderer.setColor(Color(0.55f, 0.72f, 0.90f, 1.0f));
        renderer.drawString("Blue-Grey = Precast  |  Sand-Grey = 3DP Slab", 14.0f, 664.0f);
        renderer.setColor(Color(0.96f, 0.68f, 0.34f, 1.0f));
        renderer.drawString("Amber + Gold = Cantilever + Tension Cable", 14.0f, 682.0f);
    }

    void updateStatusStrings()
    {
        const auto& rep = m_carbcomn.lastReport();
        const auto& p = m_carbcomn.params();
        m_metricsLine =
            "Col: " + fmtDouble(p.columnWidth, 2) + "x" + fmtDouble(p.columnLength, 2) + "m" +
            "  |  Precast: " + fmtDouble(p.precastWidth, 2) + "x" + fmtDouble(p.precastHeight, 2) + "m (" + std::to_string(rep.totalPrecastFaces) + "f)" +
            "  |  3DP dz: " + fmtDouble(p.slabThickness, 2) + "m (" + std::to_string(rep.total3DPSlabFaces) + "f)" +
            "  |  Cantilever dz: " + fmtDouble(p.cantileverHeight, 2) + "m (" + std::to_string(rep.totalCantileverFaces) + "f)" +
            "  |  Graph: " + std::to_string(rep.centerlineGraphEdges) + "e + " + std::to_string(rep.tensionCableEdges) + " cables";

        const auto& lvls = m_carbcomn.computedLevels();
        std::ostringstream oss;
        oss << "Slabs (" << lvls.size() << "): ";
        for (std::size_t i = 0; i < lvls.size(); ++i) {
            if (i > 0) oss << "  ->  ";
            if (static_cast<int>(i) == m_selectedLevel) oss << ">> [";
            oss << "L" << i << " z=" << fmtDouble(lvls[i].z, 2) << "m ("
                << (lvls[i].isHalfModule ? "Half" : "Full")
                << (lvls[i].hasPrecast ? "+Pre" : "")
                << (lvls[i].hasOffset ? "+Off" : "")
                << (lvls[i].hasCantilever ? "+Cant" : "") << ")";
            if (static_cast<int>(i) == m_selectedLevel) oss << "] <<";
        }
        if (!m_exportNote.empty()) {
            oss << "   |   " << m_exportNote;
        }
        m_levelsLine = oss.str();
    }

    void copyUsd()
    {
        auto* app = Application::getInstance();
        if (!app) return;

        const bool include3DSolids = (m_view3DTabIdx == 0);
        const bool include2DSlabs  = (m_view3DTabIdx != 0);
        const std::string usda = m_carbcomn.exportToUSDA(include3DSolids, /*includeGraphs=*/true, include2DSlabs);

        std::filesystem::create_directories("data");
        m_carbcomn.writeUSDA(kClipboardUsdaPath, include3DSolids, /*includeGraphs=*/true, include2DSlabs);

        if (app->setClipboardText(usda)) {
            const auto& rep = m_carbcomn.lastReport();
            m_exportNote = std::string("Copied #usda 1.0 (") +
                (include3DSolids ? "3D Meshes + Graphs: " : "2D Slabs + Graphs: ") +
                std::to_string(rep.centerlineGraphEdges) + " graph edges)";
        } else {
            m_exportNote = "Failed to access system clipboard";
        }
        updateStatusStrings();
    }

    bool loadExternalUsdFile(const std::string& filePath, const char* sourceLabel)
    {
        if (m_carbcomn.loadFromFile(filePath)) {
            syncUIFromParams();
            m_selectedLevel = std::max(0, m_carbcomn.numComputedLevels() - 1);
            syncSelectedLevelFromComputed();
            m_exportNote = std::string(sourceLabel) + ": loaded '" +
                std::filesystem::path(filePath).filename().string() + "'";
            updateStatusStrings();
            frameCameraOnGeometry();
            return true;
        }
        m_exportNote = std::string(sourceLabel) + ": failed to load USD file";
        updateStatusStrings();
        return false;
    }

    void pasteUsd()
    {
        auto* app = Application::getInstance();
        if (!app) return;

        const std::string clipText = app->getClipboardText();
        if (!clipText.empty() && zSpace::zTsCarbcomnDesign::isUSDAClipboardText(clipText)) {
            if (m_carbcomn.loadFromUSDA(clipText)) {
                syncUIFromParams();
                m_selectedLevel = std::max(0, m_carbcomn.numComputedLevels() - 1);
                syncSelectedLevelFromComputed();
                m_exportNote = "Pasted #usda 1.0 stage from clipboard (" +
                    std::to_string(m_carbcomn.numComputedLevels()) + " levels)";
                updateStatusStrings();
                frameCameraOnGeometry();
                return;
            }
        }

        const std::string trimmed = trimQuotesAndWhitespace(clipText);
        if (!trimmed.empty() && std::filesystem::exists(trimmed)) {
            loadExternalUsdFile(trimmed, "Pasted path");
            return;
        }

        const std::string copiedFile = app->getClipboardFilePath();
        if (!copiedFile.empty() && std::filesystem::exists(copiedFile)) {
            loadExternalUsdFile(copiedFile, "Pasted Explorer USD");
            return;
        }

        m_exportNote = "Clipboard has no #usda 1.0 text or USD file path";
        updateStatusStrings();
    }

    void exportUsd()
    {
        std::filesystem::create_directories("data");
        const bool include3DSolids = (m_view3DTabIdx == 0);
        const bool include2DSlabs  = (m_view3DTabIdx != 0);
        if (m_carbcomn.writeUSDA(kExportUsdaPath, include3DSolids, /*includeGraphs=*/true, include2DSlabs)) {
            m_exportNote = std::string("Saved ") + kExportUsdaPath;
        } else {
            m_exportNote = "Failed to write USDA";
        }
        updateStatusStrings();
    }

    void exportMeshesAndGraphs()
    {
        std::filesystem::create_directories("data");
        const auto r1 = m_carbcomn.writeCombinedPrecastMesh(kExportPrecastObj);
        const auto r2 = m_carbcomn.writeCombined3DPSlabMesh(kExport3DPSlabObj);
        const auto r3 = m_carbcomn.writeCombinedCantileverMesh(kExportCantObj);
        const auto r4 = m_carbcomn.writeCombinedColumnMesh(kExportColObj);
        const auto r5 = m_carbcomn.writeTensionCableGraph(kExportCableJson);
        if (r1 && r2 && r3 && r4 && r5) {
            m_exportNote = "Saved precast/3DP/cantilever/columns OBJ + cables JSON";
        } else {
            m_exportNote = "Partial export error";
        }
        updateStatusStrings();
    }

    void frameCameraOnGeometry(float distanceScale = 1.85f)
    {
        const auto& rep = m_carbcomn.lastReport();
        const double hx = 0.5 * std::max(1.0, rep.totalFootprintX);
        const double hy = 0.5 * std::max(1.0, rep.totalFootprintY);
        const float minZ = static_cast<float>(rep.minZ);
        const float maxZ = static_cast<float>(std::max(rep.maxZ, rep.minZ + 1.0));

        const Vec3 mn(static_cast<float>(-hx), static_cast<float>(-hy), minZ);
        const Vec3 mx(static_cast<float>(hx),  static_cast<float>(hy),  maxZ);
        const Vec3 size = mx - mn;
        const float maxDim = std::max({ size.x, size.y, size.z, 2.0f });
        const float diag = std::max(2.0f, size.length());

        camera().setNearPlane(std::max(0.05f, diag * 0.002f));
        camera().setFarPlane(std::max(2000.0f, diag * 25.0f));
        if (auto* app = Application::getInstance()) {
            auto& ctrl = app->getCameraController();
            ctrl.focusOnBounds(mn, mx);
            ctrl.setOrbitDistance(maxDim * distanceScale);
            const Vec3 shifted = ctrl.getOrbitCenter() - camera().getRight() * (maxDim * 0.16f);
            ctrl.setOrbitCenter(shifted);
        }
    }

    void processCaptureFrame()
    {
        auto* app = Application::getInstance();
        if (!app) return;
        if (m_captureWarmup < 2) { ++m_captureWarmup; return; }

        const auto saveShot = [&](const char* name) {
            const std::string outPath = (std::filesystem::path(m_captureDir) / name).string();
            app->saveScreenshotToFile(outPath);
        };

        switch (m_captureStep) {
            case 0:
                saveShot("01_3d_on_graphs_visible_L2_selected.png");
                // Cycle to Mid Half Level L1 to verify active level ring + 3D label + top-right banner
                selectNextLevel(-1);
                break;
            case 1:
                saveShot("02_cycled_to_L1_active_level_highlight.png");
                // Switch 3D Components OFF tab -> Graph Only (horizontal + vertical + active level ring)
                m_view3DTabIdx = 1;
                break;
            case 2:
                saveShot("03_3d_off_tab_graph_always_visible.png");
                // Test Copy USD + Paste USD round-trip and switch back to 3D ON
                m_view3DTabIdx = 0;
                copyUsd();
                pasteUsd();
                break;
            case 3:
                saveShot("04_after_copy_paste_usd_roundtrip.png");
                app->quit();
                m_captureStep = -1;
                return;
            default:
                app->quit();
                m_captureStep = -1;
                return;
        }
        ++m_captureStep;
    }

    zSpace::zTsCarbcomnDesign m_carbcomn;
    std::unique_ptr<SimpleUI> m_ui;

    int m_presetIndex = 0;
    int m_selectedLevel = 2;
    int m_view3DTabIdx = 0; // 0 = 3D ON, 1 = 3D OFF (Graph always visible)

    float m_numBaysXUI = 2.0f;
    float m_numBaysYUI = 1.0f;
    float m_bayXUI = 2.25f;
    float m_bayYUI = 4.50f;
    float m_offsetXUI = 0.25f;
    float m_offsetYUI = 0.25f;

    float m_colWidthUI = 0.20f;
    float m_colLengthUI = 0.40f;
    float m_precastWidthUI = 0.22f;
    float m_precastHeightUI = 0.25f;
    float m_slabThickUI = 0.25f;
    float m_cantileverHeightUI = 0.25f;

    float m_selectedLevelZUI = 3.50f;
    float m_nextStepZUI = 2.60f;
    float m_lastSelectedZUI = 3.50f;
    float m_lastStepZUI = 2.60f;

    bool m_showColumns = true;
    bool m_showPrecast = true;
    bool m_show3DPSlab = true;
    bool m_showTensionCables = true;
    bool m_angledCorners = true;
    bool m_selectedLevelHalfUI = false;
    bool m_selectedLevelCantUI = true;
    bool m_selectedLevelOffsetUI = true;
    bool m_selectedLevelPrecastUI = true;
    bool m_showUpperSlabMeshes = true;

    std::string m_presetLabel = "Default";
    std::string m_metricsLine = "Ready";
    std::string m_levelsLine = "Ready";
    std::string m_exportNote;

    std::string m_captureDir;
    int m_captureStep = -1;
    int m_captureWarmup = 0;
};

ALICE2_REGISTER_SKETCH_AUTO(zSpaceCarbcomnDesignSketch)

#endif // __MAIN__

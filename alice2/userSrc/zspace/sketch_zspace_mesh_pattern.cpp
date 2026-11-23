// #define __MAIN__
#ifdef __MAIN__

#include <zspace/interface.h>
#include <zspace/io.h>
#include <zspace/toolsets.h>

#include <alice2.h>
#include <sketches/SketchRegistry.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace alice2;

namespace {

constexpr const char* kDesktopLisbonUsd     = "c:/Users/vishu.b/Desktop/lisbon_canyon.usd";
constexpr const char* kBundledLisbonUsd     = "data/lisbon_canyon.usd";
constexpr const char* kBundledLisbonUsda    = "data/lisbon_canyon.usda";
constexpr const char* kDualWallUsdaPath     = "data/mesh_pattern_dual_wall.usda";
constexpr const char* kCylinderUsdaPath     = "data/mesh_pattern_cylinder.usda";
constexpr const char* kExportPatternUsda    = "data/lisbon_canyon_pattern_output.usda";
constexpr const char* kExportPatternObj     = "data/lisbon_canyon_pattern_output.obj";

std::string resolveDataPath(const std::string& relativePath)
{
    namespace fs = std::filesystem;
    if (fs::path(relativePath).is_absolute() && fs::exists(relativePath)) {
        return relativePath;
    }
    const std::vector<fs::path> prefixes = {
        fs::path("."),
        fs::path(".."),
        fs::path("../.."),
        fs::path("alice2")
    };
    for (const auto& p : prefixes) {
        fs::path candidate = p / relativePath;
        if (fs::exists(candidate)) {
            return candidate.string();
        }
    }
    return relativePath;
}

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

} // namespace

class zSpaceMeshPatternSketch : public ISketch {
public:
    std::string getName() const override { return "zSpace Mesh Pattern (Lisbon Canyon)"; }
    std::string getDescription() const override { return "Gradient Subdivision + Odd-Flat / Even-Brick Negative Wave Relief + Smooth Pattern"; }
    std::string getAuthor() const override { return "zSpace"; }

    void setup() override
    {
        scene().setBackgroundColor(Color(0.95f, 0.96f, 0.97f, 1.0f));
        scene().setShowGrid(false);
        scene().setShowAxes(false);

        buildUI();
        ensureDefaultUsdaDatasetsExist();

        if (const char* envCapture = std::getenv("MESH_PATTERN_CAPTURE_DIR")) {
            if (envCapture[0] != '\0') {
                m_captureDir = envCapture;
                std::filesystem::create_directories(m_captureDir);
                m_captureStep = 0;
            }
        }

        if (const char* envUsd = std::getenv("MESH_PATTERN_USD")) {
            if (envUsd[0] != '\0' && loadExternalMeshFile(envUsd, "Env MESH_PATTERN_USD", false)) {
                return;
            }
        }

        loadPresetDataset(0);
    }

    void update(float /*deltaTime*/) override
    {
        if (!m_pattern.hasInputMesh()) return;

        const int targetSmooth = std::max(0, std::min(5, static_cast<int>(std::lround(m_smoothUI))));
        const int targetOutSmooth = std::max(0, std::min(4, static_cast<int>(std::lround(m_outSmoothUI))));
        const double targetMaxH = std::max(0.05, static_cast<double>(m_maxHeightUI));
        const double targetMinH = std::max(0.05, std::min(targetMaxH, static_cast<double>(m_minHeightUI)));
        const double targetMaxW = std::max(0.05, static_cast<double>(m_maxWidthUI));
        const double targetMinW = std::max(0.05, std::min(targetMaxW, static_cast<double>(m_minWidthUI)));
        const double targetFlatRatio = std::max(0.0, std::min(2.0, static_cast<double>(m_flatRatioUI)));
        const double targetOddWidth  = std::max(0.05, std::min(4.0, static_cast<double>(m_oddWidthUI)));
        const double targetPush = static_cast<double>(m_pushFactorUI);

        auto& p = m_pattern.params();
        if (targetOutSmooth != m_patternSmoothLevel) {
            m_patternSmoothLevel = targetOutSmooth;
            m_showEdges = (m_patternSmoothLevel == 0);
        }

        const bool geomChanged =
            (targetSmooth != p.smoothSubdivisions) ||
            (std::abs(targetMaxH - p.maxHeight) > 1e-4) ||
            (std::abs(targetMinH - p.minHeight) > 1e-4) ||
            (std::abs(targetMaxW - p.maxWidth)  > 1e-4) ||
            (std::abs(targetMinW - p.minWidth)  > 1e-4) ||
            (std::abs(targetFlatRatio - p.flatHeightRatio) > 1e-4) ||
            (std::abs(targetOddWidth - p.oddStripWidthFactor) > 1e-4) ||
            (std::abs(targetPush - p.pushHeightFactor) > 1e-4) ||
            (m_patternSmoothLevel != p.patternSmoothLevels) ||
            (m_creaseCrests   != p.creaseCrestEdges) ||
            (m_horizontalWave != p.horizontalWavePattern) ||
            (m_brickStagger   != p.staggerAlternateRows) ||
            (m_invertPhase    != p.invertPatternPhase) ||
            (m_useWidthGrad   != p.useWidthGradient) ||
            (m_closeSideWalls != p.closeSideWalls);

        const bool shadeChanged = (m_timberShading != m_lastTimberShading);

        if (geomChanged) {
            p.smoothSubdivisions    = targetSmooth;
            p.maxHeight             = targetMaxH;
            p.minHeight             = targetMinH;
            p.maxWidth              = targetMaxW;
            p.minWidth              = targetMinW;
            p.flatHeightRatio       = targetFlatRatio;
            p.oddStripWidthFactor   = targetOddWidth;
            p.pushHeightFactor      = targetPush;
            p.patternSmoothLevels   = m_patternSmoothLevel;
            p.creaseCrestEdges      = m_creaseCrests;
            p.horizontalWavePattern = m_horizontalWave;
            p.staggerAlternateRows  = m_brickStagger;
            p.invertPatternPhase    = m_invertPhase;
            p.useWidthGradient      = m_useWidthGrad;
            p.closeSideWalls        = m_closeSideWalls;

            m_pattern.compute();
            m_lastTimberShading = m_timberShading;
            rebuildRenderBuffers();
            updateStatusSummary();
        } else if (shadeChanged) {
            m_lastTimberShading = m_timberShading;
            rebuildRenderBuffers();
        }
    }

    void draw(Renderer& renderer, Camera& /*cam*/) override
    {
        if (m_pattern.hasInputMesh()) {
            drawActiveStageGeometry(renderer);
        }

        const auto& rep = m_pattern.lastReport();

        renderer.setColor(Color(0.08f, 0.10f, 0.15f, 1.0f));
        renderer.drawString(
            "zSpace Mesh Pattern  |  USD: " + m_datasetLabel +
            "  |  Grid: " + std::to_string(rep.numColumns) + " strips x " +
            std::to_string(rep.numRows) + " rows -> " +
            std::to_string(rep.patternFaces) + " faces (Smooth Lvl " +
            std::to_string(rep.patternSmoothLevels) + ")",
            14.0f, 24.0f);

        renderer.setColor(Color(0.08f, 0.36f, 0.68f, 1.0f));
        renderer.drawString(m_metricsLine, 14.0f, 44.0f);

        renderer.setColor(Color(0.16f, 0.22f, 0.30f, 1.0f));
        renderer.drawString(m_statusLine, 14.0f, 64.0f);

        renderer.setColor(Color(0.38f, 0.42f, 0.48f, 1.0f));
        renderer.drawString(
            "Shortcuts: [G] Smooth Pattern (0->2)  [1-4] Stage  [M] Preset  [S] Stagger  [Ctrl+C/V] USD  [W] Paste Width USD  [U] Export USD  [E] Export OBJ",
            14.0f, 84.0f);

        renderer.setColor(Color(0.24f, 0.30f, 0.38f, 1.0f));
        renderer.drawString("Actions & Height", 14.0f, 108.0f);
        renderer.drawString("Pattern & Width", 208.0f, 108.0f);

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
                copyPatternUsd();
                return true;
            }
            if (key == 'v' || key == 'V') {
                pasteInputUsd(/*asWidthMesh=*/false);
                return true;
            }
        }

        switch (key) {
            case 'g':
            case 'G':
                cyclePatternSmooth();
                return true;
            case 'h':
            case 'H':
                m_horizontalWave = !m_horizontalWave;
                return true;
            case 'k':
            case 'K':
                m_creaseCrests = !m_creaseCrests;
                return true;
            case '1':
                applyStage(1);
                return true;
            case '2':
                applyStage(2);
                return true;
            case '3':
                applyStage(3);
                return true;
            case '4':
                applyStage(4);
                return true;
            case '0':
            case 'n':
            case 'N':
                cycleStage();
                return true;
            case 'm':
            case 'M':
                loadPresetDataset((m_presetIndex + 1) % 3);
                return true;
            case 's':
            case 'S':
                m_brickStagger = !m_brickStagger;
                return true;
            case 'i':
            case 'I':
                m_invertPhase = !m_invertPhase;
                return true;
            case 't':
            case 'T':
                m_timberShading = !m_timberShading;
                return true;
            case 'w':
            case 'W':
                pasteInputUsd(/*asWidthMesh=*/true);
                return true;
            case 'c':
            case 'C':
                clearSecondaryWidthMesh();
                return true;
            case 'r':
            case 'R':
                resetDefaults();
                return true;
            case 'u':
            case 'U':
                exportUsd();
                return true;
            case 'e':
            case 'E':
                exportObj();
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
        if (paths.size() >= 2) {
            const bool okH = loadExternalMeshFile(paths[0], "Dropped Height USD/Mesh", false);
            const bool okW = loadExternalMeshFile(paths[1], "Dropped Width USD/Mesh", true);
            return okH || okW;
        }
        const bool asWidth = input().isShiftPressed() && m_pattern.hasInputMesh();
        return loadExternalMeshFile(paths.front(), asWidth ? "Dropped Width Mesh" : "Dropped File", asWidth);
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
        m_ui->setTheme(SimpleUI::UITheme::Dark);

        const float btnW = 180.0f;
        const float btnH = 26.0f;
        const float rowStep = 30.0f;
        const float colGap = 14.0f;

        const float xCol1 = 14.0f;
        const float xCol2 = xCol1 + btnW + colGap;
        const float yStart = 116.0f;
        const float sliderW = 112.0f;

        // Column 1: Smooth Pattern + USD Actions + Smoothness & Height Sliders
        float y1 = yStart;

        m_ui->addButton("Smooth Pattern [G]", UIRect{xCol1, y1, btnW, btnH}, [this]() {
            cyclePatternSmooth();
        });
        y1 += rowStep;

        m_ui->addButton("Cycle Stage 1->4 [N]", UIRect{xCol1, y1, btnW, btnH}, [this]() {
            cycleStage();
        });
        y1 += rowStep;

        m_ui->addButton("Switch Preset [M]", UIRect{xCol1, y1, btnW, btnH}, [this]() {
            loadPresetDataset((m_presetIndex + 1) % 3);
        });
        y1 += rowStep;

        m_ui->addButton("Reset Defaults [R]", UIRect{xCol1, y1, btnW, btnH}, [this]() {
            resetDefaults();
        });
        y1 += rowStep;

        m_ui->addButton("Copy USD [Ctrl+C]", UIRect{xCol1, y1, btnW, btnH}, [this]() {
            copyPatternUsd();
        });
        y1 += rowStep;

        m_ui->addButton("Paste USD [Ctrl+V]", UIRect{xCol1, y1, btnW, btnH}, [this]() {
            pasteInputUsd(/*asWidthMesh=*/false);
        });
        y1 += rowStep;

        m_ui->addButton("Paste Width USD [W]", UIRect{xCol1, y1, btnW, btnH}, [this]() {
            pasteInputUsd(/*asWidthMesh=*/true);
        });
        y1 += rowStep;

        m_ui->addButton("Export USD [U]", UIRect{xCol1, y1, btnW, btnH}, [this]() {
            exportUsd();
        });
        y1 += rowStep;

        m_ui->addButton("Export OBJ [E]", UIRect{xCol1, y1, btnW, btnH}, [this]() {
            exportObj();
        });

        float ys1 = y1 + btnH + 32.0f;
        m_ui->addSlider("In Smooth", Vec2{xCol1, ys1}, sliderW, 0.0f, 4.0f, m_smoothUI);
        ys1 += 38.0f;
        m_ui->addSlider("Out Smooth", Vec2{xCol1, ys1}, sliderW, 0.0f, 3.0f, m_outSmoothUI);
        ys1 += 38.0f;
        m_ui->addSlider("Max Height R", Vec2{xCol1, ys1}, sliderW, 0.25f, 5.00f, m_maxHeightUI);
        ys1 += 38.0f;
        m_ui->addSlider("Min Height K", Vec2{xCol1, ys1}, sliderW, 0.10f, 3.00f, m_minHeightUI);
        ys1 += 38.0f;
        m_ui->addSlider("Flat Crest", Vec2{xCol1, ys1}, sliderW, 0.00f, 1.00f, m_flatRatioUI);

        // Column 2: Pattern & Display Toggles + Width & Push Sliders
        float y2 = yStart;

        m_ui->addToggle("Horiz Wave [H]", UIRect{xCol2, y2, btnW, btnH}, m_horizontalWave);
        y2 += rowStep;

        m_ui->addToggle("Crease Crests [K]", UIRect{xCol2, y2, btnW, btnH}, m_creaseCrests);
        y2 += rowStep;

        m_ui->addToggle("Side Cheek Walls", UIRect{xCol2, y2, btnW, btnH}, m_closeSideWalls);
        y2 += rowStep;

        m_ui->addToggle("Brick Stagger [S]", UIRect{xCol2, y2, btnW, btnH}, m_brickStagger);
        y2 += rowStep;

        m_ui->addToggle("Flip Odd/Even [I]", UIRect{xCol2, y2, btnW, btnH}, m_invertPhase);
        y2 += rowStep;

        m_ui->addToggle("Width Gradient", UIRect{xCol2, y2, btnW, btnH}, m_useWidthGrad);
        y2 += rowStep;

        m_ui->addToggle("Timber Shading [T]", UIRect{xCol2, y2, btnW, btnH}, m_timberShading);
        y2 += rowStep;

        m_ui->addToggle("Mesh Edges", UIRect{xCol2, y2, btnW, btnH}, m_showEdges);
        y2 += rowStep;

        m_ui->addToggle("View Blue Mesh", UIRect{xCol2, y2, btnW, btnH}, m_previewBlueMesh);

        float ys2 = y2 + btnH + 32.0f;
        m_ui->addSlider("Max Width B", Vec2{xCol2, ys2}, sliderW, 0.25f, 4.00f, m_maxWidthUI);
        ys2 += 38.0f;
        m_ui->addSlider("Min Width K", Vec2{xCol2, ys2}, sliderW, 0.10f, 3.00f, m_minWidthUI);
        ys2 += 38.0f;
        m_ui->addSlider("Odd Width x", Vec2{xCol2, ys2}, sliderW, 0.10f, 2.00f, m_oddWidthUI);
        ys2 += 38.0f;
        m_ui->addSlider("Push Factor", Vec2{xCol2, ys2}, sliderW, -1.00f, 1.00f, m_pushFactorUI);
    }

    void cyclePatternSmooth()
    {
        m_patternSmoothLevel = (m_patternSmoothLevel + 1) % 4;
        m_outSmoothUI = static_cast<float>(m_patternSmoothLevel);
        m_stage = 4;
        m_showEdges = (m_patternSmoothLevel == 0);
        m_pattern.params().creaseCrestEdges = m_creaseCrests;
        m_pattern.params().closeSideWalls   = m_closeSideWalls;
        m_pattern.smoothPattern(m_patternSmoothLevel);
        rebuildRenderBuffers();
        updateStatusSummary();
    }

    void ensureDefaultUsdaDatasetsExist()
    {
        zSpace::zTsMeshPattern gen;
        {
            const std::string pWall = resolveDataPath(kDualWallUsdaPath);
            if (!std::filesystem::exists(pWall)) {
                gen.createCurvedWallPreset(18.0, 2.4, 9.0, 8, 3, /*includeWidthGradientMesh=*/true);
                gen.writeInputUSDA(kDualWallUsdaPath);
            }
        }
        {
            const std::string pCyl = resolveDataPath(kCylinderUsdaPath);
            if (!std::filesystem::exists(pCyl)) {
                gen.createOvalCylinderPreset(10.0, 6.5, 9.0, 16, 3, /*includeWidthGradientMesh=*/false);
                gen.writeInputUSDA(kCylinderUsdaPath);
            }
        }
    }

    void loadPresetDataset(int index)
    {
        m_presetIndex = ((index % 3) + 3) % 3;

        if (m_presetIndex == 0) {
            // Preset 0: Lisbon Canyon USD (Desktop file first, then bundled data/lisbon_canyon.usd / .usda)
            for (const char* candidate : { kDesktopLisbonUsd, kBundledLisbonUsd, kBundledLisbonUsda }) {
                const std::string resolved = resolveDataPath(candidate);
                if (std::filesystem::exists(resolved)) {
                    if (loadExternalMeshFile(resolved, "Lisbon Canyon USD", false)) {
                        return;
                    }
                }
            }
        }

        const char* relPath = (m_presetIndex == 1) ? kDualWallUsdaPath : kCylinderUsdaPath;
        const std::string resolved = resolveDataPath(relPath);
        if (std::filesystem::exists(resolved)) {
            loadExternalMeshFile(resolved, "USD Preset", false);
        } else {
            if (m_presetIndex == 1) {
                m_pattern.createCurvedWallPreset(18.0, 2.4, 9.0, 8, 3, true);
                m_datasetLabel = "Curved Wall (Red+Blue USD)";
            } else {
                m_pattern.createOvalCylinderPreset(10.0, 6.5, 9.0, 16, 3, false);
                m_datasetLabel = "Oval Cylinder (Red->Black USD)";
            }
            applyStage(4);
            rebuildRenderBuffers();
            frameCameraOnGeometry();
            updateStatusSummary();
        }
    }

    bool loadExternalMeshFile(const std::string& filePath, const char* sourceLabel, bool asWidthMesh)
    {
        try {
            syncUIFieldsToSolver();
            if (asWidthMesh) {
                m_pattern.loadWidthMeshFromFile(filePath);
                m_statusLine = std::string(sourceLabel) + ": attached Blue->Black width mesh '" +
                    std::filesystem::path(filePath).filename().string() + "'.";
                rebuildRenderBuffers();
            } else {
                m_pattern.loadFromFile(filePath);
                m_datasetLabel = std::filesystem::path(filePath).filename().string();
                if (m_pattern.hasWidthMesh()) {
                    m_datasetLabel += " (R->K + B->K)";
                }
                applyStage(4);
                rebuildRenderBuffers();
                frameCameraOnGeometry();
                m_statusLine = std::string(sourceLabel) + ": loaded '" + m_datasetLabel + "'.";
            }
            updateStatusSummary(/*preserveStatusLine=*/true);
            std::cout << "[MeshPattern] " << m_statusLine << std::endl;
            return true;
        } catch (const std::exception& ex) {
            m_statusLine = std::string("USD/Mesh load error: ") + ex.what();
            std::cerr << "[MeshPattern] " << m_statusLine << std::endl;
            return false;
        }
    }

    void syncUIFieldsToSolver()
    {
        auto& p = m_pattern.params();
        m_patternSmoothLevel   = std::max(0, std::min(4, static_cast<int>(std::lround(m_outSmoothUI))));
        p.smoothSubdivisions   = std::max(0, std::min(5, static_cast<int>(std::lround(m_smoothUI))));
        p.maxHeight            = std::max(0.05, static_cast<double>(m_maxHeightUI));
        p.minHeight            = std::max(0.05, std::min(p.maxHeight, static_cast<double>(m_minHeightUI)));
        p.maxWidth             = std::max(0.05, static_cast<double>(m_maxWidthUI));
        p.minWidth             = std::max(0.05, std::min(p.maxWidth, static_cast<double>(m_minWidthUI)));
        p.flatHeightRatio      = std::max(0.0, std::min(2.0, static_cast<double>(m_flatRatioUI)));
        p.oddStripWidthFactor  = std::max(0.05, std::min(4.0, static_cast<double>(m_oddWidthUI)));
        p.pushHeightFactor     = static_cast<double>(m_pushFactorUI);
        p.patternSmoothLevels   = m_patternSmoothLevel;
        p.creaseCrestEdges      = m_creaseCrests;
        p.horizontalWavePattern = m_horizontalWave;
        p.staggerAlternateRows  = m_brickStagger;
        p.invertPatternPhase    = m_invertPhase;
        p.useWidthGradient      = m_useWidthGrad;
        p.closeSideWalls        = m_closeSideWalls;
    }

    void resetDefaults()
    {
        m_smoothUI           = 2.0f;
        m_outSmoothUI        = 0.0f;
        m_maxHeightUI        = 2.0f;
        m_minHeightUI        = 0.5f;
        m_maxWidthUI         = 1.5f;
        m_minWidthUI         = 0.5f;
        m_flatRatioUI        = 0.10f;
        m_oddWidthUI         = 0.50f;
        m_pushFactorUI       = -0.5f;
        m_patternSmoothLevel = 0;
        m_creaseCrests       = true;
        m_horizontalWave     = false;
        m_brickStagger       = true;
        m_invertPhase        = false;
        m_useWidthGrad       = true;
        m_closeSideWalls     = false;
        m_showEdges          = true;

        syncUIFieldsToSolver();
        m_pattern.compute();
        applyStage(4);
        rebuildRenderBuffers();
        updateStatusSummary();
    }

    void clearSecondaryWidthMesh()
    {
        if (!m_pattern.hasWidthMesh()) {
            m_statusLine = "No secondary width mesh is loaded (using Max Width = " +
                std::to_string(m_pattern.params().maxWidth) + " m).";
            return;
        }
        m_pattern.clearWidthMesh();
        m_pattern.compute();
        rebuildRenderBuffers();
        updateStatusSummary();
        m_statusLine = "Cleared secondary width mesh — horizontal spacing reverted to Max Width.";
    }

    void applyStage(int stage)
    {
        m_stage = std::max(1, std::min(4, stage));
        switch (m_stage) {
            case 1:
                m_statusLine = "Stage 1: Input Low-Poly Colored Mesh (Surface_02 Red->Black; toggle [View Blue Mesh] for Surface_01).";
                break;
            case 2:
                m_statusLine = "Stage 2: Crease-Aware Smoothed Mesh (Catmull-Clark level " +
                    std::to_string(m_pattern.params().smoothSubdivisions) + ").";
                break;
            case 3:
                m_statusLine = "Stage 3: Remainder-Free Gradient Subdivided Quad Mesh (" +
                    std::to_string(m_pattern.lastReport().numColumns) + "x" +
                    std::to_string(m_pattern.lastReport().numRows) + " quads).";
                break;
            case 4:
            default:
                updateStatusSummary();
                break;
        }
    }

    void cycleStage()
    {
        const int next = (m_stage < 1 || m_stage >= 4) ? 1 : (m_stage + 1);
        applyStage(next);
    }

    void updateStatusSummary(bool preserveStatusLine = false)
    {
        const auto& rep = m_pattern.lastReport();
        char mbuf[440];
        std::snprintf(
            mbuf, sizeof(mbuf),
            "Height [%s]: %.2f..%.2f m (Flat %.0f%%)  |  Even W [%s]: %.2f..%.2f m  |  Odd W (%.2fx): %.2f..%.2f m  |  Push: %.2fx",
            rep.usedHeightGradient ? "Red->Black" : "Max Only",
            rep.minRowHeight, rep.maxRowHeight,
            100.0 * m_pattern.params().flatHeightRatio,
            rep.usedWidthGradient ? "Blue->Black" : "Max Only",
            rep.minColWidth, rep.maxColWidth,
            m_pattern.params().oddStripWidthFactor,
            rep.minOddColWidth, rep.maxOddColWidth,
            m_pattern.params().pushHeightFactor);
        m_metricsLine = mbuf;

        if (!preserveStatusLine) {
            char sbuf[360];
            std::snprintf(
                sbuf, sizeof(sbuf),
                "Stage 4: Odd Strips Flat + Even Strips Brick (%s, Pattern Smooth=%d)  |  %d pushed / %d flat cells  |  %.1f ms",
                m_pattern.params().pushHeightFactor <= 0.0 ? "-Normal" : "+Normal",
                rep.patternSmoothLevels,
                rep.pushedCellCount,
                rep.flatCellCount,
                rep.elapsedMs);
            m_statusLine = sbuf;
        }
    }

    void copyPatternUsd()
    {
        if (!m_pattern.hasPatternMesh()) return;
        auto* app = Application::getInstance();
        if (!app) return;
        try {
            const std::string usda = m_pattern.exportPatternToUSDA(false);
            if (app->setClipboardText(usda)) {
                m_statusLine = "Copied PatternMesh as #usda 1.0 to clipboard (" +
                    std::to_string(m_pattern.lastReport().patternFaces) + " faces).";
            } else {
                m_statusLine = "Failed to access system clipboard.";
            }
        } catch (const std::exception& ex) {
            m_statusLine = std::string("Copy USD error: ") + ex.what();
        }
        std::cout << "[MeshPattern] " << m_statusLine << std::endl;
    }

    void pasteInputUsd(bool asWidthMesh)
    {
        auto* app = Application::getInstance();
        if (!app) return;

        const std::string clipText = app->getClipboardText();
        if (!clipText.empty() && zSpace::zTsMeshPattern::isUSDAClipboardText(clipText)) {
            try {
                syncUIFieldsToSolver();
                if (asWidthMesh) {
                    m_pattern.loadWidthMeshFromUSDA(clipText);
                    m_statusLine = "Pasted Blue->Black Width Mesh from #usda 1.0 clipboard!";
                    rebuildRenderBuffers();
                } else {
                    m_pattern.loadFromUSDA(clipText);
                    m_datasetLabel = m_pattern.hasWidthMesh()
                        ? "Pasted #usda 1.0 (Height + Width)"
                        : "Pasted #usda 1.0 Height Mesh";
                    applyStage(4);
                    rebuildRenderBuffers();
                    frameCameraOnGeometry();
                    m_statusLine = "Pasted input mesh(es) from #usda 1.0 clipboard!";
                }
                updateStatusSummary(/*preserveStatusLine=*/true);
                std::cout << "[MeshPattern] " << m_statusLine << std::endl;
                return;
            } catch (const std::exception& ex) {
                m_statusLine = std::string("Paste USD error: ") + ex.what();
                std::cerr << "[MeshPattern] " << m_statusLine << std::endl;
                return;
            }
        }

        const std::string trimmed = trimQuotesAndWhitespace(clipText);
        if (!trimmed.empty() && std::filesystem::exists(trimmed)) {
            loadExternalMeshFile(trimmed, "Pasted file path", asWidthMesh);
            return;
        }

        const std::string copiedFile = app->getClipboardFilePath();
        if (!copiedFile.empty() && std::filesystem::exists(copiedFile)) {
            loadExternalMeshFile(copiedFile, "Pasted Explorer file", asWidthMesh);
            return;
        }

        m_statusLine = "Clipboard does not contain #usda 1.0 mesh text or an existing file path.";
    }

    void exportUsd()
    {
        if (!m_pattern.hasPatternMesh()) return;
        const auto res = m_pattern.writePatternMesh(kExportPatternUsda);
        if (res) {
            m_statusLine = std::string("Exported USD pattern mesh to ") + kExportPatternUsda;
        } else {
            m_statusLine = std::string("USD export error: ") + res.message();
        }
        std::cout << "[MeshPattern] " << m_statusLine << std::endl;
    }

    void exportObj()
    {
        if (!m_pattern.hasPatternMesh()) return;
        const auto res = m_pattern.writePatternMesh(kExportPatternObj);
        if (res) {
            m_statusLine = std::string("Exported OBJ pattern mesh to ") + kExportPatternObj;
        } else {
            m_statusLine = std::string("OBJ export error: ") + res.message();
        }
        std::cout << "[MeshPattern] " << m_statusLine << std::endl;
    }

    void rebuildRenderBuffers()
    {
        // 1. Low-poly input cage wireframe
        m_cageLineVerts.clear();
        {
            auto& cageMesh = (m_previewBlueMesh && m_pattern.hasWidthMesh())
                ? m_pattern.inputWidthMesh()
                : m_pattern.inputHeightMesh();
            zSpace::zFnMesh cFn(cageMesh);
            zSpace::zPointArray cPts;
            zSpace::zIntArray cEdges;
            cFn.getVertexPositions(cPts);
            cFn.getEdgeData(cEdges);
            m_cageLineVerts.reserve(cEdges.size());
            for (std::size_t i = 0; i + 1 < cEdges.size(); i += 2) {
                const int a = cEdges[i];
                const int b = cEdges[i + 1];
                if (a >= 0 && a < static_cast<int>(cPts.size()) &&
                    b >= 0 && b < static_cast<int>(cPts.size())) {
                    m_cageLineVerts.emplace_back(cPts[a].x, cPts[a].y, cPts[a].z);
                    m_cageLineVerts.emplace_back(cPts[b].x, cPts[b].y, cPts[b].z);
                }
            }
        }

        // 2. Stage 3: Gradient-subdivided base quad mesh buffers
        {
            const auto& sx = m_pattern.subdivPosX();
            const auto& sy = m_pattern.subdivPosY();
            const auto& sz = m_pattern.subdivPosZ();
            const auto& sq = m_pattern.subdivQuadIndices();
            const auto& qR = m_pattern.subdivQuadRed();
            const auto& qB = m_pattern.subdivQuadBlue();

            m_subdivVerts.resize(sx.size());
            for (std::size_t v = 0; v < sx.size(); ++v) {
                m_subdivVerts[v] = Vec3(static_cast<float>(sx[v]), static_cast<float>(sy[v]), static_cast<float>(sz[v]));
            }

            const std::size_t nQ = sq.size() / 4;
            m_subdivTris.resize(nQ * 6);
            m_subdivCols.resize(nQ * 6);
            for (std::size_t f = 0; f < nQ; ++f) {
                const Vec3& p0 = m_subdivVerts[sq[4 * f + 0]];
                const Vec3& p1 = m_subdivVerts[sq[4 * f + 1]];
                const Vec3& p2 = m_subdivVerts[sq[4 * f + 2]];
                const Vec3& p3 = m_subdivVerts[sq[4 * f + 3]];
                m_subdivTris[6 * f + 0] = p0; m_subdivTris[6 * f + 1] = p1; m_subdivTris[6 * f + 2] = p2;
                m_subdivTris[6 * f + 3] = p0; m_subdivTris[6 * f + 4] = p2; m_subdivTris[6 * f + 5] = p3;

                const float r = (f < qR.size()) ? qR[f] : 0.8f;
                const float b = (m_pattern.lastReport().usedWidthGradient && f < qB.size()) ? qB[f] : 0.0f;
                const Color c(0.18f + 0.78f * r, 0.16f, 0.16f + 0.78f * b, 1.0f);
                for (int k = 0; k < 6; ++k) m_subdivCols[6 * f + k] = c;
            }
            m_subdivEdgeIndices = m_pattern.subdivEdgePairs();
            m_subdivEdgeColors.assign(m_subdivEdgeIndices.size() / 2, Color(0.14f, 0.92f, 0.56f, 1.0f));
        }

        // 3. Stage 4: Final 3D Pattern Mesh (crease-aware group normals when smoothed; flat-faceted when unsmoothed)
        {
            const auto& px = m_pattern.posX();
            const auto& py = m_pattern.posY();
            const auto& pz = m_pattern.posZ();
            const auto& counts = m_pattern.faceCounts();
            const auto& connects = m_pattern.faceConnects();
            const auto& pushed = m_pattern.faceIsPushed();
            const bool smoothShaded = (m_pattern.lastReport().patternSmoothLevels > 0);
            const bool splitCrestNormals = m_pattern.params().creaseCrestEdges;

            m_patternVerts.resize(px.size());
            for (std::size_t v = 0; v < px.size(); ++v) {
                m_patternVerts[v] = Vec3(static_cast<float>(px[v]), static_cast<float>(py[v]), static_cast<float>(pz[v]));
            }

            // Separate smooth normal accumulators for flat crest/strip faces (group 0) vs wave scoop faces (group 1)
            // so creased crest edges stay razor-sharp while wave scoops and curved cylinder strips shade smoothly!
            std::vector<Vec3> vNormFlat;
            std::vector<Vec3> vNormWave;
            if (smoothShaded) {
                vNormFlat.assign(m_patternVerts.size(), Vec3(0.0f, 0.0f, 0.0f));
                vNormWave.assign(m_patternVerts.size(), Vec3(0.0f, 0.0f, 0.0f));

                std::size_t nOff = 0;
                for (std::size_t f = 0; f < counts.size(); ++f) {
                    const int fc = counts[f];
                    const unsigned char fTag = (f < pushed.size()) ? pushed[f] : 0;
                    if (fc >= 3 && fTag != 2) {
                        const Vec3& p0 = m_patternVerts[connects[nOff + 0]];
                        const Vec3& p1 = m_patternVerts[connects[nOff + 1]];
                        const Vec3& pLast = m_patternVerts[connects[nOff + fc - 1]];
                        const Vec3 e1 = p1 - p0;
                        const Vec3 e2 = pLast - p0;
                        const Vec3 fn(
                            e1.y * e2.z - e1.z * e2.y,
                            e1.z * e2.x - e1.x * e2.z,
                            e1.x * e2.y - e1.y * e2.x);
                        for (int k = 0; k < fc; ++k) {
                            const int vid = connects[nOff + k];
                            if (!splitCrestNormals) {
                                vNormFlat[vid] = vNormFlat[vid] + fn;
                                vNormWave[vid] = vNormWave[vid] + fn;
                            } else if (fTag == 1) {
                                vNormWave[vid] = vNormWave[vid] + fn;
                            } else {
                                vNormFlat[vid] = vNormFlat[vid] + fn;
                            }
                        }
                    }
                    nOff += static_cast<std::size_t>(fc);
                }

                auto normalizeArray = [](std::vector<Vec3>& arr) {
                    for (auto& n : arr) {
                        const float l = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
                        if (l > 1e-7f) { n.x /= l; n.y /= l; n.z /= l; }
                    }
                };
                normalizeArray(vNormFlat);
                normalizeArray(vNormWave);
            }

            m_patternTris.clear();
            m_patternTriCols.clear();
            m_patternTris.reserve(counts.size() * 6);
            m_patternTriCols.reserve(counts.size() * 6);

            const float lx = 0.36f, ly = -0.52f, lz = 0.77f;
            const float l2x = -0.45f, l2y = 0.62f, l2z = 0.64f;

            auto evalLitColor = [&](float nx, float ny, float nz, unsigned char fTag) -> Color {
                const float key1 = std::max(0.0f, nx * lx + ny * ly + nz * lz);
                const float key2 = std::max(0.0f, nx * l2x + ny * l2y + nz * l2z);
                const float sky  = 0.5f + 0.5f * nz;
                const float shade = 0.40f + 0.46f * key1 + 0.16f * key2 + 0.12f * sky;

                float baseR = 0.80f, baseG = 0.82f, baseB = 0.85f;
                if (m_timberShading) {
                    if (fTag == 2)      { baseR = 0.78f; baseG = 0.55f; baseB = 0.34f; }
                    else if (fTag == 1) { baseR = 0.84f; baseG = 0.60f; baseB = 0.38f; }
                    else                { baseR = 0.90f; baseG = 0.68f; baseB = 0.45f; }
                } else {
                    if (fTag == 2)      { baseR = 0.73f; baseG = 0.76f; baseB = 0.81f; }
                    else if (fTag == 1) { baseR = 0.78f; baseG = 0.81f; baseB = 0.85f; }
                    else                { baseR = 0.86f; baseG = 0.88f; baseB = 0.90f; }
                }
                return Color(
                    std::min(1.0f, baseR * shade),
                    std::min(1.0f, baseG * shade),
                    std::min(1.0f, baseB * shade),
                    1.0f);
            };

            std::size_t off = 0;
            for (std::size_t f = 0; f < counts.size(); ++f) {
                const int fc = counts[f];
                const unsigned char fTag = (f < pushed.size()) ? pushed[f] : 0;
                if (fc >= 3) {
                    const int i0 = connects[off + 0];
                    const Vec3& p0 = m_patternVerts[i0];
                    const bool useSmoothGroup = smoothShaded && (fTag != 2);

                    Color flatCol;
                    if (!useSmoothGroup) {
                        const Vec3& p1 = m_patternVerts[connects[off + 1]];
                        const Vec3& pLast = m_patternVerts[connects[off + fc - 1]];
                        const Vec3 e1 = p1 - p0;
                        const Vec3 e2 = pLast - p0;
                        Vec3 n(
                            e1.y * e2.z - e1.z * e2.y,
                            e1.z * e2.x - e1.x * e2.z,
                            e1.x * e2.y - e1.y * e2.x);
                        const float nLen = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
                        if (nLen > 1e-7f) { n.x /= nLen; n.y /= nLen; n.z /= nLen; }
                        flatCol = evalLitColor(n.x, n.y, n.z, fTag);
                    }

                    const auto& nArr = (fTag == 1) ? vNormWave : vNormFlat;
                    for (int k = 1; k + 1 < fc; ++k) {
                        const int i1 = connects[off + k];
                        const int i2 = connects[off + k + 1];
                        m_patternTris.push_back(p0);
                        m_patternTris.push_back(m_patternVerts[i1]);
                        m_patternTris.push_back(m_patternVerts[i2]);

                        if (useSmoothGroup) {
                            m_patternTriCols.push_back(evalLitColor(nArr[i0].x, nArr[i0].y, nArr[i0].z, fTag));
                            m_patternTriCols.push_back(evalLitColor(nArr[i1].x, nArr[i1].y, nArr[i1].z, fTag));
                            m_patternTriCols.push_back(evalLitColor(nArr[i2].x, nArr[i2].y, nArr[i2].z, fTag));
                        } else {
                            m_patternTriCols.push_back(flatCol);
                            m_patternTriCols.push_back(flatCol);
                            m_patternTriCols.push_back(flatCol);
                        }
                    }
                }
                off += static_cast<std::size_t>(fc);
            }

            m_patternEdgeIndices = m_pattern.edgePairs();
            const Color edgeCol = m_timberShading
                ? Color(0.28f, 0.18f, 0.10f, 0.80f)
                : Color(0.18f, 0.21f, 0.26f, 0.80f);
            m_patternEdgeColors.assign(m_patternEdgeIndices.size() / 2, edgeCol);
        }
    }

    void drawActiveStageGeometry(Renderer& renderer)
    {
        if (m_stage == 1 || m_stage == 2) {
            zDisplayMeshSetting disp;
            disp.showVertices = false;
            disp.showFaces = true;
            disp.useVertexColors = true;
            disp.showEdges = m_showEdges;
            disp.edgeColor = Color(0.15f, 0.92f, 0.56f, 1.0f);
            disp.edgeWidth = (m_stage == 1) ? 1.8f : 1.0f;

            if (m_stage == 1) {
                auto& m = (m_previewBlueMesh && m_pattern.hasWidthMesh())
                    ? m_pattern.inputWidthMesh()
                    : m_pattern.inputHeightMesh();
                scene().draw(m, disp);
            } else {
                auto& m = (m_previewBlueMesh && m_pattern.hasWidthMesh())
                    ? m_pattern.smoothedWidthMesh()
                    : m_pattern.smoothedHeightMesh();
                scene().draw(m, disp);
            }
        } else if (m_stage == 3) {
            if (!m_subdivTris.empty()) {
                renderer.drawMesh(
                    m_subdivTris.data(),
                    nullptr,
                    m_subdivCols.data(),
                    static_cast<int>(m_subdivTris.size()),
                    nullptr,
                    0,
                    false);
            }
            if (m_showEdges && !m_subdivEdgeIndices.empty()) {
                renderer.setLineWidth(1.1f);
                renderer.drawMeshEdges(
                    m_subdivVerts.data(),
                    m_subdivEdgeIndices.data(),
                    m_subdivEdgeColors.data(),
                    static_cast<int>(m_subdivEdgeColors.size()));
            }
        } else {
            if (!m_patternTris.empty()) {
                renderer.drawMesh(
                    m_patternTris.data(),
                    nullptr,
                    m_patternTriCols.data(),
                    static_cast<int>(m_patternTris.size()),
                    nullptr,
                    0,
                    false);
            }
            if (m_showEdges && !m_patternEdgeIndices.empty()) {
                renderer.setLineWidth(1.0f);
                renderer.drawMeshEdges(
                    m_patternVerts.data(),
                    m_patternEdgeIndices.data(),
                    m_patternEdgeColors.data(),
                    static_cast<int>(m_patternEdgeColors.size()));
            }
        }

        if (m_showInputCage && !m_cageLineVerts.empty() && m_stage != 1) {
            renderer.drawLines(
                m_cageLineVerts.data(),
                static_cast<int>(m_cageLineVerts.size()),
                Color(0.10f, 0.90f, 0.52f, 0.90f),
                2.0f);
        }
    }

    void frameCameraOnGeometry(float distanceScale = 1.45f)
    {
        const auto& verts = !m_patternVerts.empty() ? m_patternVerts : m_subdivVerts;
        if (verts.empty()) return;

        Vec3 mn = verts[0];
        Vec3 mx = verts[0];
        for (const auto& p : verts) {
            mn.x = std::min(mn.x, p.x); mn.y = std::min(mn.y, p.y); mn.z = std::min(mn.z, p.z);
            mx.x = std::max(mx.x, p.x); mx.y = std::max(mx.y, p.y); mx.z = std::max(mx.z, p.z);
        }
        const Vec3 size = mx - mn;
        const float maxSize = std::max({ size.x, size.y, size.z, 1.0f });
        const float diag = std::max(1.0f, size.length());
        camera().setNearPlane(std::max(0.1f, diag * 0.002f));
        camera().setFarPlane(std::max(5000.0f, diag * 25.0f));
        if (auto* app = Application::getInstance()) {
            auto& ctrl = app->getCameraController();
            ctrl.focusOnBounds(mn, mx);
            ctrl.setOrbitDistance(maxSize * distanceScale);
            const Vec3 shiftedCenter = ctrl.getOrbitCenter() - camera().getRight() * (maxSize * 0.14f);
            ctrl.setOrbitCenter(shiftedCenter);
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
                // Zoom in on front wall first to inspect faceted -> creased smooth -> post-smooth cheek walls!
                if (!m_patternVerts.empty()) {
                    Vec3 mn = m_patternVerts[0], mx = m_patternVerts[0];
                    for (const auto& p : m_patternVerts) {
                        mn.x = std::min(mn.x, p.x); mn.y = std::min(mn.y, p.y); mn.z = std::min(mn.z, p.z);
                        mx.x = std::max(mx.x, p.x); mx.y = std::max(mx.y, p.y); mx.z = std::max(mx.z, p.z);
                    }
                    auto& ctrl = app->getCameraController();
                    const Vec3 frontWallFocus(0.5f * (mn.x + mx.x) + 8.0f, mn.y + 3.0f, 0.5f * (mn.z + mx.z));
                    ctrl.setOrbitCenter(frontWallFocus);
                    ctrl.setOrbitDistance(16.0f);
                }
                break;
            case 1:
                saveShot("01_closeup_faceted.png");
                m_outSmoothUI = 2.0f;
                m_patternSmoothLevel = 2;
                m_creaseCrests = true;
                m_closeSideWalls = false;
                m_showEdges = false;
                syncUIFieldsToSolver();
                m_pattern.smoothPattern(2);
                rebuildRenderBuffers();
                updateStatusSummary();
                break;
            case 2:
                saveShot("02_closeup_creased_smooth_no_cheeks.png");
                m_closeSideWalls = true;
                syncUIFieldsToSolver();
                m_pattern.smoothPattern(2);
                rebuildRenderBuffers();
                updateStatusSummary();
                break;
            case 3:
                saveShot("03_closeup_creased_smooth_with_cheeks.png");
                frameCameraOnGeometry();
                break;
            case 4:
                saveShot("04_full_canyon_creased_smooth_with_cheeks.png");
                m_horizontalWave = true;
                syncUIFieldsToSolver();
                m_pattern.compute();
                rebuildRenderBuffers();
                updateStatusSummary();
                break;
            case 5:
                saveShot("05_full_canyon_horizontal_wave.png");
                if (!m_patternVerts.empty()) {
                    Vec3 mn = m_patternVerts[0], mx = m_patternVerts[0];
                    for (const auto& p : m_patternVerts) {
                        mn.x = std::min(mn.x, p.x); mn.y = std::min(mn.y, p.y); mn.z = std::min(mn.z, p.z);
                        mx.x = std::max(mx.x, p.x); mx.y = std::max(mx.y, p.y); mx.z = std::max(mx.z, p.z);
                    }
                    auto& ctrl = app->getCameraController();
                    const Vec3 frontWallFocus(0.5f * (mn.x + mx.x) + 8.0f, mn.y + 3.0f, 0.5f * (mn.z + mx.z));
                    ctrl.setOrbitCenter(frontWallFocus);
                    ctrl.setOrbitDistance(16.0f);
                }
                break;
            case 6:
                saveShot("06_closeup_horizontal_wave_smooth_cheeks.png");
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

    zSpace::zTsMeshPattern m_pattern;
    std::unique_ptr<SimpleUI> m_ui;

    int m_presetIndex = 0;
    int m_stage = 4;
    int m_patternSmoothLevel = 0;

    float m_smoothUI     = 2.0f;
    float m_outSmoothUI  = 0.0f;
    float m_maxHeightUI  = 2.0f;
    float m_minHeightUI  = 0.5f;
    float m_maxWidthUI   = 1.5f;
    float m_minWidthUI   = 0.5f;
    float m_flatRatioUI  = 0.10f;
    float m_oddWidthUI   = 0.50f;
    float m_pushFactorUI = -0.5f;

    bool m_creaseCrests      = true;
    bool m_horizontalWave    = false;
    bool m_brickStagger      = true;
    bool m_invertPhase       = false;
    bool m_useWidthGrad      = true;
    bool m_timberShading     = true;
    bool m_lastTimberShading = true;
    bool m_showEdges         = true;
    bool m_showInputCage     = false;
    bool m_previewBlueMesh   = false;
    bool m_closeSideWalls    = false;

    std::string m_datasetLabel = "lisbon_canyon.usd";
    std::string m_metricsLine  = "Ready";
    std::string m_statusLine   = "Ready";

    std::string m_captureDir;
    int m_captureStep = -1;
    int m_captureWarmup = 0;

    std::vector<Vec3> m_cageLineVerts;

    std::vector<Vec3> m_subdivVerts;
    std::vector<Vec3> m_subdivTris;
    std::vector<Color> m_subdivCols;
    std::vector<int> m_subdivEdgeIndices;
    std::vector<Color> m_subdivEdgeColors;

    std::vector<Vec3> m_patternVerts;
    std::vector<Vec3> m_patternTris;
    std::vector<Color> m_patternTriCols;
    std::vector<int> m_patternEdgeIndices;
    std::vector<Color> m_patternEdgeColors;
};

ALICE2_REGISTER_SKETCH_AUTO(zSpaceMeshPatternSketch)

#endif // __MAIN__

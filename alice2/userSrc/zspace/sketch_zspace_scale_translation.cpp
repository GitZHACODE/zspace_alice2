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

constexpr const char* kDefaultCurvesUsdaPath = "data/beacon_4arc_curves.usda";
constexpr const char* kExportSurfaceUsdaPath = "data/scale_translation_output.usda";
constexpr const char* kExportSurfaceObjPath  = "data/scale_translation_output.obj";

std::string resolveDataPath(const std::string& relativePath)
{
    namespace fs = std::filesystem;
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

} // namespace

class zSpaceScaleTranslationSketch : public ISketch {
public:
    std::string getName() const override { return "Beacon Scale-Translation Surface"; }
    std::string getDescription() const override { return "4-Arc Bottom & Top Rails -> Hyperboloid Guide -> Scale-Translation Planar Quad Surface (Steps 4-7)"; }
    std::string getAuthor() const override { return "zSpace"; }

    void setup() override
    {
        scene().setBackgroundColor(Color(0.96f, 0.97f, 0.98f, 1.0f));
        scene().setShowGrid(false);
        scene().setShowAxes(true);
        scene().setAxesLength(8.0f);

        buildUI();

        if (const char* envCapture = std::getenv("SCALE_TRANS_CAPTURE_DIR")) {
            if (envCapture[0] != '\0') {
                m_captureDir = envCapture;
                std::filesystem::create_directories(m_captureDir);
                m_captureStep = 0;
            }
        }

        if (const char* envFile = std::getenv("SCALE_TRANS_CURVES")) {
            if (envFile[0] != '\0' && loadExternalCurveFile(envFile, "CLI / Env")) {
                return;
            }
        }

        loadDefaultBeaconDataset();
    }

    void update(float /*deltaTime*/) override
    {
        if (!m_solver.hasCurves()) return;

        const double targetSpacing = std::max(0.10, static_cast<double>(m_spacingUI));
        const int targetOffset = static_cast<int>(std::lround(m_offsetPtsUI));
        const int targetVert   = std::max(1, static_cast<int>(std::lround(m_numVertDivsUI)));
        const int targetRibSub = std::max(1, static_cast<int>(std::lround(m_ribSubdivsUI)));
        const double targetAbove = std::max(0.0, static_cast<double>(m_offsetAboveSlabUI));
        const double targetBelow = std::max(0.0, static_cast<double>(m_offsetBelowSlabUI));
        const double targetPitch = std::max(0.10, static_cast<double>(m_louverSpacingUI));

        bool geomChanged = false;
        if (std::abs(targetSpacing - m_solver.params().spacing) > 1e-4 ||
            targetOffset != m_solver.params().offsetPoints ||
            targetVert != m_solver.params().numVerticalDivs ||
            m_useSlabElevations != m_solver.params().useSlabElevations ||
            targetRibSub != m_solver.params().ribSubdivsPerBay ||
            std::abs(targetAbove - m_solver.params().glazingOffsetAboveSlab) > 1e-4 ||
            std::abs(targetBelow - m_solver.params().glazingOffsetBelowSlab) > 1e-4 ||
            std::abs(targetPitch - m_solver.params().louverSpacing) > 1e-4)
        {
            m_solver.setSpacing(targetSpacing);
            m_solver.setOffsetPoints(targetOffset);
            m_solver.setNumVerticalDivs(targetVert);
            m_solver.params().useSlabElevations = m_useSlabElevations;
            m_solver.params().ribSubdivsPerBay = targetRibSub;
            m_solver.params().glazingOffsetAboveSlab = targetAbove;
            m_solver.params().glazingOffsetBelowSlab = targetBelow;
            m_solver.params().louverSpacing = targetPitch;
            m_solver.compute();
            geomChanged = true;
        }

        const bool tolChanged = std::abs(m_analysisTolUI - m_lastAppliedTol) > 1e-7f;
        const bool modeChanged = (m_quadDiagMode != m_lastQuadDiagMode) || (m_showWarpedGuide != m_lastShowWarped);

        if (tolChanged) {
            m_lastAppliedTol = m_analysisTolUI;
            m_solver.params().analysisTolerance = static_cast<double>(m_analysisTolUI);
        }
        if (modeChanged) {
            m_lastQuadDiagMode = m_quadDiagMode;
            m_lastShowWarped = m_showWarpedGuide;
            m_solver.params().quadDiagonalMode = m_quadDiagMode;
        }

        if (m_showAnalysisColors && !m_hasAnalysis) {
            runAnalysis(/*forceShowSurface=*/false);
        } else if (geomChanged || modeChanged || (tolChanged && m_hasAnalysis)) {
            if (m_hasAnalysis) {
                runAnalysis(/*forceShowSurface=*/false);
            } else {
                rebuildRenderBuffers();
            }
            if (geomChanged) {
                updateSummaryStatus();
            }
        }
    }

    void draw(Renderer& renderer, Camera& /*cam*/) override
    {
        if (m_solver.hasCurves()) {
            drawSceneGeometry(renderer);
        }

        // Top HUD Status Overlay
        const auto& ad = m_solver.arcDivisions();
        std::string arcDivsStr = "[";
        for (std::size_t k = 0; k < ad.size(); ++k) {
            if (k >= 8) {
                arcDivsStr += ",...";
                break;
            }
            if (k > 0) arcDivsStr += ",";
            arcDivsStr += std::to_string(ad[k]);
        }
        arcDivsStr += "]";

        renderer.setColor(Color(0.08f, 0.10f, 0.15f, 1.0f));
        renderer.drawString(
            "zSpace Scale-Translation  |  Input: " + m_datasetLabel +
            "  |  Arcs " + arcDivsStr + " = " +
            std::to_string(m_solver.numPrimaryMembers()) + " primary ribs, " +
            std::to_string(m_solver.numSecondaryMembers()) + " secondary louvers, " +
            std::to_string(m_solver.numFaces()) + " planar cells",
            14.0f, 24.0f);

        renderer.setColor(Color(0.08f, 0.36f, 0.68f, 1.0f));
        renderer.drawString(m_analysisLine, 14.0f, 44.0f);

        renderer.setColor(Color(0.16f, 0.22f, 0.30f, 1.0f));
        renderer.drawString(m_statusLine, 14.0f, 64.0f);

        renderer.setColor(Color(0.38f, 0.42f, 0.48f, 1.0f));
        renderer.drawString(
            "Shortcuts: [4-8] Step View  [N] Cycle 4->8  [A] Analyze  [W] Warped Guide  [R] Reset  [Ctrl+C/V] USD  [U] Export USD  [E] Export OBJ",
            14.0f, 84.0f);

        renderer.setColor(Color(0.24f, 0.30f, 0.38f, 1.0f));
        renderer.drawString("Actions & Parameters", 14.0f, 108.0f);
        renderer.drawString("Steps (4-8) & Timber", 208.0f, 108.0f);

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
                copySurfaceUsd();
                return true;
            }
            if (key == 'v' || key == 'V') {
                pasteCurvesUsd();
                return true;
            }
        }

        switch (key) {
            case 'a':
            case 'A':
                runAnalysis(/*forceShowSurface=*/true);
                return true;
            case '4':
                applyWorkflowStage(4);
                return true;
            case '5':
                applyWorkflowStage(5);
                return true;
            case '6':
                applyWorkflowStage(6);
                return true;
            case '7':
                applyWorkflowStage(7);
                return true;
            case '8':
                applyWorkflowStage(8);
                return true;
            case '0':
            case 'n':
            case 'N':
                cycleWorkflowStage();
                return true;
            case 'w':
            case 'W':
                m_showWarpedGuide = !m_showWarpedGuide;
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
        return loadExternalCurveFile(paths.front(), "Dropped file");
    }

    bool onMousePress(int button, int state, int x, int y) override
    {
        if (m_ui && m_ui->onMousePress(button, state, x, y)) {
            return true;
        }
        return false;
    }

    bool onMouseMove(int x, int y) override
    {
        if (m_ui && m_ui->onMouseMove(x, y)) {
            return true;
        }
        return false;
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

        const float xActions = 14.0f;
        const float xToggles = xActions + btnW + colGap; // 208.0f
        const float yStart = 116.0f;

        // Column 1: Action Buttons + Sliders
        float yCol1 = yStart;

        m_ui->addButton("Analyze [A]", UIRect{xActions, yCol1, btnW, btnH}, [this]() {
            runAnalysis(/*forceShowSurface=*/true);
        });
        yCol1 += rowStep;

        m_ui->addButton("Cycle Step 4->8 [N]", UIRect{xActions, yCol1, btnW, btnH}, [this]() {
            cycleWorkflowStage();
        });
        yCol1 += rowStep;

        m_ui->addButton("Reset Defaults [R]", UIRect{xActions, yCol1, btnW, btnH}, [this]() {
            resetDefaults();
        });
        yCol1 += rowStep;

        m_ui->addButton("Copy USD [Ctrl+C]", UIRect{xActions, yCol1, btnW, btnH}, [this]() {
            copySurfaceUsd();
        });
        yCol1 += rowStep;

        m_ui->addButton("Paste USD [Ctrl+V]", UIRect{xActions, yCol1, btnW, btnH}, [this]() {
            pasteCurvesUsd();
        });
        yCol1 += rowStep;

        m_ui->addButton("Export USD [U]", UIRect{xActions, yCol1, btnW, btnH}, [this]() {
            exportUsd();
        });
        yCol1 += rowStep;

        m_ui->addButton("Export OBJ [E]", UIRect{xActions, yCol1, btnW, btnH}, [this]() {
            exportObj();
        });
        yCol1 += rowStep;

        m_ui->addButton("Beacon Preset", UIRect{xActions, yCol1, btnW, btnH}, [this]() {
            loadDefaultBeaconDataset(true);
        });

        const float sliderW = 112.0f;
        float ySlider = yCol1 + btnH + 32.0f;
        m_ui->addSlider("Spacing", Vec2{xActions, ySlider}, sliderW, 0.50f, 20.00f, m_spacingUI);
        ySlider += 38.0f;
        m_ui->addSlider("Offset Pts", Vec2{xActions, ySlider}, sliderW, -35.0f, 35.0f, m_offsetPtsUI);
        ySlider += 38.0f;
        m_ui->addSlider("Vert Tiers", Vec2{xActions, ySlider}, sliderW, 1.0f, 20.0f, m_numVertDivsUI);
        ySlider += 38.0f;
        m_ui->addSlider("Analysis Tol", Vec2{xActions, ySlider}, sliderW, 0.0005f, 0.0250f, m_analysisTolUI);

        // Column 2: Step Overlays, Timber Toggles & Step 8 Architectural Sliders
        float yCol2 = yStart;

        m_ui->addToggle("Step 4: Input Arcs", UIRect{xToggles, yCol2, btnW, btnH}, m_showStep4InputArcs);
        yCol2 += rowStep;

        m_ui->addToggle("Step 5: Hyperboloid", UIRect{xToggles, yCol2, btnW, btnH}, m_showStep5Hyperboloid);
        yCol2 += rowStep;

        m_ui->addToggle("Step 6: Horiz Rails", UIRect{xToggles, yCol2, btnW, btnH}, m_showStep6HorizRails);
        yCol2 += rowStep;

        m_ui->addToggle("Step 7: Quad Mesh", UIRect{xToggles, yCol2, btnW, btnH}, m_showStep7Mesh);
        yCol2 += rowStep;

        m_ui->addToggle("Step 8: 3D Ribs", UIRect{xToggles, yCol2, btnW, btnH}, m_showStep8Ribs);
        yCol2 += rowStep;

        m_ui->addToggle("Step 8: 3D Louvers", UIRect{xToggles, yCol2, btnW, btnH}, m_showStep8Louvers);
        yCol2 += rowStep;

        m_ui->addToggle("USDA Slab Zs", UIRect{xToggles, yCol2, btnW, btnH}, m_useSlabElevations);
        yCol2 += rowStep;

        m_ui->addToggle("Warped Guide [W]", UIRect{xToggles, yCol2, btnW, btnH}, m_showWarpedGuide);
        yCol2 += rowStep;

        m_ui->addToggle("Analysis Colors", UIRect{xToggles, yCol2, btnW, btnH}, m_showAnalysisColors);
        yCol2 += rowStep;

        m_ui->addToggle("Mesh Edges", UIRect{xToggles, yCol2, btnW, btnH}, m_showMeshEdges);

        float ySlider2 = yCol2 + btnH + 32.0f;
        m_ui->addSlider("Rib Subdivs", Vec2{xToggles, ySlider2}, sliderW, 1.0f, 16.0f, m_ribSubdivsUI);
        ySlider2 += 38.0f;
        m_ui->addSlider("Above Slab", Vec2{xToggles, ySlider2}, sliderW, 0.00f, 6.00f, m_offsetAboveSlabUI);
        ySlider2 += 38.0f;
        m_ui->addSlider("Below Slab", Vec2{xToggles, ySlider2}, sliderW, 0.00f, 8.00f, m_offsetBelowSlabUI);
        ySlider2 += 38.0f;
        m_ui->addSlider("Louver Pitch", Vec2{xToggles, ySlider2}, sliderW, 0.15f, 1.50f, m_louverSpacingUI);
    }

    void loadDefaultBeaconDataset(bool forceSyntheticPreset = false)
    {
        try {
            m_ribSubdivsUI = 8.0f;
            m_louverSpacingUI = 0.40f;
            m_solver.params().ribWidth = 0.40;
            m_solver.params().ribDepth = 0.80;
            m_solver.params().louverHeight = 0.25;
            m_solver.params().louverDepth = 0.30;
            m_solver.params().louverSpacing = 0.40;
            m_solver.params().ribSubdivsPerBay = 8;

            const std::string resolvedRhino = resolveDataPath("data/beacon_4arc_curves_rhino.usda");
            if (!forceSyntheticPreset && std::filesystem::exists(resolvedRhino)) {
                m_spacingUI = 5.05f;
                m_offsetPtsUI = 13.0f;
                m_numVertDivsUI = 4.0f;
                m_offsetAboveSlabUI = 0.80f;
                m_offsetBelowSlabUI = 3.60f;
                m_useSlabElevations = true;

                m_solver.params().spacing = 5.05;
                m_solver.params().offsetPoints = 13;
                m_solver.params().useSlabElevations = true;
                m_solver.params().numVerticalDivs = 4;
                m_solver.params().glazingOffsetAboveSlab = 0.80;
                m_solver.params().glazingOffsetBelowSlab = 3.60;
                m_solver.params().analysisTolerance = static_cast<double>(m_analysisTolUI);
                m_solver.loadCurvesFromFile(resolvedRhino);
                m_datasetLabel = "beacon_4arc_curves_rhino.usda";
            } else {
                m_spacingUI = 2.05f;
                m_offsetPtsUI = 13.0f;
                m_numVertDivsUI = 4.0f;
                m_offsetAboveSlabUI = 0.40f;
                m_offsetBelowSlabUI = 1.60f;
                m_useSlabElevations = true;

                m_solver.params().spacing = 2.05;
                m_solver.params().offsetPoints = 13;
                m_solver.params().useSlabElevations = true;
                m_solver.params().numVerticalDivs = 4;
                m_solver.params().glazingOffsetAboveSlab = 0.40;
                m_solver.params().glazingOffsetBelowSlab = 1.60;
                m_solver.params().analysisTolerance = static_cast<double>(m_analysisTolUI);

                const std::string resolvedUsda = resolveDataPath(kDefaultCurvesUsdaPath);
                if (!forceSyntheticPreset && std::filesystem::exists(resolvedUsda)) {
                    m_solver.loadCurvesFromFile(resolvedUsda);
                    if (m_solver.slabVerticalLines().empty()) {
                        m_solver.createBeaconPreset(24.0, 15.0, 9.8, 1.42, zSpace::zVector(-4.5f, 2.0f, 14.0f), { 3.5, 7.0, 10.5 });
                        m_solver.writeCurvesUSDA(resolvedUsda, 33);
                    }
                    m_datasetLabel = "beacon_4arc_curves.usda";
                } else {
                    m_solver.createBeaconPreset(24.0, 15.0, 9.8, 1.42, zSpace::zVector(-4.5f, 2.0f, 14.0f), { 3.5, 7.0, 10.5 });
                    m_solver.writeCurvesUSDA(kDefaultCurvesUsdaPath, 33);
                    m_datasetLabel = "beacon_4arc_curves.usda";
                }
            }

            m_hasAnalysis = false;
            m_showAnalysisColors = false;
            m_showWarpedGuide = false;
            m_lastShowWarped = false;
            applyWorkflowStage(8);

            rebuildRenderBuffers();
            frameCameraOnGeometry();
            m_analysisLine = "Analysis: not run yet — click [Analyze] (or press A) to inspect quad planarity.";
            updateSummaryStatus();
        } catch (const std::exception& ex) {
            m_statusLine = std::string("Error loading Beacon dataset: ") + ex.what();
            std::cerr << "[ScaleTranslation] " << m_statusLine << std::endl;
        }
    }

    bool loadExternalCurveFile(const std::string& filePath, const char* sourceLabel)
    {
        try {
            m_solver.setSpacing(static_cast<double>(m_spacingUI));
            m_solver.setOffsetPoints(static_cast<int>(std::lround(m_offsetPtsUI)));
            m_solver.setNumVerticalDivs(static_cast<int>(std::lround(m_numVertDivsUI)));
            m_solver.loadCurvesFromFile(filePath);

            m_spacingUI = static_cast<float>(m_solver.params().spacing);
            m_offsetPtsUI = static_cast<float>(m_solver.params().offsetPoints);
            m_numVertDivsUI = static_cast<float>(m_solver.params().numVerticalDivs);
            m_datasetLabel = std::filesystem::path(filePath).filename().string();

            m_hasAnalysis = false;
            m_showAnalysisColors = false;
            m_showWarpedGuide = false;
            m_lastShowWarped = false;

            rebuildRenderBuffers();
            frameCameraOnGeometry();
            m_analysisLine = "Analysis: not run yet — click [Analyze] (or press A) to inspect quad planarity.";
            m_statusLine = std::string(sourceLabel) + ": loaded 4-arc bottom & top curves from " + m_datasetLabel + ".";
            std::cout << "[ScaleTranslation] " << m_statusLine << std::endl;
            return true;
        } catch (const std::exception& ex) {
            m_statusLine = std::string("Load curves error: ") + ex.what();
            std::cerr << "[ScaleTranslation] " << m_statusLine << std::endl;
            return false;
        }
    }

    void applyWorkflowStage(int stage)
    {
        m_workflowStage = stage;
        switch (stage) {
            case 4: // Step 4: Translate to Upper Rail Position (Bottom + Top curves + Slab lines)
                m_showStep4InputArcs   = true;
                m_showStep5Hyperboloid = false;
                m_showStep6HorizRails  = false;
                m_showStep7Mesh        = false;
                m_showStep8Ribs        = false;
                m_showStep8Louvers     = false;
                m_statusLine = "Step 4: Input Bottom & Upper Multi-Arc Curves + Vertical Slab Lines.";
                break;
            case 5: // Step 5: Get Hyperboloid Surface Guide Shape
                m_showStep4InputArcs   = true;
                m_showStep5Hyperboloid = true;
                m_showStep6HorizRails  = false;
                m_showStep7Mesh        = false;
                m_showStep8Ribs        = false;
                m_showStep8Louvers     = false;
                m_statusLine = "Step 5: Hyperboloid Surface Guide Shape (" +
                    std::to_string(m_solver.totalDivisions()) + " rulings, offset = " +
                    std::to_string(m_solver.params().offsetPoints) + " pts).";
                break;
            case 6: // Step 6: Get Scaled+Translated Horizontal Rails Along Surface
                m_showStep4InputArcs   = false;
                m_showStep5Hyperboloid = true;
                m_showStep6HorizRails  = true;
                m_showStep7Mesh        = false;
                m_showStep8Ribs        = false;
                m_showStep8Louvers     = false;
                m_statusLine = "Step 6: Scaled + Translated Horizontal Rails at Slab Elevations.";
                break;
            case 7: // Step 7: Get Vertical Divisions + Planar Scale-Translation Mesh
                m_showStep4InputArcs   = true;
                m_showStep5Hyperboloid = false;
                m_showStep6HorizRails  = true;
                m_showStep7Mesh        = true;
                m_showStep8Ribs        = false;
                m_showStep8Louvers     = false;
                updateSummaryStatus();
                break;
            case 8: // Step 8: Get Architectural Geometry (Smooth 0.4x0.8m Ribs + 0.25x0.30m Louvers)
            default:
                m_showStep4InputArcs   = true;
                m_showStep5Hyperboloid = false;
                m_showStep6HorizRails  = true;
                m_showStep7Mesh        = false;
                m_showStep8Ribs        = true;
                m_showStep8Louvers     = true;
                updateSummaryStatus();
                break;
        }
    }

    void cycleWorkflowStage()
    {
        const int nextStage = (m_workflowStage < 4 || m_workflowStage >= 8) ? 4 : (m_workflowStage + 1);
        applyWorkflowStage(nextStage);
    }

    void resetDefaults()
    {
        m_solver.reset();
        m_spacingUI = static_cast<float>(m_solver.params().spacing);
        m_offsetPtsUI = static_cast<float>(m_solver.params().offsetPoints);
        m_numVertDivsUI = static_cast<float>(m_solver.params().numVerticalDivs);
        m_useSlabElevations = m_solver.params().useSlabElevations;
        m_ribSubdivsUI = static_cast<float>(m_solver.params().ribSubdivsPerBay);
        m_offsetAboveSlabUI = static_cast<float>(m_solver.params().glazingOffsetAboveSlab);
        m_offsetBelowSlabUI = static_cast<float>(m_solver.params().glazingOffsetBelowSlab);
        m_louverSpacingUI = static_cast<float>(m_solver.params().louverSpacing);

        m_hasAnalysis = false;
        m_showAnalysisColors = false;
        m_showWarpedGuide = false;
        m_lastShowWarped = false;
        applyWorkflowStage(8);
        rebuildRenderBuffers();
        m_analysisLine = "Analysis: not run yet — click [Analyze] (or press A) to inspect quad planarity.";
        updateSummaryStatus();
    }

    void runAnalysis(bool forceShowSurface = false)
    {
        if (!m_solver.hasMesh()) return;
        m_solver.params().analysisTolerance = static_cast<double>(m_analysisTolUI);
        m_solver.params().quadDiagonalMode = m_quadDiagMode;

        const auto rep = m_solver.analyze(/*analyzeWarpedGuide=*/m_showWarpedGuide, /*updateMeshFaceColors=*/false);
        if (forceShowSurface) {
            m_showStep7Mesh = true;
            m_showAnalysisColors = true;
        }
        m_hasAnalysis = true;
        rebuildRenderBuffers(&rep);

        const double pctPlanar = (rep.faceCount > 0)
            ? (100.0 * static_cast<double>(rep.planarFaceCount) / static_cast<double>(rep.faceCount))
            : 100.0;

        char buf[320];
        std::snprintf(
            buf, sizeof(buf),
            "%s (tol %.4f): %d / %d planar (%.1f%%) | max face dev: %.6f m | max rib dev: %.6f m | waist scale: %.3f..%.3f",
            m_showWarpedGuide ? "Warped Hyperboloid Guide" : "Scale-Translation Surface",
            rep.threshold, rep.planarFaceCount, rep.faceCount, pctPlanar,
            rep.maxDeviation, rep.maxRibPlanarDeviation, rep.minRailScale, rep.maxRailScale);
        m_analysisLine = buf;
        std::cout << "[ScaleTranslation] " << m_analysisLine << std::endl;
    }

    void updateSummaryStatus()
    {
        double minScale = 1.0, maxScale = 1.0;
        if (!m_solver.horizontalRails().empty()) {
            minScale = m_solver.horizontalRails().front().scale;
            maxScale = minScale;
            for (const auto& r : m_solver.horizontalRails()) {
                minScale = std::min(minScale, r.scale);
                maxScale = std::max(maxScale, r.scale);
            }
        }
        char buf[384];
        std::snprintf(
            buf, sizeof(buf),
            "Built in %.2f ms  |  Ribs: 0.4x0.8 m (%d subdivs/bay)  |  Louvers: 0.25x0.30 m @ %.2f m (%d rings)  |  Slab Offsets: +%.2f / -%.2f m",
            m_solver.lastComputeMs(),
            m_solver.params().ribSubdivsPerBay,
            m_solver.params().louverSpacing,
            m_solver.numLouverLevels(),
            m_solver.params().glazingOffsetAboveSlab,
            m_solver.params().glazingOffsetBelowSlab);
        m_statusLine = buf;
    }

    void copySurfaceUsd()
    {
        if (!m_solver.hasMesh()) return;
        auto* app = Application::getInstance();
        if (!app) return;
        try {
            const std::string usda = m_solver.exportFullSceneToUSDA(m_hasAnalysis && !m_showWarpedGuide);
            if (app->setClipboardText(usda)) {
                m_statusLine = "Copied Mesh + Rails + 3D Ribs & Louvers as #usda 1.0 to clipboard.";
            } else {
                m_statusLine = "Failed to access system clipboard.";
            }
        } catch (const std::exception& ex) {
            m_statusLine = std::string("Copy USD failed: ") + ex.what();
        }
        std::cout << "[ScaleTranslation] " << m_statusLine << std::endl;
    }

    void pasteCurvesUsd()
    {
        auto* app = Application::getInstance();
        if (!app) return;

        const std::string clipText = app->getClipboardText();
        if (!clipText.empty() && zSpace::zTsScaleTranslation::isUSDAClipboardText(clipText)) {
            try {
                m_solver.loadCurvesFromUSDA(clipText);
                m_datasetLabel = "Pasted #usda 1.0 Curves";
                m_hasAnalysis = false;
                m_showAnalysisColors = false;
                m_showWarpedGuide = false;
                rebuildRenderBuffers();
                frameCameraOnGeometry();
                m_analysisLine = "Analysis: not run yet — click [Analyze] (or press A) to inspect quad planarity.";
                m_statusLine = "Pasted 4-arc bottom & top curves from #usda 1.0 clipboard!";
                std::cout << "[ScaleTranslation] " << m_statusLine << std::endl;
                return;
            } catch (const std::exception& ex) {
                m_statusLine = std::string("Paste USD error: ") + ex.what();
                std::cerr << "[ScaleTranslation] " << m_statusLine << std::endl;
                return;
            }
        }

        if (!clipText.empty()) {
            std::string trimmed = clipText;
            while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t' || trimmed.front() == '\r' || trimmed.front() == '\n' || trimmed.front() == '"')) {
                trimmed.erase(trimmed.begin());
            }
            while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t' || trimmed.back() == '\r' || trimmed.back() == '\n' || trimmed.back() == '"')) {
                trimmed.pop_back();
            }
            if (!trimmed.empty() && std::filesystem::exists(trimmed)) {
                loadExternalCurveFile(trimmed, "Pasted file path");
                return;
            }
        }

        const std::string copiedFile = app->getClipboardFilePath();
        if (!copiedFile.empty() && std::filesystem::exists(copiedFile)) {
            loadExternalCurveFile(copiedFile, "Pasted Explorer file");
            return;
        }

        m_statusLine = "Clipboard does not contain #usda 1.0 4-arc curves or a curve file path.";
    }

    void exportUsd()
    {
        if (!m_solver.hasMesh()) return;
        try {
            const auto parent = std::filesystem::path(kExportSurfaceUsdaPath).parent_path();
            if (!parent.empty()) std::filesystem::create_directories(parent);
            std::ofstream ofs(kExportSurfaceUsdaPath, std::ios::binary);
            ofs << m_solver.exportFullSceneToUSDA(m_hasAnalysis);
            if (ofs) {
                m_statusLine = std::string("Exported full USD scene (Mesh + Rails + Ribs + Louvers) to ") + kExportSurfaceUsdaPath;
            } else {
                m_statusLine = std::string("USD export error writing ") + kExportSurfaceUsdaPath;
            }
        } catch (const std::exception& ex) {
            m_statusLine = std::string("USD export error: ") + ex.what();
        }
        std::cout << "[ScaleTranslation] " << m_statusLine << std::endl;
    }

    void exportObj()
    {
        if (!m_solver.hasMesh()) return;
        const auto res = m_solver.writeSurfaceMesh(kExportSurfaceObjPath, false);
        if (res) {
            m_statusLine = std::string("Exported OBJ surface to ") + kExportSurfaceObjPath;
        } else {
            m_statusLine = std::string("OBJ export error: ") + res.message();
        }
        std::cout << "[ScaleTranslation] " << m_statusLine << std::endl;
    }

    void rebuildRenderBuffers(const zSpace::zScaleTransAnalysisReport* analysisOpt = nullptr)
    {
        const int nV = m_solver.numVertices();
        const int nF = m_solver.numFaces();
        const auto& X = m_showWarpedGuide ? m_solver.warpedPosX() : m_solver.posX();
        const auto& Y = m_showWarpedGuide ? m_solver.warpedPosY() : m_solver.posY();
        const auto& Z = m_showWarpedGuide ? m_solver.warpedPosZ() : m_solver.posZ();

        m_currVerts.resize(static_cast<std::size_t>(nV));
        for (int v = 0; v < nV; ++v) {
            m_currVerts[v] = Vec3(static_cast<float>(X[v]), static_cast<float>(Y[v]), static_cast<float>(Z[v]));
        }

        const auto& quads = m_solver.quadIndices();
        m_triPositions.resize(static_cast<std::size_t>(nF * 6));
        const Color neutralClay(0.76f, 0.78f, 0.80f, 1.0f);
        m_neutralTriColors.assign(static_cast<std::size_t>(nF * 6), neutralClay);

        for (int f = 0; f < nF; ++f) {
            const Vec3& p0 = m_currVerts[quads[4 * f + 0]];
            const Vec3& p1 = m_currVerts[quads[4 * f + 1]];
            const Vec3& p2 = m_currVerts[quads[4 * f + 2]];
            const Vec3& p3 = m_currVerts[quads[4 * f + 3]];

            m_triPositions[6 * f + 0] = p0;
            m_triPositions[6 * f + 1] = p1;
            m_triPositions[6 * f + 2] = p2;
            m_triPositions[6 * f + 3] = p0;
            m_triPositions[6 * f + 4] = p2;
            m_triPositions[6 * f + 5] = p3;
        }

        const zSpace::zScaleTransAnalysisReport* rep = analysisOpt
            ? analysisOpt
            : (m_hasAnalysis ? &m_solver.lastAnalysis() : nullptr);

        if (rep && static_cast<int>(rep->faceDeviations.size()) == nF) {
            const Color planarCol(0.0f, 240.0f / 255.0f, 140.0f / 255.0f, 1.0f);
            const Color nonPlanarCol(240.0f / 255.0f, 0.0f, 140.0f / 255.0f, 1.0f);
            m_triColors.resize(static_cast<std::size_t>(nF * 6));
            for (int f = 0; f < nF; ++f) {
                const Color c = (rep->faceDeviations[f] <= rep->threshold) ? planarCol : nonPlanarCol;
                for (int k = 0; k < 6; ++k) {
                    m_triColors[6 * f + k] = c;
                }
            }
        } else {
            m_triColors = m_neutralTriColors;
        }

        m_edgeIndices = m_solver.edgePairs();
        m_edgeColors.assign(m_edgeIndices.size() / 2, Color(0.16f, 0.19f, 0.24f, 1.0f));

        // Cache 8 bi-arc junction points (4 bottom + 4 top) and smooth per-arc line segments
        m_junctionPts.clear();
        for (const auto& p : m_solver.bottomLoop().junctionPoints()) {
            m_junctionPts.emplace_back(p.x, p.y, p.z);
        }
        for (const auto& p : m_solver.topLoop().junctionPoints()) {
            m_junctionPts.emplace_back(p.x, p.y, p.z);
        }

        m_botEndArcLines.clear();
        m_botSideArcLines.clear();
        m_topArcLines.clear();
        constexpr int kSegs = 36;
        for (std::size_t k = 0; k < m_solver.bottomLoop().arcs.size(); ++k) {
            auto& botDst = (k % 2 == 0) ? m_botEndArcLines : m_botSideArcLines;
            for (int s = 0; s < kSegs; ++s) {
                const double t0 = static_cast<double>(s) / kSegs;
                const double t1 = static_cast<double>(s + 1) / kSegs;
                const auto b0 = m_solver.bottomLoop().arcs[k].pointAt(t0);
                const auto b1 = m_solver.bottomLoop().arcs[k].pointAt(t1);
                botDst.emplace_back(b0.x, b0.y, b0.z);
                botDst.emplace_back(b1.x, b1.y, b1.z);
            }
        }
        for (std::size_t k = 0; k < m_solver.topLoop().arcs.size(); ++k) {
            for (int s = 0; s < kSegs; ++s) {
                const double t0 = static_cast<double>(s) / kSegs;
                const double t1 = static_cast<double>(s + 1) / kSegs;
                const auto u0 = m_solver.topLoop().arcs[k].pointAt(t0);
                const auto u1 = m_solver.topLoop().arcs[k].pointAt(t1);
                m_topArcLines.emplace_back(u0.x, u0.y, u0.z);
                m_topArcLines.emplace_back(u1.x, u1.y, u1.z);
            }
        }

        // Cache vertical slab lines + common orange slab points + outer b/T points
        m_slabLineVerts.clear();
        for (const auto& seg : m_solver.slabVerticalLines()) {
            m_slabLineVerts.emplace_back(seg.startPt.x, seg.startPt.y, seg.startPt.z);
            m_slabLineVerts.emplace_back(seg.endPt.x, seg.endPt.y, seg.endPt.z);
        }
        m_slabCommonPts.clear();
        for (const auto& p : m_solver.slabCommonPoints()) {
            m_slabCommonPts.emplace_back(p.x, p.y, p.z);
        }
        m_slabOuterPts.clear();
        for (const auto& p : m_solver.slabOuterEndpoints()) {
            m_slabOuterPts.emplace_back(p.x, p.y, p.z);
        }

        // Build directional-lit Step 8 3D Primary Ribs & Secondary Louvers triangle buffers
        auto buildLitQuadTris = [](const std::vector<double>& px,
                                   const std::vector<double>& py,
                                   const std::vector<double>& pz,
                                   const std::vector<int>& qIdx,
                                   float baseR, float baseG, float baseB,
                                   std::vector<Vec3>& outTris,
                                   std::vector<Color>& outCols) {
            const std::size_t nQ = qIdx.size() / 4;
            outTris.resize(nQ * 6);
            outCols.resize(nQ * 6);
            // Architectural studio key + fill sun direction
            const float lx = 0.42f, ly = -0.56f, lz = 0.71f;

            for (std::size_t f = 0; f < nQ; ++f) {
                const int i0 = qIdx[4 * f + 0];
                const int i1 = qIdx[4 * f + 1];
                const int i2 = qIdx[4 * f + 2];
                const int i3 = qIdx[4 * f + 3];
                const Vec3 p0(static_cast<float>(px[i0]), static_cast<float>(py[i0]), static_cast<float>(pz[i0]));
                const Vec3 p1(static_cast<float>(px[i1]), static_cast<float>(py[i1]), static_cast<float>(pz[i1]));
                const Vec3 p2(static_cast<float>(px[i2]), static_cast<float>(py[i2]), static_cast<float>(pz[i2]));
                const Vec3 p3(static_cast<float>(px[i3]), static_cast<float>(py[i3]), static_cast<float>(pz[i3]));

                const Vec3 e1 = p1 - p0;
                const Vec3 e2 = p3 - p0;
                Vec3 n(
                    e1.y * e2.z - e1.z * e2.y,
                    e1.z * e2.x - e1.x * e2.z,
                    e1.x * e2.y - e1.y * e2.x);
                const float nLen = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
                if (nLen > 1e-7f) { n.x /= nLen; n.y /= nLen; n.z /= nLen; }

                const float ndotl = std::max(0.0f, n.x * lx + n.y * ly + n.z * lz);
                const float skyFill = 0.5f + 0.5f * n.z;
                const float shade = 0.56f + 0.36f * ndotl + 0.14f * skyFill;
                const Color c(
                    std::min(1.0f, baseR * shade),
                    std::min(1.0f, baseG * shade),
                    std::min(1.0f, baseB * shade),
                    1.0f);

                outTris[6 * f + 0] = p0; outTris[6 * f + 1] = p1; outTris[6 * f + 2] = p2;
                outTris[6 * f + 3] = p0; outTris[6 * f + 4] = p2; outTris[6 * f + 5] = p3;
                for (int k = 0; k < 6; ++k) outCols[6 * f + k] = c;
            }
        };

        buildLitQuadTris(
            m_solver.ribsPosX(), m_solver.ribsPosY(), m_solver.ribsPosZ(), m_solver.ribsQuadIndices(),
            0.86f, 0.68f, 0.43f, m_ribsTriPositions, m_ribsTriColors);
        buildLitQuadTris(
            m_solver.louversPosX(), m_solver.louversPosY(), m_solver.louversPosZ(), m_solver.louversQuadIndices(),
            0.76f, 0.56f, 0.34f, m_louversTriPositions, m_louversTriColors);
    }

    void drawSceneGeometry(Renderer& renderer)
    {
        // Step 7: Shaded Scale-Translation (or Warped Guide) Quad Mesh
        if (m_showStep7Mesh && !m_triPositions.empty()) {
            const Color* cols = m_showAnalysisColors ? m_triColors.data() : m_neutralTriColors.data();
            renderer.drawMesh(
                m_triPositions.data(),
                nullptr,
                cols,
                static_cast<int>(m_triPositions.size()),
                nullptr,
                0,
                false);

            if (m_showMeshEdges && !m_edgeIndices.empty()) {
                renderer.setLineWidth(1.0f);
                renderer.drawMeshEdges(
                    m_currVerts.data(),
                    m_edgeIndices.data(),
                    m_edgeColors.data(),
                    static_cast<int>(m_edgeColors.size()));
            }
        }

        // Step 8: 3D Smooth Curved Primary Ribs (0.4 x 0.8 m)
        if (m_showStep8Ribs && !m_ribsTriPositions.empty()) {
            renderer.drawMesh(
                m_ribsTriPositions.data(),
                nullptr,
                m_ribsTriColors.data(),
                static_cast<int>(m_ribsTriPositions.size()),
                nullptr,
                0,
                false);
        }

        // Step 8: 3D Mitered Secondary Louvers (0.25 x 0.30 m @ 0.40 m pitch)
        if (m_showStep8Louvers && !m_louversTriPositions.empty()) {
            renderer.drawMesh(
                m_louversTriPositions.data(),
                nullptr,
                m_louversTriColors.data(),
                static_cast<int>(m_louversTriPositions.size()),
                nullptr,
                0,
                false);
        }

        // Step 5: Twisted Hyperboloid Guide Rulings
        if (m_showStep5Hyperboloid) {
            zDisplayGraphSetting gDisp;
            gDisp.showVertices = false;
            gDisp.showEdges = true;
            gDisp.edgeColor = Color(240.0f / 255.0f, 0.0f, 140.0f / 255.0f, 0.72f);
            gDisp.edgeWidth = 1.4f;
            scene().draw(m_solver.hyperboloidGuideGraph(), gDisp);
        }

        // Step 6: Scaled + Translated Horizontal Rails Along Surface
        if (m_showStep6HorizRails) {
            zDisplayGraphSetting rDisp;
            rDisp.showVertices = false;
            rDisp.showEdges = true;
            rDisp.edgeColor = Color(240.0f / 255.0f, 0.0f, 140.0f / 255.0f, 1.0f);
            rDisp.edgeWidth = 2.0f;
            scene().draw(m_solver.horizontalRailsGraph(), rDisp);
        }

        // Step 4: Bottom & Top 4-Arc Input Curves + Bi-Arc Tangent Junction Nodes
        if (m_showStep4InputArcs) {
            if (!m_botEndArcLines.empty()) {
                renderer.drawLines(
                    m_botEndArcLines.data(),
                    static_cast<int>(m_botEndArcLines.size()),
                    Color(0.0f, 0.58f, 0.84f, 1.0f), // Cyan end-cap bi-arcs (matches Beacon diagram Step 3)
                    2.8f);
            }
            if (!m_botSideArcLines.empty()) {
                renderer.drawLines(
                    m_botSideArcLines.data(),
                    static_cast<int>(m_botSideArcLines.size()),
                    Color(0.10f, 0.14f, 0.22f, 1.0f), // Dark ink side bi-arcs
                    2.8f);
            }
            if (!m_topArcLines.empty()) {
                renderer.drawLines(
                    m_topArcLines.data(),
                    static_cast<int>(m_topArcLines.size()),
                    Color(240.0f / 255.0f, 0.0f, 140.0f / 255.0f, 1.0f), // Brand magenta upper rail
                    2.8f);
            }

            if (!m_junctionPts.empty()) {
                renderer.setColor(Color(0.05f, 0.07f, 0.12f, 1.0f));
                renderer.setPointSize(7.0f);
                renderer.drawPoints(m_junctionPts.data(), static_cast<int>(m_junctionPts.size()));
            }
        }

        // Vertical Slab Height Lines (b -> T) & Common Interior Slab Elevation Points (orange dots)
        if ((m_showStep4InputArcs || m_showStep6HorizRails) && !m_slabLineVerts.empty()) {
            renderer.drawLines(
                m_slabLineVerts.data(),
                static_cast<int>(m_slabLineVerts.size()),
                Color(0.78f, 0.34f, 0.28f, 1.0f),
                2.0f);
            if (!m_slabOuterPts.empty()) {
                renderer.setColor(Color(0.06f, 0.08f, 0.12f, 1.0f));
                renderer.setPointSize(7.5f);
                renderer.drawPoints(m_slabOuterPts.data(), static_cast<int>(m_slabOuterPts.size()));
            }
            if (!m_slabCommonPts.empty()) {
                renderer.setColor(Color(0.98f, 0.38f, 0.04f, 1.0f)); // Orange common slab-height points
                renderer.setPointSize(8.5f);
                renderer.drawPoints(m_slabCommonPts.data(), static_cast<int>(m_slabCommonPts.size()));
            }
        }
    }

    void frameCameraOnGeometry(float distanceScale = 1.30f)
    {
        if (m_currVerts.empty()) return;
        Vec3 mn = m_currVerts[0];
        Vec3 mx = m_currVerts[0];
        for (const auto& p : m_currVerts) {
            mn.x = std::min(mn.x, p.x); mn.y = std::min(mn.y, p.y); mn.z = std::min(mn.z, p.z);
            mx.x = std::max(mx.x, p.x); mx.y = std::max(mx.y, p.y); mx.z = std::max(mx.z, p.z);
        }
        const Vec3 size = mx - mn;
        const float maxSize = std::max({ size.x, size.y, size.z, 1.0f });
        const float diag = std::max(1.0f, size.length());
        camera().setNearPlane(std::max(0.05f, diag * 0.001f));
        camera().setFarPlane(std::max(1000.0f, diag * 15.0f));
        if (auto* app = Application::getInstance()) {
            auto& ctrl = app->getCameraController();
            ctrl.focusOnBounds(mn, mx);
            ctrl.setOrbitDistance(maxSize * distanceScale);
            const Vec3 shiftedCenter = ctrl.getOrbitCenter() - camera().getRight() * (maxSize * 0.15f);
            ctrl.setOrbitCenter(shiftedCenter);
        }
    }

    void processCaptureFrame()
    {
        auto* app = Application::getInstance();
        if (!app) return;

        if (m_captureWarmup < 2) {
            ++m_captureWarmup;
            return;
        }

        const auto saveShot = [&](const char* name) {
            const std::string outPath = (std::filesystem::path(m_captureDir) / name).string();
            app->saveScreenshotToFile(outPath);
        };

        switch (m_captureStep) {
            case 0:
                // Default Step 7 neutral grey load state
                saveShot("01_step7_loaded_grey.png");
                applyWorkflowStage(4);
                break;
            case 1:
                saveShot("02_step4_input_4arcs.png");
                applyWorkflowStage(5);
                break;
            case 2:
                saveShot("03_step5_hyperboloid_guide.png");
                applyWorkflowStage(6);
                break;
            case 3:
                saveShot("04_step6_horizontal_rails.png");
                applyWorkflowStage(7);
                runAnalysis(true);
                break;
            case 4:
                saveShot("05_step7_analyzed_planar.png");
                m_showWarpedGuide = true;
                m_lastShowWarped = true;
                runAnalysis(true);
                break;
            case 5:
                saveShot("06_warped_hyperboloid_comparison.png");
                m_showWarpedGuide = false;
                m_lastShowWarped = false;
                m_showAnalysisColors = false;
                applyWorkflowStage(8);
                rebuildRenderBuffers();
                break;
            case 6:
                saveShot("07_step8_ribs_and_louvers.png");
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

    zSpace::zTsScaleTranslation m_solver;
    std::unique_ptr<SimpleUI> m_ui;

    float m_spacingUI = 5.05f;
    float m_offsetPtsUI = 13.0f;
    float m_numVertDivsUI = 4.0f;
    float m_analysisTolUI = 0.0050f;
    float m_lastAppliedTol = 0.0050f;
    float m_ribSubdivsUI = 8.0f;
    float m_offsetAboveSlabUI = 0.80f;
    float m_offsetBelowSlabUI = 3.60f;
    float m_louverSpacingUI = 0.40f;

    int m_workflowStage = 8;
    bool m_showStep4InputArcs = true;
    bool m_showStep5Hyperboloid = false;
    bool m_showStep6HorizRails = true;
    bool m_showStep7Mesh = false;
    bool m_showStep8Ribs = true;
    bool m_showStep8Louvers = true;
    bool m_useSlabElevations = true;
    bool m_showWarpedGuide = false;
    bool m_lastShowWarped = false;
    bool m_quadDiagMode = false;
    bool m_lastQuadDiagMode = false;
    bool m_hasAnalysis = false;
    bool m_showAnalysisColors = false;
    bool m_showMeshEdges = true;

    std::string m_datasetLabel = "Beacon Facade 4-Arc";
    std::string m_analysisLine = "Ready";
    std::string m_statusLine = "Ready";

    std::string m_captureDir;
    int m_captureStep = -1;
    int m_captureWarmup = 0;

    std::vector<Vec3> m_currVerts;
    std::vector<Vec3> m_triPositions;
    std::vector<Color> m_triColors;
    std::vector<Color> m_neutralTriColors;
    std::vector<int> m_edgeIndices;
    std::vector<Color> m_edgeColors;
    std::vector<Vec3> m_ribsTriPositions;
    std::vector<Color> m_ribsTriColors;
    std::vector<Vec3> m_louversTriPositions;
    std::vector<Color> m_louversTriColors;
    std::vector<Vec3> m_junctionPts;
    std::vector<Vec3> m_botEndArcLines;
    std::vector<Vec3> m_botSideArcLines;
    std::vector<Vec3> m_topArcLines;
    std::vector<Vec3> m_slabLineVerts;
    std::vector<Vec3> m_slabCommonPts;
    std::vector<Vec3> m_slabOuterPts;
};

ALICE2_REGISTER_SKETCH_AUTO(zSpaceScaleTranslationSketch)

#endif // __MAIN__

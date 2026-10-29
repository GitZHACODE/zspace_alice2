#define __MAIN__
#ifdef __MAIN__

#include <zspace/interface.h>
#include <zspace/slicing.h>
#include <zspace/zSlicingSettings.h>
#include <zspace/zSlicerViewValidation.h>
#include <zspace/zSlicerPathMarkers.h>
#include <zspace/zSlicerTiming.h>
#include <zspace/zSlicerFieldPreview.h>

// clang-format off
#include <windows.h>
#include <commdlg.h>
// clang-format on

#include <alice2.h>
#include <sketches/SketchRegistry.h>

// End-to-end functional test for the HarmonicSlicer port
// (zspace_toolsets/include/zspace/zToolsets/fabrication/slicing/zSlicer/HarmonicSlicer.h).
// Loads a USD input and runs the two-method comparison pipeline on it
// (Methods 1-2 + cross-method metrics), not just a compile/link probe.

#include <nlohmann/json.hpp>
#include <cctype>
#include <stdexcept>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace alice2;

namespace {

// Slicer picked by the "Harmonic | Topology" button group. kSlicerNone (no
// button lit) = inputs loaded and displayed only, nothing computed.
constexpr int kSlicerNone = -1;
constexpr int kSlicerHarmonic = 0;
constexpr int kSlicerTopology = 1;
constexpr int kHarmonicModeCount = 10;
constexpr int kTopologyModeCount = 9;
using InspectorPath = alice2::slicing::InspectorPathSnapshot;
using InspectorPaths = std::vector<InspectorPath>;

inline int displayModeCount(int slicer)
{
    if (slicer == kSlicerNone) return 1;
    return slicer == kSlicerTopology ? kTopologyModeCount : kHarmonicModeCount;
}

const char* displayModeName(int slicer, int mode)
{
    static const char* harmonic[kHarmonicModeCount] = {
        "Input preview (transparent block, bracing, slot point)",
        "Input mesh (colored top/bottom)", "Tetrahedra volume mesh",
        "Method 2 (u,v) check",
        "Tracing curves (failed red, top success green)",
        "Print paths + path / width / FaceTag fields", "Print mesh (rectangle lofts)",
        "Sampled path per layer (up/down: layer) - 3D + 2D targets",
        "Raw SDF contour per layer (up/down: layer) - before PrintSyn",
        "FaceTag samples + ownership field (up/down: layer)"};
    static const char* topology[kTopologyModeCount] = {
        "Input preview (transparent block, bracing, slot point)",
        "Input mesh (colored top/bottom)", "V-loops + vertex value t (blue 0 -> red 1)",
        "Section meshes", "Print paths + path / width / FaceTag fields", "Print mesh (rectangle lofts)",
        "Sampled path per layer (up/down: layer) - 3D + 2D targets",
        "Raw SDF contour per layer (up/down: layer) - before PrintSyn",
        "FaceTag samples + ownership field (up/down: layer)"};
    if (slicer == kSlicerNone) return "Input preview (transparent block, bracing, slot point)";
    const int count = displayModeCount(slicer);
    const int m = ((mode % count) + count) % count;
    return slicer == kSlicerTopology ? topology[m] : harmonic[m];
}

using alice2::slicing::defaultSlicerSettings;
using alice2::slicing::defaultPrintSynSettings;
using alice2::slicing::applySlicerJson;
using alice2::slicing::applyPrintSynJson;

// Native Windows "Open File" dialog, same pattern as userSrc/zspace/sketch.cpp.
// Returns true and fills outPath if the user picked an existing file; false
// (outPath untouched) if they cancelled.
inline bool openFileDialog(const char* filter, const char* title, std::string& outPath)
{
    char fileBuffer[MAX_PATH] = {0};
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = fileBuffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn)) {
        outPath = fileBuffer;
        return true;
    }
    return false;
}

} // namespace

class HarmonicSlicerTestSketch : public ISketch {
public:
    std::string getName() const override { return "Slicer End-to-End Test (Harmonic | Topology)"; }
    std::string getDescription() const override { return "Loads a tagged USD mesh and runs the HarmonicSlicer or TopologySlicer pipeline, then extracts paths and maps them directly to 3D."; }
    std::string getAuthor() const override { return "alice2 + zspace_toolsets"; }

    void setup() override
    {
        scene().setBackgroundColor(Color(1.0f, 1.0f, 1.0f, 1.0f));
        scene().setShowGrid(false);
        scene().setShowAxes(false);

        setupUI();

        m_viewValidation = alice2::slicing::SlicerViewValidation::read();
        if (!m_viewValidation.Input.empty()) {
          try {
            m_inputMeshPath = m_viewValidation.Input;
            loadInputs();
            if (!m_inputLoaded) throw std::runtime_error("Validation input did not load.");
            m_settings._HIsoRelaxIterations = m_viewValidation.RelaxIterations;
            if(m_viewValidation.HeightOptimization>=0)m_settings.HPrintHeight.Enabled=m_viewValidation.HeightOptimization!=0;
            if(m_viewValidation.HeightSurfaces>=0)m_settings.HPrintHeight.OptimizeSurfaces=m_viewValidation.HeightSurfaces!=0;
            m_slicerChoice = m_lastSlicerChoice = m_viewValidation.Topology ? kSlicerTopology : kSlicerHarmonic;
            runSelectedSlicer();
            zSpace::zSlicer* slicer = m_viewValidation.Topology ? static_cast<zSpace::zSlicer*>(m_topoSlicer.get()) : static_cast<zSpace::zSlicer*>(m_slicer.get());
            const bool complete = slicer && zSpace::zFnGraph(const_cast<zSpace::PrintPath&>(slicer->getResultantPrintPath()).graph).numEdges() > 0;
            if (!complete && (!m_viewValidation.AllowPartial || !slicer || slicer->getPrintPaths3D().empty()))
                throw std::runtime_error("Validation pipeline did not produce a complete print path.");
            if (!complete) std::cerr << "[SlicerViewValidation] PARTIAL, noncompliant: " << slicer->getStatus() << std::endl;
            if (!m_viewValidation.Output.empty()) {
                std::string error;
                if (!complete || !zSpace::zTsIO::exportPrintPathUSD(slicer->getResultantPrintPath(), m_inputMeshPath,
                    m_viewValidation.Output, static_cast<int>(slicer->getPrintPaths3D().size()), &error, 0, &slicer->getPrintPaths3D()))
                    throw std::runtime_error("Validation USD export failed: " + error);
            }
            auto* app = Application::getInstance();
            if (!app->getCameraController().hasSavedCamera(0)) throw std::runtime_error("Validation requires the user's saved F1 camera.");
            app->getCameraController().loadCamera(0);
            m_displayMode = (std::min)(m_viewValidation.DisplayMode, displayModeCount(m_slicerChoice) - 1);
            m_inspectLayer = m_viewValidation.Layer;
            const int printMeshMode = m_viewValidation.Topology ? 5 : 6;
            if (m_displayMode == printMeshMode) app->getRenderer().setSceneRenderMode(SceneRenderMode::Regular);
            m_validationCaptureFrame = app->getFrameCount() + 4;
            std::cout << "[SlicerViewValidation] mode=" << (m_viewValidation.Topology ? "topology" : "harmonic")
                      << " relaxIterations=" << m_viewValidation.RelaxIterations << " heightSurfaces=" << m_settings.HPrintHeight.OptimizeSurfaces << " view=F1 printLayers=" << slicer->getPrintPaths3D().size() << std::endl;
          } catch (const std::exception& error) {
            std::cerr << "[SlicerViewValidation] FAILED: " << error.what() << std::endl;
            Application::getInstance()->quit();
          }
        }

        // No default geometry and no default import - the scene starts
        // empty; press 'i' to pick a USD and run the pipeline on it.
    }

    void cleanup() override { m_fieldPreview.clear(); }

    // Sliders write straight into the bound variable on drag (SimpleUI has
    // no per-change callback), so the only way to notice an edit is to
    // snapshot the last known value and compare every frame - same pattern
    // as userSrc/zspace/sketch.cpp's setupUI()/m_lastSettings.
    // Nothing runs until a slicer button is picked; then that slicer (only)
    // is created and run on the loaded inputs, and reruns on its sliders.
    void update(float /*deltaTime*/) override
    {
        bool rerun = false;
        if (m_slicerChoice != m_lastSlicerChoice) {
            m_lastSlicerChoice = m_slicerChoice;
            m_displayMode = 0;
            rerun = true;
        }
        if (m_layerSpacing != m_lastLayerSpacing) {
            m_lastLayerSpacing = m_layerSpacing;
            rerun = true;
        }
        if (rerun && m_inputLoaded && m_slicerChoice != kSlicerNone) runSelectedSlicer();
        if (m_exportAction == 0) {
            m_exportAction = -1;
            exportPrintPath();
        }
        if (m_validationCaptureFrame >= 0 && Application::getInstance()->getFrameCount() >= m_validationCaptureFrame) {
            m_validationCaptureFrame = -1;
            // Invoke Alice's own saved-camera framebuffer capture after real
            // frames have rendered. No report/mock renderer substitutes for it.
            Application::getInstance()->takeScreenshotAllCameras();
            Application::getInstance()->quit();
        }
    }

    bool onKeyPress(unsigned char key, int, int) override
    {
        if (key == 'i' || key == 'I') {
            std::string picked;
            // OPENFILENAMEA's filter is pairs of "label\0pattern\0", double-null
            // terminated - the trailing "" closes the last pattern string.
            const char* meshFilter = "USD inputs (*.usd;*.usda;*.usdc)\0*.usd;*.usda;*.usdc\0";
            if (!openFileDialog(meshFilter, "Select slicer input", picked)) {
                return true; // user cancelled - leave the current mesh/results alone
            }
            m_inputMeshPath = picked;

            loadInputs(); // display only - pick a slicer button to compute
            focusCameraOnInputMesh();
            return true;
        }

        if (key == 'k') {
            m_displayMode = (m_displayMode + 1) % displayModeCount(m_slicerChoice);
            return true;
        }

        if (key == 'K') {
            const int count = displayModeCount(m_slicerChoice);
            m_displayMode = (m_displayMode - 1 + count) % count;
            return true;
        }

        // Layer inspector: up / down arrow (alice2 key codes 201 / 203).
        if ((key == 201 || key == 203) && isLayerInspectorMode()) {
            const int count = inspectorLayerCount();
            if (count > 0) m_inspectLayer = (std::max)(0, (std::min)(count - 1, m_inspectLayer + (key == 201 ? 1 : -1)));
            return true;
        }

        return false;
    }

    bool onMousePress(int button, int state, int x, int y) override
    {
        return m_ui && m_ui->onMousePress(button, state, x, y);
    }

    bool onMouseMove(int x, int y) override
    {
        return m_ui && m_ui->onMouseMove(x, y);
    }

    void draw(Renderer& renderer, Camera&) override
    {
        if (m_slicerChoice == kSlicerNone || (m_slicerChoice == kSlicerTopology && !m_topoSlicer)
            || (m_slicerChoice == kSlicerHarmonic && !m_slicer)) {
            drawInputPreview();
        } else if (m_slicerChoice == kSlicerTopology) {
            switch (m_displayMode) {
                case 0: drawInputPreview(); break;
                case 1: drawInputColored(); drawBracingGraph(); break;
                case 2: drawInputColored(); drawVLoops(); break;
                case 3: drawGhostInputMesh(); drawSectionMeshes(); break;
                case 4: drawGhostInputMesh(); drawFieldPrintPaths(renderer); drawTopologyFieldMinimap(renderer); break;
                case 5: drawPrintMesh(); break;
                case 6: drawGhostInputMesh(); drawLayerInspector(renderer); break;
                case 7: drawGhostInputMesh(); drawLayerInspector(renderer, true); break;
                case 8: drawGhostInputMesh(); drawLayerInspector(renderer, false, true); break;
                default: break;
            }
        } else {
            switch (m_displayMode) {
                case 0: drawInputPreview(); break;
                case 1: drawInputColored(); drawBracingGraph(); break;
                case 2: drawTetWireframe(); break;
                case 3: drawGhostInputMesh(); drawHarmonicIsoUV(); drawUVProbe(renderer); drawUVGradientLegend(renderer); break;
                case 4: drawGhostInputMesh(); drawFailedTraces(renderer); drawTopSuccessTraces(renderer); break;
                case 5: drawGhostInputMesh(); drawFieldPrintPaths(renderer); drawHarmonicFieldMinimap(renderer); break;
                case 6: drawPrintMesh(); break;
                case 7: drawGhostInputMesh(); drawLayerInspector(renderer); break;
                case 8: drawGhostInputMesh(); drawLayerInspector(renderer, true); break;
                case 9: drawGhostInputMesh(); drawLayerInspector(renderer, false, true); break;
                default: break;
            }
        }

        drawSlotAnchor(renderer);

        renderer.setColor(Color(0.08f, 0.09f, 0.12f, 1.0f));
        renderer.drawString(getName(), 10, 28);
        std::ostringstream modeLine;
        modeLine << (m_slicerChoice == kSlicerNone ? "[No slicer] "
                     : m_slicerChoice == kSlicerTopology ? "[Topology] " : "[Harmonic] ")
                 << "'k'/'K' cycle display forward/back (" << (m_displayMode + 1) << "/"
                 << displayModeCount(m_slicerChoice) << ": " << displayModeName(m_slicerChoice, m_displayMode)
                 << ") | 'i' load a USD";
        renderer.drawString(modeLine.str(), 10, 50);
        if (!m_inputLoaded) {
            renderer.drawString("Empty scene - press 'i' to load USD: AG_block, Slicer_bracing, Slicer_slot and Slicer_setting.", 10, 68);
        } else if (m_slicerChoice == kSlicerNone) {
            renderer.drawString("Inputs loaded - pick Harmonic or Topology below to run that slicer.", 10, 68);
        }

        // Drawn last so the slider sits on top of the text/help lines.
        if (m_ui) m_ui->draw(renderer);
    }

private:
    void setupUI()
    {
        m_ui = std::make_unique<SimpleUI>(input());
        // Dark theme = black slider text, readable against this sketch's
        // white background (SimpleUI defaults to white/Light text).
        m_ui->setTheme(SimpleUI::UITheme::Dark);
        // Placed below the widest possible status-line block (up to 16
        // lines starting at y=90, ~20px apart) so it never overlaps it.
        // The only layering input, shared by both slicers (same LayerSpacing -> same layers).
        m_ui->addSlider("Layer Spacing (mm)", Vec2{10.0f, 440.0f}, 220.0f, 0.1f, 200.0f, m_layerSpacing);
        // (u,v) probe - mode 3 only. Drag these to slide the drawUVProbe()
        // marker column through the model and check layer alignment live.
        m_ui->addSlider("Probe U", Vec2{10.0f, 480.0f}, 220.0f, 0.0f, 1.0f, m_probeU);
        m_ui->addSlider("Probe V", Vec2{10.0f, 520.0f}, 220.0f, 0.0f, 1.0f, m_probeV);
        // Which slicer runSelectedSlicer() creates and runs; none lit until
        // the user picks one (loading a USD clears the pick).
        m_ui->addButtonGroup({"Harmonic", "Topology"}, Vec2{10.0f, 560.0f}, 106.0f, 26.0f, 8.0f, m_slicerChoice);
        m_ui->addButtonGroup({"Export USD"}, Vec2{10.0f, 600.0f}, 220.0f, 26.0f, 0.0f, m_exportAction);
    }

    void exportPrintPath()
    {
        auto* slicer = activeSlicer();
        if (!slicer) {
            logFail("Export USD", "Run a slicer and PrintSyn before exporting.");
            return;
        }
        const auto& path = slicer->getResultantPrintPath();
        if (zSpace::zFnGraph(const_cast<zSpace::zObjGraph&>(path.graph)).numVertices() < 2) {
            logFail("Export USD", "No combined print path is available; complete PrintSyn first.");
            return;
        }
        if (!isUSDInput()) {
            logFail("Export USD", "Load a USD input to export input geometry and printpath together.");
            return;
        }
        char fileBuffer[MAX_PATH] = {};
        if (m_inputMeshPath.size() >= MAX_PATH) {
            logFail("Export USD", "Input path is too long for the save dialog.");
            return;
        }
        std::copy(m_inputMeshPath.begin(), m_inputMeshPath.end(), fileBuffer);
        OPENFILENAMEA dialog = {};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = GetActiveWindow();
        dialog.lpstrFilter = "USD files (*.usda;*.usdc;*.usd)\0*.usda;*.usdc;*.usd\0";
        dialog.lpstrFile = fileBuffer;
        dialog.nMaxFile = MAX_PATH;
        dialog.lpstrTitle = "Save input USD with printpath and print mesh";
        dialog.lpstrDefExt = "usda";
        dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetSaveFileNameA(&dialog)) {
            if (CommDlgExtendedError() != 0)
                logFail("Export USD", "Could not open the save dialog.");
            return;
        }
        std::string error;
        if (!alice2::slicing::timeSlicerMethod("exportPrintPathUSD", [&] {
                return zSpace::zTsIO::exportPrintPathUSD(path, m_inputMeshPath, fileBuffer,
                    static_cast<int>(slicer->getPrintPaths3D().size()), &error, 0, &slicer->getPrintPaths3D()); }))
            logFail("Export USD", error);
        else
            logOk("Export USD", std::string("Saved ") + fileBuffer);
    }

    // Console status in the same style as the ts3dp library's own prints:
    // green = success, red = failure, "[source]" names who is reporting
    // (HarmonicSlicer / TopologySlicer / Input). Each slicer step prints
    // its own "[zXxxSlicer::step]" line, so the sketch doesn't repeat them.
    static void logOk(const std::string& source, const std::string& message)
    {
        zSpace::zTsUtils::printSuccess("[" + source + "] " + message);
    }
    static void logInput(const std::string& message)
    {
        std::cout << "\033[37m[Input] " << message << "\033[0m\n" << std::flush;
    }
    static void logFail(const std::string& source, const std::string& message)
    {
        zSpace::zTsUtils::printError("[" + source + "] " + message);
    }

    void focusCameraOnInputMesh()
    {
        const alice2::slicing::SlicerMethodTimer timer("focusCameraOnInputMesh");
        zSpace::zPoint minBB;
        zSpace::zPoint maxBB;
        zSpace::zFnMesh fn(m_inputMesh);
        if (fn.numVertices() == 0) return;
        fn.getBounds(minBB, maxBB);
        const auto includePoint = [&](const zSpace::zPoint& p) {
            minBB.x = (std::min)(minBB.x, p.x);
            minBB.y = (std::min)(minBB.y, p.y);
            minBB.z = (std::min)(minBB.z, p.z);
            maxBB.x = (std::max)(maxBB.x, p.x);
            maxBB.y = (std::max)(maxBB.y, p.y);
            maxBB.z = (std::max)(maxBB.z, p.z);
        };
        if (m_haveBracing) {
            zSpace::zPointArray points;
            zSpace::zFnGraph(m_bracingGraph).getVertexPositions(points);
            for (const auto& p : points) includePoint(p);
        }
        if (m_haveSlotAnchor) includePoint(m_slotAnchor);

        auto* app = Application::getInstance();
        auto& camera = app->getCamera();
        auto& controller = app->getCameraController();
        const Vec3 low(static_cast<float>(minBB.x), static_cast<float>(minBB.y), static_cast<float>(minBB.z));
        const Vec3 high(static_cast<float>(maxBB.x), static_cast<float>(maxBB.y), static_cast<float>(maxBB.z));
        const float radius = (std::max)(0.001f, (high - low).length() * 0.5f);
        if (!std::isfinite(radius)) return;
        const float aspect = (std::max)(0.01f, camera.getAspectRatio());
        const float halfFov = (std::max)(1.0f, (std::min)(170.0f, camera.getFieldOfView()))
                              * 0.00872664626f;
        const float limitingAngle = (std::min)(halfFov, std::atan(std::tan(halfFov) * aspect));
        const float distance = 1.2f * radius / std::sin(limitingAngle);
        const float nearPlane = radius * 1e-4f;
        const float farPlane = distance + radius * 100.0f;
        camera.setNearPlane(nearPlane);
        camera.setFarPlane(farPlane);
        if (camera.getProjectionType() == ProjectionType::Orthographic) {
            const float halfHeight = 1.2f * radius / (std::min)(1.0f, aspect);
            camera.setOrthographic(-halfHeight * aspect, halfHeight * aspect,
                                   -halfHeight, halfHeight, nearPlane, farPlane);
        }
        controller.setOrbitCenter((low + high) * 0.5f);
        controller.setOrbitDistance(distance);
        controller.setPanSpeed(radius * 0.01f);
        controller.setZoomSpeed(radius * 0.1f);
    }

    // Translucent faces plus its own edge wireframe - a backdrop for the
    // overlay display modes (2-4). Never carries per-vertex color: that's
    // what drawInputColored()/mode 1 is for. Edge color is kept close to
    // the face color (same rgb, slightly higher alpha) so the wireframe
    // blends into the faces instead of standing out as a separate layer.
    static zDisplayMeshSetting ghostMeshDisplay()
    {
        zDisplayMeshSetting meshDisplay;
        meshDisplay.showVertices = false;
        meshDisplay.showEdges = true;
        meshDisplay.showFaces = true;
        meshDisplay.faceColor = Color(0.75f, 0.8f, 0.9f, 0.35f);
        meshDisplay.edgeColor = Color(0.75f, 0.8f, 0.9f, 0.45f);
        meshDisplay.edgeWidth = 1.0f;
        return meshDisplay;
    }

    void drawPrintMesh()
    {
        zDisplayMeshSetting display;
        display.showVertices = false;
        display.showEdges = true;
        display.showFaces = true;
        display.faceColor = Color(0.65f, 0.65f, 0.65f, 1.0f);
        display.edgeColor = Color(0.0f, 0.0f, 0.0f, 1.0f);
        display.edgeWidth = 1.0f;
        scene().draw(m_printMesh, display);
    }

    void drawVLoops()
    {
        for (zSpace::zObjGraph& g : m_topoSlicer->getVLoops())
            scene().draw(g, Display::lines(Color(0.05f, 0.05f, 0.05f, 1.0f), 3.0f));
    }

    void drawSectionMeshes()
    {
        zDisplayMeshSetting wire;
        wire.showVertices = false;
        wire.showFaces = false;
        wire.showEdges = true;
        wire.edgeColor = Color(0.1f, 0.25f, 0.8f, 0.8f);
        wire.edgeWidth = 1.0f;
        for (zSpace::zObjMesh& m : m_topoSlicer->getIsoMeshes()) scene().draw(m, wire);
    }

    // The boundary slot anchor, in every display mode.
    void drawSlotAnchor(Renderer& renderer)
    {
        if (!m_haveSlotAnchor) return;
        renderer.drawPoint(Vec3(static_cast<float>(m_slotAnchor.x), static_cast<float>(m_slotAnchor.y),
                                static_cast<float>(m_slotAnchor.z)),
                           Color(0.85f, 0.1f, 0.6f, 1.0f),
                           (m_displayMode == 0 || m_slicerChoice == kSlicerNone) ? 20.0f : 12.0f);
    }

    void drawInputPreview()
    {
        if (!m_inputLoaded) return;
        auto display = ghostMeshDisplay();
        display.faceColor = Color(0.75f, 0.8f, 0.9f, 0.2f);
        display.edgeColor = Color(0.15f, 0.2f, 0.3f, 0.8f);
        display.edgeWidth = 1.5f;
        scene().draw(m_inputMesh, display);
        if (m_haveBracing)
            scene().draw(m_bracingGraph, Display::lines(Color(0.9f, 0.45f, 0.0f, 1.0f), 6.0f));
    }

    void drawBracingGraph()
    {
        if (m_haveBracing)
            scene().draw(m_bracingGraph, Display::lines(Color(0.9f, 0.45f, 0.0f, 1.0f), 3.0f));
    }

    void drawGhostInputMesh()
    {
        scene().draw(m_inputMesh, ghostMeshDisplay());
    }

    // Splits the input mesh into one mesh per face group - bottom, top and
    // every side_<n> - with its own vertices, so each draws in one flat color
    // (the renderer only blends vertex colors): bottom black, top white, side
    // group n by its width. The side colors use the width field's own rule -
    // the group's BoundaryWidth (padded with the last entry like the
    // slicers), converted to the stored value (zTsField::printedWidth()),
    // through zTsUtils::colorWidthField() with m_widthPalette (every group's
    // value, plus the bracing's when a bracing graph is loaded) as the palette.
    // drawFieldMinimap() colors the width field with the same palette, so a
    // group and the field region it prints match. Groups of equal width share
    // a color, as they do in the field.
    void buildInputGroups(std::string& outSummary)
    {
        const alice2::slicing::SlicerMethodTimer timer("buildInputGroups");
        m_inputGroupMeshes.clear();
        m_inputGroupColors.clear();
        m_widthPalette.clear();
        zSpace::zFnMesh fn(m_inputMesh);
        zSpace::zPointArray positions;
        fn.getVertexPositions(positions);
        zSpace::zIntArray connects, counts;
        fn.getPolygonData(connects, counts);
        if (positions.empty() || counts.size() != m_faceTags.size()) return;

        // Group key per face: -2 bottom, -1 top, n >= 0 side_<n>.
        constexpr int kBottomKey = -2, kTopKey = -1;
        zSpace::zIntArray faceOffset(counts.size(), 0);
        std::map<int, zSpace::zIntArray> groupFaces;
        int offset = 0;
        for (size_t f = 0; f < counts.size(); ++f) {
            faceOffset[f] = offset;
            offset += counts[f];
            int key = (f < m_faceWidthIds.size()) ? m_faceWidthIds[f] : 0;
            if (m_faceTags[f] == static_cast<int>(zSpace::VertexTag::Bottom)) key = kBottomKey;
            else if (m_faceTags[f] == static_cast<int>(zSpace::VertexTag::Top)) key = kTopKey;
            groupFaces[key].push_back(static_cast<int>(f));
        }

        // The width field palette: printed width of every side group (+ bracing).
        const auto printed = [&](int group) {
            return zSpace::zTsField::printedWidth(zSpace::zTsField::widthOfGroup(m_settings.BoundaryWidth, group), true,
                                                  m_settings.DoubleBoundary, m_settings.OverlapWidth);
        };
        zSpace::zScalarArray sideValues;
        for (const auto& entry : groupFaces)
            if (entry.first >= 0) sideValues.push_back(printed(entry.first));
        m_widthPalette = sideValues;
        if (m_haveBracing)
            m_widthPalette.push_back(zSpace::zTsField::printedWidth(m_settings.BracingWidth, false,
                                                                    m_settings.DoubleBoundary, m_settings.OverlapWidth));
        zSpace::zColorArray sideColors;
        zSpace::zTsUtils::colorWidthField(sideValues, sideColors, &m_widthPalette);

        // One exploded mesh per group (std::map: bottom, top, then side_0,
        // side_1, ...), each created in place in the pre-sized array - a
        // zObjMesh is never moved after creation.
        m_inputGroupMeshes.assign(groupFaces.size(), zSpace::zObjMesh());
        size_t groupIndex = 0;
        size_t sideIndex = 0;
        std::ostringstream summary;
        for (const auto& entry : groupFaces) {
            std::vector<int> remap(positions.size(), -1);
            zSpace::zPointArray groupPositions;
            zSpace::zIntArray groupCounts, groupConnects;
            for (int f : entry.second) {
                groupCounts.push_back(counts[f]);
                for (int k = 0; k < counts[f]; ++k) {
                    const int v = connects[faceOffset[f] + k];
                    if (remap[v] < 0) {
                        remap[v] = static_cast<int>(groupPositions.size());
                        groupPositions.push_back(positions[v]);
                    }
                    groupConnects.push_back(remap[v]);
                }
            }
            zSpace::zFnMesh(m_inputGroupMeshes[groupIndex++]).create(groupPositions, groupCounts, groupConnects);

            Color color(1.0f, 1.0f, 1.0f, 1.0f);
            if (entry.first == kBottomKey) {
                color = Color(0.0f, 0.0f, 0.0f, 1.0f);
                summary << " bottom=" << entry.second.size();
            } else if (entry.first == kTopKey) {
                summary << " top=" << entry.second.size();
            } else {
                const zSpace::zColor& c = sideColors[sideIndex++];
                color = Color(c.r, c.g, c.b, 1.0f);
                summary << " side_" << entry.first << "=" << entry.second.size() << " (w=" << printed(entry.first) << ")";
            }
            m_inputGroupColors.push_back(color);
        }
        outSummary = summary.str();
    }

    // Mode a: the input mesh as its exploded face groups (buildInputGroups()):
    // bottom black, top white, side_<n> in the width field's colors.
    void drawInputColored()
    {
        zDisplayMeshSetting group;
        group.showVertices = false;
        group.showEdges = true;
        group.edgeColor = Color(0.35f, 0.35f, 0.35f, 1.0f);
        group.edgeWidth = 1.0f;
        group.showFaces = true;
        for (size_t g = 0; g < m_inputGroupMeshes.size() && g < m_inputGroupColors.size(); ++g) {
            group.faceColor = m_inputGroupColors[g];
            scene().draw(m_inputGroupMeshes[g], group);
        }
    }

    // Every failed trace (getTraceGraphs(), one per tet vertex; valid =
    // getVolumetricParameterizationValidity()) in red, with its end point. A
    // trace that failed at its start has no graph - its vertex is the end point.
    void drawFailedTraces(Renderer& renderer)
    {
        zSpace::zObjGraphArray& graphs = m_slicer->getTraceGraphs();
        const auto& valid = m_slicer->getVolumetricParameterizationValidity();
        const auto& vertices = m_slicer->getTetVolume().Vertices;
        const auto& markers = m_slicer->getTetVolume().vertextag;
        const Color color(0.95f, 0.15f, 0.1f, 1.0f);
        for (size_t i = 0; i < graphs.size() && i < valid.size() && i < vertices.size(); ++i) {
            if (valid[i] || (i < markers.size() && markers[i] == zSpace::VertexTag::Bottom)) continue;
            drawTrace(renderer, graphs[i], vertices[i].Position, color);
        }
    }

    // The successful traces of the Top-tagged vertices in
    // green - failures are drawFailedTraces()'s job.
    void drawTopSuccessTraces(Renderer& renderer)
    {
        zSpace::zObjGraphArray& graphs = m_slicer->getTraceGraphs();
        const auto& valid = m_slicer->getVolumetricParameterizationValidity();
        const auto& vertices = m_slicer->getTetVolume().Vertices;
        const Color color(0.1f, 0.7f, 0.2f, 1.0f);
        const auto& tags = m_slicer->getTetVolume().vertextag;
        for (size_t i = 0; i < tags.size(); ++i) {
            if (tags[i] != static_cast<int>(zSpace::VertexTag::Top)) continue;
            if (i >= graphs.size() || i >= valid.size() || i >= vertices.size() || !valid[i]) continue;
            drawTrace(renderer, graphs[i], vertices[i].Position, color);
        }
    }

    // One trace: its graph and its end point (the graph's last vertex, or
    // start when the trace has no graph).
    void drawTrace(Renderer& renderer, zSpace::zObjGraph& graph, const zSpace::zPoint& start, const Color& color)
    {
        zSpace::zFnGraph fn(graph);
        zSpace::zPoint end = start;
        if (fn.numVertices() > 0) {
            scene().draw(graph, Display::lines(color, 2.0f));
            zSpace::zPointArray positions;
            fn.getVertexPositions(positions);
            end = positions.back();
        }
        renderer.drawPoint(Vec3(static_cast<float>(end.x), static_cast<float>(end.y), static_cast<float>(end.z)), color, 6.0f);
    }

    // Replays computeProcessedPrintPaths()'s cached result: the slicer's
    // per-layer 3D print paths, inner track blue and outer track pink (a
    // single-perimeter path has no outer track, so it is all blue).
    static Color faceTagColor(int tag) {
        const auto c = alice2::slicing::slicerFaceTagColor(tag);
        return Color(c.r,c.g,c.b,c.a);
    }
    const zSpace::zTsField::FaceTagSource *faceTagSource(int layer) const {
        const std::vector<zSpace::zTsField::FaceTagSource> *sources=nullptr;
        if(m_slicerChoice==kSlicerTopology && m_topoSlicer)sources=&m_topoSlicer->getFaceTagSources();
        if(m_slicerChoice==kSlicerHarmonic && m_slicer)sources=&m_slicer->getFaceTagSources();
        return sources && layer>=0 && layer<int(sources->size())?&(*sources)[layer]:nullptr;
    }
    void drawFieldPrintPaths(Renderer& renderer)
    {
        const Color innerColor(0.1f, 0.35f, 0.95f, 1.0f);
        const Color outerColor(1.0f, 0.4f, 0.75f, 1.0f);
        for (const auto& segments : m_fieldPathSegments) {
            for (const PathSegment& seg : segments) {
                renderer.drawLine(seg.a, seg.b, seg.faceTag ? faceTagColor(seg.faceTag) : seg.outer ? outerColor : innerColor, 4.0f);
            }
        }
        for (const auto& markers : m_processedMarkers) drawProcessedPathMarkers(markers);
    }

    // Draw cached final targets in batches. LEQUAL keeps them visible on their
    // own path lines while retaining occlusion by nearer geometry.
    static void drawProcessedPathMarkers(const alice2::slicing::PrintPathMarkers& markers)
    {
        glPushAttrib(GL_CURRENT_BIT | GL_POINT_BIT | GL_DEPTH_BUFFER_BIT);
        glDepthFunc(GL_LEQUAL);
        glPointSize(5.0f);
        glColor4f(0.0f, 0.0f, 0.0f, 1.0f);
        glBegin(GL_POINTS);
        for (const auto& p : markers.Targets) glVertex3f(p.x, p.y, p.z);
        glEnd();
        glPointSize(9.0f);
        glColor4f(1.0f, 0.85f, 0.0f, 1.0f);
        glBegin(GL_POINTS);
        for (const auto& p : markers.Features) glVertex3f(p.x, p.y, p.z);
        glEnd();
        glPopAttrib();
    }

    // Both slicers: every layer has its own fields on one shared grid (the
    // harmonic layers' in their unrolled charts) - shows layer 0.
    void drawHarmonicFieldMinimap(Renderer& renderer)
    {
        if (!m_slicer) return;
        const auto& pathFields = m_slicer->getPathFields();
        const auto& widthFields = m_slicer->getWidthFields();
        if (pathFields.empty() || widthFields.empty()) return;
        drawFieldMinimap(renderer, m_slicer->getScalarField(), pathFields[0], widthFields[0], "Path (layer 0)");
    }

    void drawTopologyFieldMinimap(Renderer& renderer)
    {
        if (!m_topoSlicer) return;
        const auto& pathFields = m_topoSlicer->getPathFields();
        const auto& widthFields = m_topoSlicer->getWidthFields();
        if (pathFields.empty() || widthFields.empty()) return;
        drawFieldMinimap(renderer, m_topoSlicer->getScalarField(), pathFields[0], widthFields[0], "Path (layer 0)");
    }

    // Raw and sampled inspectors share the selected layer (up/down arrows).
    bool isLayerInspectorMode() const
    {
        return (m_slicerChoice == kSlicerHarmonic && m_slicer && m_displayMode >= kHarmonicModeCount - 3) ||
               (m_slicerChoice == kSlicerTopology && m_topoSlicer && m_displayMode >= kTopologyModeCount - 3);
    }

    zSpace::zSlicer* activeSlicer() const
    {
        if (m_slicerChoice == kSlicerHarmonic) return m_slicer.get();
        if (m_slicerChoice == kSlicerTopology) return m_topoSlicer.get();
        return nullptr;
    }

    // Layers the inspector steps through: the slicer's field layers (the
    // print paths may be partial if extraction or mapping fails).
    int inspectorLayerCount() const
    {
        if(m_displayMode==displayModeCount(m_slicerChoice)-1 && activeSlicer())
            return static_cast<int>(activeSlicer()->getPrintPaths3D().size());
        if (m_slicerChoice == kSlicerHarmonic && m_slicer) return static_cast<int>(m_slicer->getPathFields().size());
        if (m_slicerChoice == kSlicerTopology && m_topoSlicer) return static_cast<int>(m_topoSlicer->getPathFields().size());
        return 0;
    }

    // One layer (up / down arrow): its 3D print paths in the view (inner
    // blue, outer pink; start vertex green, open ends red, branch vertices
    // orange) and its field in the HUD with the layer's 2D print paths on
    // top. The slicer status line says where post-processing stopped.
    // Extract only the selected layer from the final fields/sections. This
    // cache does not mutate the slicer's processed paths or add work to slicing.
    // It also stays correct if a surface optimizer rebuilds fields in postprocess.
    void cacheRawContourLayer(int layer)
    {
        if (layer < 0 || layer >= static_cast<int>(m_rawContourReady.size()) || m_rawContourReady[layer]) return;
        const alice2::slicing::SlicerMethodTimer timer("cacheRawContourLayer");
        m_rawContourReady[layer] = 1;
        const auto extract = [&](auto& op, const zSpace::zFloatArray* heights, const zSpace::zVectorArray* normals) {
            const auto& fields = op.getPathFields(); const auto& widths = op.getWidthFields(); const auto& tracks = op.getTrackFields();
            const auto& charts = op.getIsoMeshes2D(); const auto& sections = op.getIsoMeshes();
            if (static_cast<size_t>(layer) >= fields.size() || widths.size() != fields.size() || tracks.size() != fields.size()
                || static_cast<size_t>(layer) >= charts.size() || static_cast<size_t>(layer) >= sections.size()) return false;
            std::vector<std::string> messages;
            if (!zSpace::zTsField::extractPrintPaths(op.getScalarField(), fields[layer], widths[layer], tracks[layer], m_rawContours2D[layer], layer, &messages)) return false;
            return zSpace::zTsField::mapPrintPaths(m_rawContours2D[layer], const_cast<zSpace::zObjMesh&>(charts[layer]),
                const_cast<zSpace::zObjMesh&>(sections[layer]), heights, m_rawContours3D[layer], layer, normals);
        };
        bool ok = false;
        if (m_slicerChoice == kSlicerHarmonic && m_slicer && static_cast<size_t>(layer) < m_slicer->getVertexLayerHeights().size())
            ok = extract(*m_slicer, &m_slicer->getVertexLayerHeights()[layer],
                static_cast<size_t>(layer) < m_slicer->getVertexLayerNormals().size() ? &m_slicer->getVertexLayerNormals()[layer] : nullptr);
        else if (m_slicerChoice == kSlicerTopology && m_topoSlicer) ok = extract(*m_topoSlicer, &m_topoSlicer->getVertexLayerHeights(), nullptr);
        if (!ok) {m_rawContours2D[layer].clear();m_rawContours3D[layer].clear();logFail("Raw contour", "Extraction/mapping failed on layer " + std::to_string(layer));return;}
        for (const auto& path : m_rawContours3D[layer])
            m_rawInspector3D[layer].push_back(alice2::slicing::inspectorPathSnapshot(path, m_printSettings.FeatureAngleTol));
        for (const auto& path : m_rawContours2D[layer])
            m_rawInspector2D[layer].push_back(alice2::slicing::inspectorPathSnapshot(path, m_printSettings.FeatureAngleTol));
        size_t vertices = 0; for (const auto& path : m_rawContours2D[layer])
            vertices += zSpace::zFnGraph(const_cast<zSpace::zObjGraph&>(path.graph)).numVertices();
        std::cout << "[RawContourDisplay] layer=" << layer << " original vertices=" << vertices << std::endl;
    }

    void drawLayerInspector(Renderer& renderer, bool raw = false, bool faceTags = false)
    {
        zSpace::zSlicer* slicer = activeSlicer();
        const int count = inspectorLayerCount();
        if (!slicer || count == 0) return;
        m_inspectLayer = (std::max)(0, (std::min)(count - 1, m_inspectLayer));
        const int l = m_inspectLayer;
        if (raw) cacheRawContourLayer(l);

        const auto& layers3D = raw ? m_rawInspector3D : m_processedInspector3D;
        const auto& layers2D = raw ? m_rawInspector2D : m_processedInspector2D;
        const Color innerColor(0.1f, 0.35f, 0.95f, 1.0f);
        const Color outerColor(1.0f, 0.4f, 0.75f, 1.0f);
        const auto toVec3 = [](const zSpace::zPoint& p) {
            return Vec3(static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z));
        };
        std::ostringstream info;
        info << (raw ? "Raw contour layer " : "Sampled path layer ") << l << " / " << (count - 1)
             << " (up/down arrow)" << (raw ? " | before sampling" : faceTags ? " | FaceTag colours" : " | targets: black | outer junctions: yellow");
        if (l < static_cast<int>(layers3D.size())) {
            info << " | paths: " << layers3D[l].size();
            for (size_t k = 0; k < layers3D[l].size(); ++k) {
                const InspectorPath& path = layers3D[l][k];
                const auto& positions = path.Points;
                const auto& edges = path.EdgeConnects;
                if (positions.empty()) continue;
                for (size_t e = 0; e + 1 < edges.size(); e += 2) {
                    const int a = edges[e], b = edges[e + 1];
                    const bool outer = static_cast<size_t>(a) < path.TrackIds.size() &&
                                       static_cast<size_t>(b) < path.TrackIds.size() &&
                                       0.5f * (path.TrackIds[a] + path.TrackIds[b]) >= 0.5f;
                    renderer.drawLine(toVec3(positions[a]), toVec3(positions[b]), (e/2 < path.EdgeFaceTags.size() && path.EdgeFaceTags[e/2] ? faceTagColor(path.EdgeFaceTags[e/2]) : faceTags ? faceTagColor(0) : outer ? outerColor : innerColor), faceTags ? 2.0f : 4.0f);
                }
                if(faceTags) {
                    GLint depthFunction;glGetIntegerv(GL_DEPTH_FUNC,&depthFunction);glDepthFunc(GL_LEQUAL);
                    for(size_t v=0;v<positions.size();++v)
                        renderer.drawPoint(toVec3(positions[v]),faceTagColor(v<path.FaceTags.size()?path.FaceTags[v]:0),8.f);
                    glDepthFunc(depthFunction);
                }
                if (raw || faceTags) for (size_t v = 0; v < positions.size(); ++v) {
                    if (path.Degree[v] == 1) {
                        renderer.drawPoint(toVec3(positions[v]), Color(0.95f, 0.1f, 0.1f, 1.0f), 10.0f);
                    } else if (path.Degree[v] > 2) {
                        renderer.drawPoint(toVec3(positions[v]), Color(1.0f, 0.55f, 0.0f, 1.0f), 10.0f);
                    }
                }
                if (raw || faceTags) renderer.drawPoint(toVec3(positions[0]), Color(0.1f, 0.75f, 0.2f, 1.0f), 12.0f);
                if (k == 0)
                    info << " | first path: " << positions.size() << " vertices, " << edges.size() / 2 << " edges, "
                         << path.OpenEnds << " open ends, " << path.Branches << " branch vertices";
            }
        } else {
            info << " | no print paths on this layer";
        }
        if (!raw && !faceTags && static_cast<size_t>(l) < m_processedMarkers.size())
            drawProcessedPathMarkers(m_processedMarkers[l]);

        const InspectorPaths* paths2D =
            (l < static_cast<int>(layers2D.size())) ? &layers2D[l] : nullptr;
        const std::string label = std::string(raw ? "Raw contour (layer " : "Sampled path (layer ") + std::to_string(l) + ")";
        if (m_slicerChoice == kSlicerHarmonic && m_slicer) {
            const auto& pathFields = m_slicer->getPathFields();
            const auto& widthFields = m_slicer->getWidthFields();
            if (l < static_cast<int>(pathFields.size()) && l < static_cast<int>(widthFields.size()))
                drawFieldMinimap(renderer, m_slicer->getScalarField(), pathFields[l], widthFields[l], label.c_str(), paths2D, !raw && !faceTags, l);
        } else if (m_slicerChoice == kSlicerTopology && m_topoSlicer) {
            const auto& pathFields = m_topoSlicer->getPathFields();
            const auto& widthFields = m_topoSlicer->getWidthFields();
            if (l < static_cast<int>(pathFields.size()) && l < static_cast<int>(widthFields.size()))
                drawFieldMinimap(renderer, m_topoSlicer->getScalarField(), pathFields[l], widthFields[l], label.c_str(),
                                 paths2D, !raw && !faceTags, l);
        }

        renderer.setColor(Color(0.08f, 0.09f, 0.12f, 1.0f));
        renderer.drawString(info.str(), 10, 68);
        renderer.drawString("Slicer status: " + slicer->getStatus(), 10, 86);
        if(faceTags) {
            std::map<int,int> counts,pieces;
            if(l<int(layers2D.size()))for(const auto &path:layers2D[l]) {
                for(const auto& entry:path.FaceTagPointCounts)counts[entry.first]+=entry.second;
                for(const auto& entry:path.FaceTagPieceCounts)pieces[entry.first]+=entry.second;
            }
            std::ostringstream row;row<<"2D sampling: "<<m_printSettings.ResampleLength<<" mm | endpoint-inclusive totals:";
            for(auto entry:counts)row<<" tag "<<entry.first<<" = "<<entry.second<<" ("<<pieces[entry.first]<<" pieces)";
            if(counts.empty())row<<" load an input with separated side tags 3-6";
            renderer.drawString(row.str(),10,106);
            int x=10;for(int tag:{3,4,5,6}){renderer.setColor(faceTagColor(tag));renderer.drawString("FaceTag "+std::to_string(tag),float(x),126);x+=130;}
            renderer.setColor(faceTagColor(0));renderer.drawString("0 = inner / bracing / slot returns",float(x),126);
        }
    }

    // Two colored-quad minimaps stacked top-over-bottom in the top-right
    // corner: a path field above its width field, each tinted per-vertex via
    // zTsUtils' colorPathField()/colorWidthField() and drawn as a flat quad
    // mosaic in screen space - same raw-GL technique as
    // drawUVGradientLegend(), over the field's own quad grid (both slicers
    // build it with triMesh=false). Each panel keeps the grid's real aspect
    // ratio: its longer side is kPanelSize. paths2D (optional) are drawn on
    // both panels in the field's own coordinates (inner blue, outer pink,
    // start vertex green).
    void drawFieldMinimap(Renderer& renderer, const zSpace::zObjMeshScalarField& field,
                          const zSpace::zScalarArray& pathValues, const zSpace::zScalarArray& widthValues,
                          const char* pathLabel, const InspectorPaths* paths2D = nullptr,
                          bool showFeatures = true, int layer = 0)
    {
        if (pathValues.empty() || widthValues.empty()) return;
        const auto& cached = m_fieldPreview.layer(layer, field, pathValues, widthValues,
            m_widthPalette, faceTagSource(layer));
        const auto& grid = m_fieldPreview.grid();
        const auto& minBB = grid.Min;
        const auto& maxBB = grid.Max;
        const double rangeX = (maxBB.x - minBB.x > 1e-9) ? (maxBB.x - minBB.x) : 1.0;
        const double rangeY = (maxBB.y - minBB.y > 1e-9) ? (maxBB.y - minBB.y) : 1.0;

        GLint viewport[4];
        glGetIntegerv(GL_VIEWPORT, viewport);
        const bool haveTags = cached.Tags.Id != 0;
        const int panelCount=haveTags?3:2;
        const float maxPanelHeight=(viewport[3]-200.f-panelCount*24.f)/panelCount;
        const float kPanelSize=(std::min)(440.f,(std::max)(80.f,maxPanelHeight*float((std::max)(rangeX,rangeY)/rangeY)));
        constexpr float kMargin = 100.0f; // all HUDs use a 100px margin from the window edge
        constexpr float kGap = 24.0f; // leaves room for the path label between the two panels
        // Same scale on both axes, so the field shows at its real proportions.
        const float scale = kPanelSize / static_cast<float>((std::max)(rangeX, rangeY));
        const float panelW = static_cast<float>(rangeX) * scale;
        const float panelH = static_cast<float>(rangeY) * scale;
        const float right = static_cast<float>(viewport[2]) - kMargin;
        const float left = right - panelW;
        const float pathTop = kMargin;
        const float pathBottom = pathTop + panelH;
        const float widthTop = pathBottom + kGap;
        const float widthBottom = widthTop + panelH;

        const GLboolean depthEnabled=glIsEnabled(GL_DEPTH_TEST);
        glDisable(GL_DEPTH_TEST); // HUD quads, path lines and target dots share screen depth.

        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(0, viewport[2], viewport[3], 0, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();

        const auto drawPanel = [&](float top, float bottom, const alice2::slicing::FieldPreviewTexture& texture) {
            if (!texture.Id) return;
            const auto toScreen = [&](const zSpace::zPoint& p) {
                const float sx = left + static_cast<float>((p.x - minBB.x) / rangeX) * (right - left);
                const float sy = bottom - static_cast<float>((p.y - minBB.y) / rangeY) * (bottom - top);
                return std::make_pair(sx, sy);
            };

            // Vertex colours use linear texture filtering; category texels use
            // nearest filtering and the original cell's lower-left label.
            const float u0 = texture.Categorical ? 0.f : .5f/texture.Width;
            const float v0 = texture.Categorical ? 0.f : .5f/texture.Height;
            const float u1 = texture.Categorical ? 1.f : 1.f-u0;
            const float v1 = texture.Categorical ? 1.f : 1.f-v0;
            GLint activeTexture = 0; glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
            glPushAttrib(GL_TEXTURE_BIT | GL_ENABLE_BIT | GL_CURRENT_BIT);
            glActiveTexture(GL_TEXTURE0);
            glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, texture.Id);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
            glBegin(GL_QUADS);
            glTexCoord2f(u0,v0); glVertex2f(left,bottom);
            glTexCoord2f(u1,v0); glVertex2f(right,bottom);
            glTexCoord2f(u1,v1); glVertex2f(right,top);
            glTexCoord2f(u0,v1); glVertex2f(left,top);
            glEnd(); glPopAttrib(); glActiveTexture(activeTexture);

            if (paths2D) {
                glLineWidth(2.0f);
                for (const InspectorPath& path : *paths2D) {
                    const auto& pathPoints = path.Points;
                    const auto& pathEdges = path.EdgeConnects;
                    glBegin(GL_LINES);
                    for (size_t e = 0; e + 1 < pathEdges.size(); e += 2) {
                        const int a = pathEdges[e], b = pathEdges[e + 1];
                        const bool outer = static_cast<size_t>(a) < path.TrackIds.size() &&
                                           static_cast<size_t>(b) < path.TrackIds.size() &&
                                           0.5f * (path.TrackIds[a] + path.TrackIds[b]) >= 0.5f;
                        if(e/2<path.EdgeFaceTags.size() && path.EdgeFaceTags[e/2]) {const auto c=faceTagColor(path.EdgeFaceTags[e/2]);glColor4f(c.r,c.g,c.b,1.f);}
                        else if (outer) glColor4f(1.0f, 0.4f, 0.75f, 1.0f);
                        else glColor4f(0.1f, 0.35f, 0.95f, 1.0f);
                        const auto pa = toScreen(pathPoints[a]);
                        const auto pb = toScreen(pathPoints[b]);
                        glVertex2f(pa.first, pa.second);
                        glVertex2f(pb.first, pb.second);
                    }
                    glEnd();
                    if(!showFeatures && !path.FaceTags.empty()) {
                        glPointSize(4.f);glBegin(GL_POINTS);
                        for(size_t v=0;v<pathPoints.size();++v) {
                            const auto c=faceTagColor(path.FaceTags[v]);glColor4f(c.r,c.g,c.b,1.f);
                            const auto point=toScreen(pathPoints[v]);glVertex2f(point.first,point.second);
                        }
                        glEnd();glPointSize(1.f);
                    }
                    const zSpace::zIntArray emptyFeatures;
                    const auto& junctions = showFeatures ? path.FeatureIds : emptyFeatures;
                    if (showFeatures) {
                        glPointSize(4.0f);
                        glColor4f(0.0f, 0.0f, 0.0f, 1.0f);
                        glBegin(GL_POINTS);
                        for (const auto& p : pathPoints) {
                            const auto point = toScreen(p); glVertex2f(point.first, point.second);
                        }
                        glEnd();
                    }
                    glPointSize(7.0f);
                    glColor4f(1.0f, 0.85f, 0.0f, 1.0f);
                    glBegin(GL_POINTS);
                    for (int id : junctions) {
                        if (id < 0 || static_cast<size_t>(id) >= pathPoints.size()) continue;
                        const auto point = toScreen(pathPoints[id]);
                        glVertex2f(point.first, point.second);
                    }
                    glEnd();
                    glPointSize(1.0f);
                    if (!showFeatures && !pathPoints.empty()) { // diagnostic start vertex
                        const auto p0 = toScreen(pathPoints[0]);
                        glPointSize(8.0f);
                        glColor4f(0.1f, 0.75f, 0.2f, 1.0f);
                        glBegin(GL_POINTS);
                        glVertex2f(p0.first, p0.second);
                        glEnd();
                        glPointSize(1.0f);
                    }
                }
                glLineWidth(1.0f);
            }

            glColor4f(0.1f, 0.1f, 0.1f, 1.0f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(left, bottom);
            glVertex2f(right, bottom);
            glVertex2f(right, top);
            glVertex2f(left, top);
            glEnd();
        };

        drawPanel(pathTop, pathBottom, cached.Path);
        drawPanel(widthTop, widthBottom, cached.Width);
        const float tagTop=widthBottom+kGap,tagBottom=tagTop+panelH;
        if(haveTags)drawPanel(tagTop,tagBottom,cached.Tags);

        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);

        if(depthEnabled)glEnable(GL_DEPTH_TEST);
        renderer.setColor(Color(0.05f, 0.05f, 0.05f, 1.0f));
        renderer.drawString(pathLabel, left, pathBottom + 16.0f);
        renderer.drawString("Width", left, widthBottom + 16.0f);
        if(haveTags)renderer.drawString("FaceTag: 3 green | 4 purple | 5 cyan | 6 red | 0 other",left,tagBottom+16.f);
    }

    void drawTetWireframe()
    {
        const zDisplayGraphSetting tetLines = Display::lines(Color(0.15f, 0.35f, 0.85f, 1.0f), 1.0f);
        scene().draw(m_tetWireGraph, tetLines);
    }

    // Method 2's iso-surfaces colored per-vertex by their own (u,v) from
    // colorHarmonicIsoByUV() instead of a flat face color: a checker cell
    // that lands at the same on-screen spot on every layer means the
    // direct-product (u,v) parameterization is aligned across layers; a
    // cell that drifts sideways as you look up through the layers means it
    // isn't.
    void drawHarmonicIsoUV()
    {
        zDisplayMeshSetting uvCheck;
        uvCheck.showVertices = false;
        uvCheck.showEdges = false;
        uvCheck.showFaces = true;
        uvCheck.useMeshColors = true;
        for (zSpace::zObjMesh& m : m_slicer->getIsoMeshes()) scene().draw(m, uvCheck);
    }

    // Builds a plain edge graph of the tet volume TetGen produced (every
    // unique tet edge, deduplicated) - HarmonicSlicer itself only exposes
    // the raw zTetrahedron (Vertices/Volumes), it has no display-facing
    // wire graph output, so it's assembled here on the alice2 side.
    void rebuildTetWireGraph()
    {
        const alice2::slicing::SlicerMethodTimer timer("rebuildTetWireGraph");
        zSpace::zFnGraph clearFn(m_tetWireGraph);
        clearFn.clear();

        const zSpace::zTetrahedron& tet = m_slicer->getTetVolume();
        if (tet.Volumes.empty()) return;

        static constexpr int tetEdges[6][2] = {{0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 3}};
        std::set<std::pair<int, int>> uniqueEdges;
        for (const auto& volume : tet.Volumes) {
            const auto& t = volume.VertexIds;
            for (const auto& e : tetEdges) {
                const int a = t[e[0]];
                const int b = t[e[1]];
                uniqueEdges.insert(a < b ? std::make_pair(a, b) : std::make_pair(b, a));
            }
        }

        zSpace::zIntArray edgeConnects;
        edgeConnects.reserve(uniqueEdges.size() * 2);
        for (const auto& e : uniqueEdges) {
            edgeConnects.push_back(e.first);
            edgeConnects.push_back(e.second);
        }

        zSpace::zPointArray positions;
        positions.reserve(tet.Vertices.size());
        for (const auto& vertex : tet.Vertices) positions.push_back(vertex.Position);
        zSpace::zFnGraph fn(m_tetWireGraph);
        fn.create(positions, edgeConnects);
    }

    // Injects a gradient color (from getIsoMeshUV(), already normalized
    // to [0,1] by computeVolumetricParameterization()) into each Method 2
    // iso-mesh's own vertex colors, so drawHarmonicIsoUV() (mode 3) lets you
    // eyeball whether the same (u,v) spot lines up at the same on-screen
    // position across every layer. u -> red, v -> green.
    void colorHarmonicIsoByUV()
    {
        const alice2::slicing::SlicerMethodTimer timer("colorHarmonicIsoByUV");
        const std::vector<zSpace::zPointArray>& uvBySlice = m_slicer->getIsoMeshUV();
        zSpace::zObjMeshArray& meshes = m_slicer->getIsoMeshes();
        if (uvBySlice.empty() || meshes.empty()) return;

        for (size_t k = 0; k < meshes.size() && k < uvBySlice.size(); ++k) {
            const zSpace::zPointArray& uv = uvBySlice[k];
            if (uv.empty()) continue;

            zSpace::zColorArray colors;
            colors.reserve(uv.size());
            for (const zSpace::zPoint& p : uv) {
                colors.emplace_back(p.x, p.y, 1.0f - p.x * p.y, 1.0f);
            }

            zSpace::zFnMesh fn(meshes[k]);
            if (fn.numVertices() == static_cast<int>(colors.size())) fn.setVertexColors(colors, false);
        }
    }

    // Small UV -> color legend swatch in the screen's top-right corner
    // (mode 3 only), using the exact same (u,v) -> (r,g,b) formula
    // colorHarmonicIsoByUV() paints every iso-mesh vertex with, so the
    // gradient can be read directly instead of guessed from the formula.
    // Bottom-left corner of the square is (u,v)=(0,0), top-right is
    // (1,1); screen Y grows downward, so v=1 sits at the TOP of the
    // square. Raw immediate-mode GL, the same approach SimpleUI's own
    // drawRectFilled() uses internally (it's private, so this can't just
    // call it) - a true per-vertex-color quad is needed for a smooth
    // gradient, a flat-colored drawQuad()/drawRectFilled() per cell would
    // only give a coarse step gradient.
    void drawUVGradientLegend(Renderer& renderer)
    {
        GLint viewport[4];
        glGetIntegerv(GL_VIEWPORT, viewport);

        constexpr float kSize = 200.0f;  // 2x the original 100
        constexpr float kMargin = 100.0f; // all HUDs use a 100px margin from the window edge
        const float left = static_cast<float>(viewport[2]) - kMargin - kSize;
        const float right = static_cast<float>(viewport[2]) - kMargin;
        const float top = kMargin;         // smaller screen Y = top of screen
        const float bottom = kMargin + kSize;

        const auto uvColor = [](float u, float v) { return Color(u, v, 1.0f - u * v, 1.0f); };
        const Color colorU0V0 = uvColor(0.0f, 0.0f);
        const Color colorU1V0 = uvColor(1.0f, 0.0f);
        const Color colorU1V1 = uvColor(1.0f, 1.0f);
        const Color colorU0V1 = uvColor(0.0f, 1.0f);

        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(0, viewport[2], viewport[3], 0, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();

        glBegin(GL_QUADS);
        glColor4f(colorU0V0.r, colorU0V0.g, colorU0V0.b, colorU0V0.a); glVertex2f(left, bottom);  // (0,0)
        glColor4f(colorU1V0.r, colorU1V0.g, colorU1V0.b, colorU1V0.a); glVertex2f(right, bottom); // (1,0)
        glColor4f(colorU1V1.r, colorU1V1.g, colorU1V1.b, colorU1V1.a); glVertex2f(right, top);    // (1,1)
        glColor4f(colorU0V1.r, colorU0V1.g, colorU0V1.b, colorU0V1.a); glVertex2f(left, top);     // (0,1)
        glEnd();

        glColor4f(0.1f, 0.1f, 0.1f, 1.0f);
        glBegin(GL_LINE_LOOP);
        glVertex2f(left, bottom);
        glVertex2f(right, bottom);
        glVertex2f(right, top);
        glVertex2f(left, top);
        glEnd();

        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);

        renderer.setColor(Color(0.05f, 0.05f, 0.05f, 1.0f));
        renderer.drawString("(0,0)", left, bottom + 16.0f);
        renderer.drawString("(1,1)", right - 34.0f, top - 16.0f);
    }

    // Draws one marker per Method 2 layer at the (u,v) the sliders are
    // currently set to, and connects them in layer order (k ascending) into
    // a single polyline. Corresponding points may follow a curved
    // trajectory; they need not form a vertical column.
    // The (u,v) is located on each layer's per-vertex (u,v) attribute
    // (getIsoMeshUV(), triangle + barycentric coordinates - the same way the
    // slicer places the bracing and the slot) and realized on its world
    // section; a layer whose (u,v) does not cover the point is skipped.
    void drawUVProbe(Renderer& renderer)
    {
        if (!std::isfinite(m_probeU) || !std::isfinite(m_probeV)) return;
        const auto& uvw = m_slicer->getIsoMeshUV();
        const zSpace::zObjMeshArray& sections = m_slicer->getIsoMeshes();
        const zSpace::zPoint query(m_probeU, m_probeV, 0.0);
        const double tolerance = 1e-4;
        const Color markerColor(0.05f, 0.05f, 0.05f, 1.0f);

        zSpace::zObjGraph queryGraph;
        {
            zSpace::zPointArray pts = {query, query}; // a point as a zero-length edge
            zSpace::zIntArray edges = {0, 1};
            zSpace::zFnGraph(queryGraph).create(pts, edges);
        }
        std::vector<Vec3> hits;
        for (size_t k = 0; k < uvw.size() && k < sections.size(); ++k) {
            zSpace::zObjMesh& section = const_cast<zSpace::zObjMesh&>(sections[k]);
            zSpace::zFnMesh fnSection(section);
            if (fnSection.numVertices() == 0 || static_cast<int>(uvw[k].size()) != fnSection.numVertices()) continue;
            zSpace::zPointArray layerUV = uvw[k];
            for (zSpace::zPoint& p : layerUV) p.z = 0.0;
            zSpace::ParameterGraph params;
            zSpace::zPointArray located, positions, hit;
            if (!zSpace::zTsUtils::parameterizeGraph(queryGraph, section, params, &layerUV)
                || !zSpace::zTsUtils::interpolateParameterGraph(params, layerUV, located) || located.empty()
                || std::sqrt(zSpace::zTsUtils::vecDistSq(located[0], query)) > tolerance) continue;
            fnSection.getVertexPositions(positions);
            if (!zSpace::zTsUtils::interpolateParameterGraph(params, positions, hit) || hit.empty()) continue;
            hits.emplace_back(static_cast<float>(hit[0].x), static_cast<float>(hit[0].y), static_cast<float>(hit[0].z));
        }

        for (size_t k = 0; k < hits.size(); ++k) {
            renderer.drawPoint(hits[k], markerColor, 10.0f);
            if (k > 0) renderer.drawLine(hits[k - 1], hits[k], markerColor, 2.0f);
        }
    }

    // Extract paths, run PrintSyn, then cache the processed 3D paths for display.
    void computeProcessedPrintPaths(zSpace::zSlicer& slicer)
    {
        const alice2::slicing::SlicerMethodTimer methodTimer("computeProcessedPrintPaths");
        m_fieldPathSegments.clear();
        m_processedMarkers.clear();
        m_fieldPreview.clear();
        // Retain partial outputs for inspection if a PrintSyn stage fails.
        if (!alice2::slicing::timeSlicerMethod("computePostProcess", [&] { return slicer.computePostProcess(m_printSettings); }))
            logFail("PrintSyn", slicer.getStatus());

        const auto& layers = slicer.getPrintPaths3D();
        m_rawContours2D.assign(layers.size(), {}); m_rawContours3D.assign(layers.size(), {});
        m_rawContourReady.assign(layers.size(), 0);
        m_rawInspector3D.assign(layers.size(), {}); m_rawInspector2D.assign(layers.size(), {});
        m_processedInspector3D.assign(layers.size(), {}); m_processedInspector2D.assign(layers.size(), {});
        std::string meshError;
        if (!alice2::slicing::timeSlicerMethod("buildPrintMesh", [&] { return zSpace::zTsUtils::buildPrintMesh(layers, m_printMesh, &meshError); }))
            logFail("Print mesh", meshError);
        const alice2::slicing::SlicerMethodTimer cacheTimer("cacheProcessedPathsAndMarkers");
        m_fieldPathSegments.assign(layers.size(), std::vector<PathSegment>());
        m_processedMarkers.resize(layers.size());
        const auto& paths2D = slicer.getPrintPaths2D();
        for (size_t k = 0; k < layers.size(); ++k) {
            if (k < paths2D.size()) for (const auto& path : paths2D[k])
                m_processedInspector2D[k].push_back(alice2::slicing::inspectorPathSnapshot(path, m_printSettings.FeatureAngleTol));
            for (const zSpace::PrintPath& path : layers[k]) {
                m_processedInspector3D[k].push_back(alice2::slicing::inspectorPathSnapshot(path, m_printSettings.FeatureAngleTol));
                const auto markers = alice2::slicing::printPathMarkers(path, m_printSettings.FeatureAngleTol);
                m_processedMarkers[k].Targets.insert(m_processedMarkers[k].Targets.end(), markers.Targets.begin(), markers.Targets.end());
                m_processedMarkers[k].Features.insert(m_processedMarkers[k].Features.end(), markers.Features.begin(), markers.Features.end());
                zSpace::zFnGraph fn(const_cast<zSpace::zObjGraph&>(path.graph));
                zSpace::zPointArray positions;
                zSpace::zIntArray edges;
                fn.getVertexPositions(positions);
                fn.getEdgeData(edges);
                // Per-vertex inner(0)/outer(1) tag; empty for a single perimeter.
                const auto trackAt = [&](int v) {
                    return (v >= 0 && v < static_cast<int>(path.trackIds.size())) ? path.trackIds[v] : 0.0f;
                };
                for (size_t e = 0; e + 1 < edges.size(); e += 2) {
                    const zSpace::zPoint& pa = positions[edges[e]];
                    const zSpace::zPoint& pb = positions[edges[e + 1]];
                    PathSegment seg;
                    seg.a = Vec3(static_cast<float>(pa.x), static_cast<float>(pa.y), static_cast<float>(pa.z));
                    seg.b = Vec3(static_cast<float>(pb.x), static_cast<float>(pb.y), static_cast<float>(pb.z));
                    seg.faceTag=e/2<path.edgeFaceTags.size()?path.edgeFaceTags[e/2]:0;
                    seg.outer = 0.5f * (trackAt(edges[e]) + trackAt(edges[e + 1])) >= 0.5f;
                    m_fieldPathSegments[k].push_back(seg);
                }
            }
        }
    }

    // Read named geometry and settings from the USD stage.
    bool isUSDInput() const
    {
        const auto dot = m_inputMeshPath.find_last_of('.');
        std::string ext = dot == std::string::npos ? "" : m_inputMeshPath.substr(dot);
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return ext == ".usd" || ext == ".usda" || ext == ".usdc";
    }

#if defined(ZSPACE_TS3DP_OPENUSD)
    void loadUSDInputs()
    {
        const alice2::slicing::SlicerMethodTimer timer("loadUSDInputs");
        zSpace::zTsIO::SlicerUSDInput input;
        std::string error;
        logInput("File: " + m_inputMeshPath);
        if (!alice2::slicing::timeSlicerMethod("readSlicerInputUSD", [&] { return zSpace::zTsIO::readSlicerInputUSD(m_inputMeshPath, input, &error); })) {
            logFail("Input", error);
            return;
        }
        try {
            const auto settings = nlohmann::json::parse(input.SettingsJSON);
            m_settings = defaultSlicerSettings();
            m_printSettings = defaultPrintSynSettings();
            std::vector<std::string> unknown;
            applySlicerJson(settings.at("slicer"), m_settings, unknown);
            applyPrintSynJson(settings.at("printSyn"), m_printSettings, unknown);
#if defined(ALICE2_SLICER_DISABLE_SURFACE_HEIGHT)
            m_settings.HPrintHeight.OptimizeSurfaces = false;
            logInput("Height mode: global layer scheduling; local surface correction disabled.");
#endif
            if(!input.PrintHeightDomain.empty()){
                m_settings.HPrintHeight.Enabled=true;
                m_settings.HPrintHeight.Minimum=input.PrintHeightDomain[0];
                m_settings.HPrintHeight.Maximum=input.PrintHeightDomain[1];
                logInput("Harmonic target printheightdomain = ["+std::to_string(input.PrintHeightDomain[0])+", "+std::to_string(input.PrintHeightDomain[1])+"] mm, tolerance +/-"+std::to_string(m_settings.HPrintHeight.Tolerance)+" mm");
            }
            for (const auto& key : unknown) logFail("Settings", "ignoring unknown setting '" + key + "'");
            m_settings.BracingCurveWidths = input.BracingPrintWidths;
            if (!input.BracingPrintWidths.empty()) m_settings.BracingWidth = input.BracingPrintWidths.front();
            m_faceTags = input.FaceTags;
            m_faceWidthIds = input.FaceWidthIds;
            if (m_settings.BoundaryWidth.empty()) throw std::runtime_error("Block has no side faces.");
            zSpace::zPointArray points;
            zSpace::zIntArray counts, connects;
            zSpace::zFnMesh source(input.Block);
            source.getVertexPositions(points);
            source.getPolygonData(connects, counts);
            zSpace::zFnMesh(m_inputMesh).create(points, counts, connects);
            points.clear(); connects.clear();
            for (auto& curve : input.BracingCurves) {
                zSpace::zPointArray localPoints;
                zSpace::zIntArray localEdges;
                zSpace::zFnGraph graph(curve);
                graph.getVertexPositions(localPoints); graph.getEdgeData(localEdges);
                const int offset = static_cast<int>(points.size());
                points.insert(points.end(), localPoints.begin(), localPoints.end());
                for (int id : localEdges) connects.push_back(offset + id);
            }
            zSpace::zFnGraph(m_bracingGraph).create(points, connects);
            m_haveBracing = !connects.empty();
            m_slotAnchor = input.StartPoint;
            m_haveSlotAnchor = true;
            m_layerSpacing = m_lastLayerSpacing = m_settings.LayerSpacing;
            std::string groups;
            buildInputGroups(groups);
            logInput("block Mesh: vertices=" + std::to_string(source.numVertices()) + " faces=" + std::to_string(source.numPolygons()) + " | face groups:" + groups);
            logInput("bracing Curves: ribs=" + std::to_string(input.BracingCurves.size()) + " vertices=" + std::to_string(points.size()) + " edges=" + std::to_string(connects.size()/2));
            logInput("slotpoint Points: points=1");
            logOk("Settings", "Loaded /Slicer_setting; geometry and dimensional settings normalized to mm (source metersPerUnit=" + std::to_string(input.MetersPerUnit) + ").");
            m_inputLoaded = true;
        } catch (const std::exception& e) {
            logFail("Input", e.what());
            m_inputLoaded = false;
        }
    }

#endif

    void loadInputs()
    {
        const alice2::slicing::SlicerMethodTimer timer("loadInputs");
        m_slicer.reset();
        m_topoSlicer.reset();
        clearResults();
        m_slicerChoice = m_lastSlicerChoice = kSlicerNone;
        m_displayMode = 0;
        m_inputLoaded = false;
        m_haveBracing = false;
        m_haveSlotAnchor = false;
        m_slotAnchor = zSpace::zPoint(0, 0, 0);
        zSpace::zFnGraph(m_bracingGraph).clear();
        zSpace::zFnMesh(m_inputMesh).clear();

#if defined(ZSPACE_TS3DP_OPENUSD)
        if (!isUSDInput()) {
            logFail("Input", "Only USD input is supported.");
            return;
        }
        loadUSDInputs();
#else
        logFail("Input", "USD input requires OpenUSD support in this build.");
#endif
    }
    // Results cached for drawing, owned by the sketch rather than a slicer.
    void clearResults()
    {
        const alice2::slicing::SlicerMethodTimer timer("clearResults");
        zSpace::zFnMesh(m_printMesh).clear();
        m_fieldPathSegments.clear();
        m_processedMarkers.clear();
        m_fieldPreview.clear();
        m_processedInspector3D.clear(); m_processedInspector2D.clear();
        m_rawInspector3D.clear(); m_rawInspector2D.clear();
        m_rawContours2D.clear(); m_rawContours3D.clear(); m_rawContourReady.clear();
        zSpace::zFnGraph(m_tetWireGraph).clear();
    }

    // Creates ONLY the picked slicer (destroying the other one) and runs it
    // on the loaded inputs.
    void runSelectedSlicer()
    {
        const alice2::slicing::SlicerTimingSession timing;
        const auto start = std::chrono::steady_clock::now();
        clearResults();
        if (m_slicerChoice == kSlicerTopology) {
            m_slicer.reset();
            if (!m_topoSlicer) m_topoSlicer = std::make_unique<zSpace::TopologySlicer>();
            runTopologyPipeline();
        } else if (m_slicerChoice == kSlicerHarmonic) {
            m_topoSlicer.reset();
            if (!m_slicer) m_slicer = std::make_unique<zSpace::HarmonicSlicer>();
            runHarmonicPipeline();
        }
        const auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start);
        std::cout << "[" << (m_slicerChoice == kSlicerTopology ? "TopologySlicer" : "HarmonicSlicer")
                  << "] computation time: " << elapsed.count() << " ms" << std::endl;
    }

    void runHarmonicPipeline()
    {
        const alice2::slicing::SlicerMethodTimer timer("runHarmonicPipeline");
        // The loaded settings (shared + H* entries are read), with
        // LayerSpacing from the "Layer Spacing" slider and one BoundaryWidth
        // per width group.
        alice2::slicing::timeSlicerMethod("HarmonicSlicer.configureInputs", [&] {
            zSpace::SlicerSettings settings = m_settings;
            settings.LayerSpacing = m_layerSpacing;
            fitBoundaryWidths(settings, "HarmonicSlicer");
            m_slicer->setSettings(settings);
            m_slicer->setInputMesh(m_inputMesh, m_faceTags, m_faceWidthIds);
            if (m_haveBracing) m_slicer->setBracingGraph(m_bracingGraph);
            else m_slicer->clearBracingGraph();
            if (m_haveSlotAnchor) m_slicer->setSlotAnchor(m_slotAnchor);
            else m_slicer->clearSlotAnchor();
        });

        // Every step runs only if the previous one succeeded - a failed step
        // stops the run (nothing after it has an input).
        const auto stopped = [&](bool ok, const char* step) {
            if (!ok) logFail("HarmonicSlicer", std::string("run stopped at ") + step + ".");
            return !ok;
        };
        if (stopped(alice2::slicing::timeSlicerMethod("HarmonicSlicer.computeBasicStructure", [&] { return m_slicer->computeBasicStructure(); }), "computeBasicStructure")) return;
        rebuildTetWireGraph();
        if (stopped(alice2::slicing::timeSlicerMethod("HarmonicSlicer.computeHarmonicField", [&] { return m_slicer->computeHarmonicField(); }), "computeHarmonicField")) return;
        if (stopped(alice2::slicing::timeSlicerMethod("HarmonicSlicer.computeVolumetricParameterization", [&] { return m_slicer->computeVolumetricParameterization(); }), "computeVolumetricParameterization"))
            return; // step 4: tracing (u,v) values
        if (stopped(alice2::slicing::timeSlicerMethod("HarmonicSlicer.computeContours", [&] { return m_slicer->computeContours(); }), "computeContours")) return;
        colorHarmonicIsoByUV();
        if (stopped(alice2::slicing::timeSlicerMethod("HarmonicSlicer.computeField", [&] { return m_slicer->computeField(); }), "computeField")) return;
        computeProcessedPrintPaths(*m_slicer); // Process once and cache per-layer display paths.
    }

    // Topology slicer + its post-processing, with the loaded settings
    // (shared entries are read). BoundaryWidth is padded to the width groups
    // of the side faces (fitBoundaryWidths()), as compasviewer.py does.
    void runTopologyPipeline()
    {
        const alice2::slicing::SlicerMethodTimer timer("runTopologyPipeline");
        logOk("TopologySlicer", "run started");
        zSpace::SlicerSettings settings = m_settings;
        settings.LayerSpacing = m_layerSpacing; // "Layer Spacing" slider
        fitBoundaryWidths(settings, "TopologySlicer");
        alice2::slicing::timeSlicerMethod("TopologySlicer.configureInputs", [&] {
            m_topoSlicer->setSettings(settings);
            m_topoSlicer->setInputMesh(m_inputMesh, m_faceTags, m_faceWidthIds);
            m_topoSlicer->setBracingGraph(m_bracingGraph); // empty graph = no bracing
            if (m_haveSlotAnchor) m_topoSlicer->setSlotAnchor(m_slotAnchor);
            else m_topoSlicer->clearSlotAnchor();
        });


        // The settings are absolute lengths. On a model in other units than
        // the settings assume they explode into millions of layers / field
        // cells and the run never finishes - refuse instead of freezing the app.
        {
            zSpace::zPoint minBB, maxBB;
            zSpace::zFnMesh(m_inputMesh).getBounds(minBB, maxBB);
            const double layers = (maxBB.z - minBB.z) / settings.LayerSpacing;
            const double cells = ((maxBB.x - minBB.x) * settings.FieldScaleUp / settings.FieldCellSize)
                * ((maxBB.y - minBB.y) * settings.FieldScaleUp / settings.FieldCellSize);
            constexpr double kMaxLayers = 2000.0;
            constexpr double kMaxCells = 4.0e6;
            if (layers > kMaxLayers || cells > kMaxCells) {
                std::ostringstream out;
                out << "run skipped: ~" << static_cast<long long>(layers) << " layers / ~"
                    << static_cast<long long>(cells) << " field cells per layer (limits " << kMaxLayers << " / "
                    << kMaxCells << ") - check LayerSpacing / FieldCellSize against the model's size.";
                logFail("TopologySlicer", out.str());
                return;
            }
        }

        // Failing here (e.g. a triangle mesh or no Top/Bottom tags) stops the
        // run - computeSlicer() would only repeat the same failure.
        if (!alice2::slicing::timeSlicerMethod("TopologySlicer.computeBasicStructure", [&] { return m_topoSlicer->computeBasicStructure(); })) {
            logFail("TopologySlicer", "run stopped at computeBasicStructure.");
            return;
        }
        if (!alice2::slicing::timeSlicerMethod("TopologySlicer.computeSlicer", [&] { return m_topoSlicer->computeSlicer(); })) {
            logFail("TopologySlicer", "run stopped at computeSlicer.");
            return;
        }
        computeProcessedPrintPaths(*m_topoSlicer);
    }

    // One BoundaryWidth entry per width group the side faces use (side_<n>):
    // keeps the given ones, pads with the last.
    void fitBoundaryWidths(zSpace::SlicerSettings& settings, const std::string& source)
    {
        const int groups = std::max(1, zSpace::zTsUtils::widthGroupCount(m_faceTags, m_faceWidthIds));
        std::vector<float>& widths = settings.BoundaryWidth;
        if (int(widths.size()) < groups) {
            const float fill = widths.empty() ? 0.06f : widths.back();
            logFail(source, "BoundaryWidth has " + std::to_string(widths.size()) + " entries for "
                + std::to_string(groups) + " width groups - padded with " + std::to_string(fill) + ".");
            widths.resize(groups, fill);
        }
    }

    std::string m_inputMeshPath;
    zSpace::zObjMesh m_inputMesh;
    zSpace::zObjMesh m_printMesh;
    // Bracing from the USD stage, in world coordinates.
    zSpace::zObjGraph m_bracingGraph;
    // First vertex of object 02; missing object defaults to the world origin.
    zSpace::zPoint m_slotAnchor;
    bool m_haveSlotAnchor = false;
    // Settings from /Slicer_setting, normalized to millimetres, shared by both slicers.
    zSpace::SlicerSettings m_settings = defaultSlicerSettings();
    alice2::slicing::SlicerViewValidation m_viewValidation;
    int m_validationCaptureFrame = -1;
    zSpace::PrintSynSetting m_printSettings = defaultPrintSynSettings();
    zSpace::zObjGraph m_tetWireGraph;
    // Only the picked slicer exists (created by runSelectedSlicer()); both
    // null until a slicer button is pressed after loading.
    std::unique_ptr<zSpace::HarmonicSlicer> m_slicer;
    std::unique_ptr<zSpace::TopologySlicer> m_topoSlicer;
    zSpace::zIntArray m_faceTags;     // from USD FaceTag attributes, shared by both slicers
    zSpace::zIntArray m_faceWidthIds; // width group per face (side_<n> -> BoundaryWidth[n])
    bool m_haveBracing = false;
    bool m_inputLoaded = false; // mesh read by loadInputs()
    // Input mesh split per face group (bottom, top, side_<n>) and each group's
    // flat color - see buildInputGroups(); m_widthPalette is the width palette
    // shared with the width field minimap.
    std::vector<zSpace::zObjMesh> m_inputGroupMeshes;
    std::vector<Color> m_inputGroupColors;
    zSpace::zScalarArray m_widthPalette;
    int m_slicerChoice = kSlicerNone;     // "Harmonic | Topology" button group (-1 = none lit)
    int m_lastSlicerChoice = kSlicerNone; // last value seen by update()
    float m_layerSpacing = 8.0f;     // "Layer Spacing" slider, shared by both slicers
    float m_lastLayerSpacing = 8.0f; // last value seen by update()
    int m_displayMode = 0; // index into the active slicer's display modes - see displayModeName()
    int m_inspectLayer = 0; // layer shown by the layer inspector mode (up/down arrow)

    std::unique_ptr<SimpleUI> m_ui;
    int m_exportAction = -1;
    float m_probeU = 0.5f; // slider-bound, drawUVProbe() query point (mode 2)
    float m_probeV = 0.5f; // slider-bound, drawUVProbe() query point (mode 2)
    // computeProcessedPrintPaths()'s cached result, replayed every frame by
    // drawFieldPrintPaths() - [layer][segment], already resolved to
    // world-space endpoints, with the segment's track (outer = pink).
    alice2::slicing::SlicerFieldPreview m_fieldPreview;
    std::vector<InspectorPaths> m_processedInspector3D, m_processedInspector2D;
    std::vector<InspectorPaths> m_rawInspector3D, m_rawInspector2D;
    struct PathSegment {
        int faceTag=0;
        Vec3 a;
        Vec3 b;
        bool outer = false;
    };
    std::vector<std::vector<PathSegment>> m_fieldPathSegments;
    std::vector<alice2::slicing::PrintPathMarkers> m_processedMarkers;
    zSpace::PrintPathArray m_rawContours2D, m_rawContours3D;
    std::vector<char> m_rawContourReady;
};

ALICE2_REGISTER_SKETCH_AUTO(HarmonicSlicerTestSketch)

#endif // __MAIN__

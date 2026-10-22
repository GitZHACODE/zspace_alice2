#define __MAIN__
#include "slicer.h"
#ifdef __MAIN__

#include <zspace/interface.h>
#include <zspace/slicing.h>
#include <zspace/zSlicingSettings.h>

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
using namespace alice2::slicer;

namespace {

using alice2::slicing::defaultPrintSynSettings;
using alice2::slicing::defaultSlicerSettings;

// Native Windows "Open File" dialog, same pattern as userSrc/zspace/sketch.cpp.
// Returns true and fills outPath if the user picked an existing file; false
// (outPath untouched) if they cancelled.
inline bool openFileDialog(const char *filter, const char *title,
                           std::string &outPath) {
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

class SlicerSketch : public ISketch {
public:
  std::string getName() const override {
    return "Slicer End-to-End Test (Harmonic | Topology)";
  }
  std::string getDescription() const override {
    return "Loads a tagged USD mesh and runs the HarmonicSlicer or "
           "TopologySlicer pipeline, then extracts paths and maps them "
           "directly to 3D.";
  }
  std::string getAuthor() const override { return "alice2 + zspace_toolsets"; }

  void setup() override {
    scene().setBackgroundColor(Color(1.0f, 1.0f, 1.0f, 1.0f));
    scene().setShowGrid(false);
    scene().setShowAxes(false);

    setupUI();
  }

  void update(float /*deltaTime*/) override {
    if (state.importAction == 0) {
      state.importAction = -1;
      importUSD();
    }
    if (state.slicerChoice != state.lastSlicerChoice) {
      state.lastSlicerChoice = state.slicerChoice;
      state.displayMode = 0;
      if (state.inputLoaded && state.slicerChoice != kSlicerNone)
        runSelectedSlicer();
    }
    if (state.exportAction == 0) {
      state.exportAction = -1;
      exportPrintPath();
    }
  }

  bool onKeyPress(unsigned char key, int, int) override {
    if (key == 'k') {
      state.displayMode =
          (state.displayMode + 1) % displayModeCount(state.slicerChoice);
      return true;
    }

    if (key == 'K') {
      const int count = displayModeCount(state.slicerChoice);
      state.displayMode = (state.displayMode - 1 + count) % count;
      return true;
    }

    // Layer inspector: up / down arrow (alice2 key codes 201 / 203).
    if ((key == 201 || key == 203) && isLayerInspectorMode(state, scene())) {
      const int count = inspectorLayerCount(state, scene());
      if (count > 0)
        state.inspectLayer =
            (std::max)(0, (std::min)(count - 1, state.inspectLayer +
                                                    (key == 201 ? 1 : -1)));
      return true;
    }

    return false;
  }

  bool onMousePress(int button, int buttonState, int x, int y) override {
    return state.ui && state.ui->onMousePress(button, buttonState, x, y);
  }

  bool onMouseMove(int x, int y) override {
    return state.ui && state.ui->onMouseMove(x, y);
  }

  void draw(Renderer &renderer, Camera &) override {
    if (state.slicerChoice == kSlicerNone ||
        (state.slicerChoice == kSlicerTopology && !state.topoSlicer) ||
        (state.slicerChoice == kSlicerHarmonic && !state.slicer)) {
      drawInputPreview(state, scene());
    } else if (state.slicerChoice == kSlicerTopology) {
      switch (state.displayMode) {
      case 0:
        drawInputPreview(state, scene());
        break;
      case 1:
        drawInputColored(state, scene());
        drawBracingGraph(state, scene());
        break;
      case 2:
        drawInputColored(state, scene());
        drawVLoops(state, scene());
        break;
      case 3:
        drawGhostInputMesh(state, scene());
        drawSectionMeshes(state, scene());
        break;
      case 4:
        drawGhostInputMesh(state, scene());
        drawFieldPrintPaths(state, scene(), renderer);
        drawTopologyFieldMinimap(state, scene(), renderer);
        break;
      case 5:
        drawGhostInputMesh(state, scene());
        drawLayerInspector(state, scene(), renderer);
        break;
      default:
        break;
      }
    } else {
      switch (state.displayMode) {
      case 0:
        drawInputPreview(state, scene());
        break;
      case 1:
        drawInputColored(state, scene());
        drawBracingGraph(state, scene());
        break;
      case 2:
        drawTetWireframe(state, scene());
        break;
      case 3:
        drawGhostInputMesh(state, scene());
        drawHarmonicIsoUV(state, scene());
        drawUVGradientLegend(state, scene(), renderer);
        break;
      case 4:
        drawGhostInputMesh(state, scene());
        drawFailedTraces(state, scene(), renderer);
        drawTopSuccessTraces(state, scene(), renderer);
        break;
      case 5:
        drawGhostInputMesh(state, scene());
        drawFieldPrintPaths(state, scene(), renderer);
        drawHarmonicFieldMinimap(state, scene(), renderer);
        break;
      case 6:
        drawGhostInputMesh(state, scene());
        drawLayerInspector(state, scene(), renderer);
        break;
      default:
        break;
      }
    }

    drawSlotAnchor(state, scene(), renderer);

    renderer.setColor(Color(0.08f, 0.09f, 0.12f, 1.0f));
    renderer.drawString(getName(), 10, 28);
    std::ostringstream modeLine;
    modeLine << (state.slicerChoice == kSlicerNone       ? "[No slicer] "
                 : state.slicerChoice == kSlicerTopology ? "[Topology] "
                                                         : "[Harmonic] ")
             << "'k'/'K' cycle display forward/back ("
             << (state.displayMode + 1) << "/"
             << displayModeCount(state.slicerChoice) << ": "
             << displayModeName(state.slicerChoice, state.displayMode) << ")";
    renderer.drawString(modeLine.str(), 10, 50);
    if (!state.inputLoaded) {
      renderer.drawString("Click Import USD to load AG_block, Slicer_bracing, "
                          "Slicer_slot and Slicer_setting.",
                          10, 68);
    } else if (state.slicerChoice == kSlicerNone) {
      renderer.drawString(
          "Inputs loaded - pick Harmonic or Topology below to run that slicer.",
          10, 68);
    }

    // Draw controls above the display overlays.
    if (state.ui)
      state.ui->draw(renderer);
  }

private:
  void setupUI() {
    state.ui = std::make_unique<SimpleUI>(input());
    state.ui->setTheme(SimpleUI::UITheme::Dark);
    state.ui->addButtonGroup({"Import USD"}, Vec2{10.0f, 440.0f}, 220.0f, 26.0f,
                             0.0f, state.importAction);
    state.ui->addButtonGroup({"Harmonic", "Topology"}, Vec2{10.0f, 480.0f},
                             106.0f, 26.0f, 8.0f, state.slicerChoice);
    state.ui->addButtonGroup({"Export USD"}, Vec2{10.0f, 520.0f}, 220.0f, 26.0f,
                             0.0f, state.exportAction);
  }

  void importUSD() {
    std::string picked;
    const char *filter =
        "USD inputs (*.usd;*.usda;*.usdc)\0*.usd;*.usda;*.usdc\0";
    if (!openFileDialog(filter, "Select slicer input", picked))
      return;
    state.inputMeshPath = std::move(picked);
    loadInputs();
    if (state.inputLoaded)
      focusCameraOnInputMesh(state, scene());
  }

  void exportPrintPath() {
    auto *slicer = activeSlicer(state, scene());
    if (!slicer) {
      logFail("Export USD", "Run a slicer and PrintSyn before exporting.");
      return;
    }
    const auto &path = slicer->getResultantPrintPath();
    if (zSpace::zFnGraph(const_cast<zSpace::zObjGraph &>(path.graph))
            .numVertices() < 2) {
      logFail("Export USD",
              "No combined print path is available; complete PrintSyn first.");
      return;
    }
    if (!isUSDInput()) {
      logFail(
          "Export USD",
          "Load a USD input to export input geometry and printpath together.");
      return;
    }
    char fileBuffer[MAX_PATH] = {};
    if (state.inputMeshPath.size() >= MAX_PATH) {
      logFail("Export USD", "Input path is too long for the save dialog.");
      return;
    }
    std::copy(state.inputMeshPath.begin(), state.inputMeshPath.end(),
              fileBuffer);
    OPENFILENAMEA dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = GetActiveWindow();
    dialog.lpstrFilter =
        "USD files (*.usda;*.usdc;*.usd)\0*.usda;*.usdc;*.usd\0";
    dialog.lpstrFile = fileBuffer;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrTitle = "Save input USD with printpath";
    dialog.lpstrDefExt = "usda";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameA(&dialog)) {
      if (CommDlgExtendedError() != 0)
        logFail("Export USD", "Could not open the save dialog.");
      return;
    }
    std::string error;
    if (!zSpace::aliceSlicingUSD::exportUSD(
            path, state.inputMeshPath, fileBuffer,
            static_cast<int>(slicer->getPrintPaths3D().size()), &error))
      logFail("Export USD", error);
    else
      logOk("Export USD", std::string("Saved ") + fileBuffer);
  }

  // Console status in the same style as the ts3dp library's own prints:
  // green = success, red = failure, "[source]" names who is reporting
  // (HarmonicSlicer / TopologySlicer / Input). Each slicer step prints
  // its own "[zXxxSlicer::step]" line, so the sketch doesn't repeat them.
  static void logOk(const std::string &source, const std::string &message) {
    zSpace::zTsUtils::printSuccess("[" + source + "] " + message);
  }
  static void logInput(const std::string &message) {
    std::cout << "\033[37m[Input] " << message << "\033[0m\n" << std::flush;
  }
  static void logFail(const std::string &source, const std::string &message) {
    zSpace::zTsUtils::printError("[" + source + "] " + message);
  }

  // Extract paths, run PrintSyn, then cache the processed 3D paths for display.
  void computeProcessedPrintPaths(zSpace::zSlicer &slicer) {
    state.fieldPathSegments.clear();
    // computePostProcess performs extraction itself; do not extract twice.
    // Retain partial outputs for inspection if a PrintSyn stage fails.
    if (!slicer.computePostProcess(state.printSettings))
      logFail("PrintSyn", slicer.getStatus());

    const auto &layers = slicer.getPrintPaths3D();
    state.fieldPathSegments.assign(layers.size(),
                                   std::vector<SlicerState::PathSegment>());
    for (size_t k = 0; k < layers.size(); ++k) {
      for (const zSpace::PrintPath &path : layers[k]) {
        zSpace::zFnGraph fn(const_cast<zSpace::zObjGraph &>(path.graph));
        zSpace::zPointArray positions;
        zSpace::zIntArray edges;
        fn.getVertexPositions(positions);
        fn.getEdgeData(edges);
        // Per-vertex inner(0)/outer(1) tag; empty for a single perimeter.
        const auto trackAt = [&](int v) {
          return (v >= 0 && v < static_cast<int>(path.trackIds.size()))
                     ? path.trackIds[v]
                     : 0.0f;
        };
        for (size_t e = 0; e + 1 < edges.size(); e += 2) {
          const zSpace::zPoint &pa = positions[edges[e]];
          const zSpace::zPoint &pb = positions[edges[e + 1]];
          SlicerState::PathSegment seg;
          seg.a = Vec3(static_cast<float>(pa.x), static_cast<float>(pa.y),
                       static_cast<float>(pa.z));
          seg.b = Vec3(static_cast<float>(pb.x), static_cast<float>(pb.y),
                       static_cast<float>(pb.z));
          seg.outer =
              0.5f * (trackAt(edges[e]) + trackAt(edges[e + 1])) >= 0.5f;
          state.fieldPathSegments[k].push_back(seg);
        }
      }
    }
  }

  // Read named geometry and settings from the USD stage.
  bool isUSDInput() const {
    const auto dot = state.inputMeshPath.find_last_of('.');
    std::string ext =
        dot == std::string::npos ? "" : state.inputMeshPath.substr(dot);
    for (char &c : ext)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".usd" || ext == ".usda" || ext == ".usdc";
  }

#if defined(ALICE2_SLICER_USD)
  void loadUSDInputs() {
    zSpace::aliceSlicingUSD::SlicerUSDInput input;
    std::string error;
    logInput("File: " + state.inputMeshPath);
    if (!zSpace::aliceSlicingUSD::importUSD(state.inputMeshPath, input,
                                                     &error)) {
      logFail("Input", error);
      return;
    }
    try {
      state.settings = input.Settings;
      state.printSettings = input.PrintSettings;
      for (const auto &key : input.UnknownSettings)
        logFail("Settings", "ignoring unknown setting '" + key + "'");
      state.settings.BracingCurveWidths = input.BracingPrintWidths;
      if (!input.BracingPrintWidths.empty())
        state.settings.BracingWidth = input.BracingPrintWidths.front();
      state.faceTags = std::move(input.FaceTags);
      state.faceWidthIds = std::move(input.FaceWidthIds);
      if (state.settings.BoundaryWidth.empty())
        throw std::runtime_error("Block has no side faces.");
      state.inputMesh = std::move(input.Block);
      state.bracingGraph = std::move(input.Bracing);
      zSpace::zFnMesh source(state.inputMesh);
      zSpace::zFnGraph bracing(state.bracingGraph);
      state.haveBracing = bracing.numEdges() > 0;
      state.slotAnchor = input.StartPoint;
      state.haveSlotAnchor = true;
      std::string groups;
      buildInputGroups(state, scene(), groups);
      logInput("block Mesh: vertices=" + std::to_string(source.numVertices()) +
               " faces=" + std::to_string(source.numPolygons()) +
               " | face groups:" + groups);
      logInput(
          "bracing Curves: ribs=" + std::to_string(input.BracingCurveCount) +
          " vertices=" + std::to_string(bracing.numVertices()) +
          " edges=" + std::to_string(bracing.numEdges()));
      logInput("slotpoint Points: points=1");
      logOk("Settings", "Loaded /Slicer_setting; geometry and dimensional "
                        "settings normalized to mm (source metersPerUnit=" +
                            std::to_string(input.MetersPerUnit) + ").");
      state.inputLoaded = true;
    } catch (const std::exception &e) {
      logFail("Input", e.what());
      state.inputLoaded = false;
    }
  }

#endif

  void loadInputs() {
    state.slicer.reset();
    state.topoSlicer.reset();
    clearResults();
    state.slicerChoice = state.lastSlicerChoice = kSlicerNone;
    state.displayMode = 0;
    state.inputLoaded = false;
    state.haveBracing = false;
    state.haveSlotAnchor = false;
    state.slotAnchor = zSpace::zPoint(0, 0, 0);
    zSpace::zFnGraph(state.bracingGraph).clear();
    zSpace::zFnMesh(state.inputMesh).clear();

#if defined(ALICE2_SLICER_USD)
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
  void clearResults() {
    state.fieldPathSegments.clear();
    zSpace::zFnGraph(state.tetWireGraph).clear();
  }

  // Creates ONLY the picked slicer (destroying the other one) and runs it
  // on the loaded inputs.
  void runSelectedSlicer() {
    clearResults();
    const auto start = std::chrono::steady_clock::now();
    if (state.slicerChoice == kSlicerTopology) {
      state.slicer.reset();
      if (!state.topoSlicer)
        state.topoSlicer = std::make_unique<zSpace::TopologySlicer>();
      runTopologyPipeline();
    } else if (state.slicerChoice == kSlicerHarmonic) {
      state.topoSlicer.reset();
      if (!state.slicer)
        state.slicer = std::make_unique<zSpace::HarmonicSlicer>();
      runHarmonicPipeline();
    }
    const auto elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start);
    std::cout << "["
              << (state.slicerChoice == kSlicerTopology ? "TopologySlicer"
                                                        : "HarmonicSlicer")
              << "] computation time: " << elapsed.count() << " ms"
              << std::endl;
  }

  void runHarmonicPipeline() {
    zSpace::SlicerSettings settings = state.settings;
    fitBoundaryWidths(settings, "HarmonicSlicer");
    state.slicer->setSettings(settings);
    state.slicer->setInputMesh(state.inputMesh, state.faceTags,
                               state.faceWidthIds);
    if (state.haveBracing)
      state.slicer->setBracingGraph(state.bracingGraph);
    else
      state.slicer->clearBracingGraph();
    if (state.haveSlotAnchor)
      state.slicer->setSlotAnchor(state.slotAnchor);
    else
      state.slicer->clearSlotAnchor();

    // Every step runs only if the previous one succeeded - a failed step
    // stops the run (nothing after it has an input).
    const auto stopped = [&](bool ok, const char *step) {
      if (!ok)
        logFail("HarmonicSlicer", std::string("run stopped at ") + step + ".");
      return !ok;
    };
    if (stopped(state.slicer->computeBasicStructure(), "computeBasicStructure"))
      return; // step 1: tag and tetrahedralize
    rebuildTetWireGraph(state, scene());
    if (stopped(state.slicer->computeHarmonicField(), "computeHarmonicField"))
      return; // step 3: harmonic (w) values
    if (stopped(state.slicer->computeVolumetricParameterization(),
                "computeVolumetricParameterization"))
      return; // step 4: tracing (u,v) values
    if (stopped(state.slicer->computeContours(), "computeContours"))
      return; // step 5: slice meshes, ARAP charts and boundaries
    colorHarmonicIsoByUV(state, scene());
    if (stopped(state.slicer->computeField(), "computeField"))
      return; // step 7: per-layer path/width fields
    computeProcessedPrintPaths(
        *state.slicer); // Process once and cache per-layer display paths.
  }

  // Topology slicer + its post-processing, with the loaded settings
  // (shared entries are read). BoundaryWidth covers the side-face width groups.
  void runTopologyPipeline() {
    zSpace::SlicerSettings settings = state.settings;
    fitBoundaryWidths(settings, "TopologySlicer");
    state.topoSlicer->setSettings(settings);
    state.topoSlicer->setInputMesh(state.inputMesh, state.faceTags,
                                   state.faceWidthIds);
    state.topoSlicer->setBracingGraph(
        state.bracingGraph); // empty graph = no bracing
    if (state.haveSlotAnchor)
      state.topoSlicer->setSlotAnchor(state.slotAnchor);
    else
      state.topoSlicer->clearSlotAnchor();

    // The settings are absolute lengths. On a model in other units than
    // the settings assume they explode into millions of layers / field
    // cells and the run never finishes - refuse instead of freezing the app.
    {
      zSpace::zPoint minBB, maxBB;
      zSpace::zFnMesh(state.inputMesh).getBounds(minBB, maxBB);
      const double layers = (maxBB.z - minBB.z) / settings.LayerSpacing;
      const double cells = ((maxBB.x - minBB.x) * settings.FieldScaleUp /
                            settings.FieldCellSize) *
                           ((maxBB.y - minBB.y) * settings.FieldScaleUp /
                            settings.FieldCellSize);
      constexpr double kMaxLayers = 2000.0;
      constexpr double kMaxCells = 4.0e6;
      if (layers > kMaxLayers || cells > kMaxCells) {
        std::ostringstream out;
        out << "run skipped: ~" << static_cast<long long>(layers)
            << " layers / ~" << static_cast<long long>(cells)
            << " field cells per layer (limits " << kMaxLayers << " / "
            << kMaxCells
            << ") - check LayerSpacing / FieldCellSize against the model's "
               "size.";
        logFail("TopologySlicer", out.str());
        return;
      }
    }

    // computeSlicer builds the basic structure and stops if any step fails.
    if (!state.topoSlicer->computeSlicer()) {
      logFail("TopologySlicer", "run stopped at computeSlicer.");
      return;
    }
    computeProcessedPrintPaths(*state.topoSlicer);
  }

  // One BoundaryWidth entry per width group the side faces use (side_<n>):
  // keeps the given ones, pads with the last.
  void fitBoundaryWidths(zSpace::SlicerSettings &settings,
                         const std::string &source) {
    const int groups = std::max(1, zSpace::zTsUtils::widthGroupCount(
                                       state.faceTags, state.faceWidthIds));
    std::vector<float> &widths = settings.BoundaryWidth;
    if (int(widths.size()) < groups) {
      const float fill = widths.empty() ? 0.06f : widths.back();
      logFail(source, "BoundaryWidth has " + std::to_string(widths.size()) +
                          " entries for " + std::to_string(groups) +
                          " width groups - padded with " +
                          std::to_string(fill) + ".");
      widths.resize(groups, fill);
    }
  }

  SlicerState state;
};

ALICE2_REGISTER_SKETCH_AUTO(SlicerSketch)

#endif // __MAIN__

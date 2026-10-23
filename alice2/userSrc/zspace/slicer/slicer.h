#pragma once
#include <zspace/zToolsets/fabrication/slicing/utils/IO.h>

// IO and headless tests share this header without pulling in Alice rendering.
#ifndef ALICE2_SLICER_IO_ONLY
#include <alice2.h>
#include <memory>
#include <vector>
#include <zspace/interface.h>
#include <zspace/slicing.h>
#include <zspace/zSlicingSettings.h>
namespace alice2::slicer {
// Slicer picked by the "Harmonic | Topology" button group. kSlicerNone (no
// button lit) = inputs loaded and displayed only, nothing computed.
constexpr int kSlicerNone = -1;
constexpr int kSlicerHarmonic = 0;
constexpr int kSlicerTopology = 1;
constexpr int kHarmonicModeCount = 7;
constexpr int kTopologyModeCount = 6;

inline int displayModeCount(int slicer) {
  if (slicer == kSlicerNone)
    return 1;
  return slicer == kSlicerTopology ? kTopologyModeCount : kHarmonicModeCount;
}

inline const char *displayModeName(int slicer, int mode) {
  static const char *harmonic[kHarmonicModeCount] = {
      "Input preview (transparent block, bracing, slot point)",
      "Input mesh (colored top/bottom)",
      "Tetrahedra volume mesh",
      "Method 2 (u,v) check",
      "Tracing curves (failed red, top success green)",
      "Print paths (inner blue, outer pink) + layer-0 field",
      "Layer inspector (up/down: layer) - 3D paths + 2D paths on the layer's "
      "field"};
  static const char *topology[kTopologyModeCount] = {
      "Input preview (transparent block, bracing, slot point)",
      "Input mesh (colored top/bottom)",
      "V-loops + vertex value t (blue 0 -> red 1)",
      "Section meshes",
      "Print paths (inner blue, outer pink) + layer-0 field",
      "Layer inspector (up/down: layer) - 3D paths + 2D paths on the layer's "
      "field"};
  if (slicer == kSlicerNone)
    return "Input preview (transparent block, bracing, slot point)";
  const int count = displayModeCount(slicer);
  const int m = ((mode % count) + count) % count;
  return slicer == kSlicerTopology ? topology[m] : harmonic[m];
}

// Owned by the main sketch, passed explicitly to view functions.
struct SlicerState {
  std::string inputMeshPath;
  zSpace::zObjMesh inputMesh;
  // Bracing from the USD stage, in world coordinates.
  zSpace::zObjGraph bracingGraph;
  // Required single point read from /Slicer_slot.
  zSpace::zPoint slotAnchor;
  bool haveSlotAnchor = false;
  // Settings from /Slicer_setting, normalized to millimetres, shared by both
  // slicers.
  zSpace::SlicerSettings settings = alice2::slicing::defaultSlicerSettings();
  zSpace::PrintSynSetting printSettings =
      alice2::slicing::defaultPrintSynSettings();
  zSpace::zObjGraph tetWireGraph;
  // Only the picked slicer exists (created by runSelectedSlicer()); both
  // null until a slicer button is pressed after loading.
  std::unique_ptr<zSpace::HarmonicSlicer> slicer;
  std::unique_ptr<zSpace::TopologySlicer> topoSlicer;
  zSpace::zIntArray
      faceTags; // from USD FaceTag attributes, shared by both slicers
  zSpace::zIntArray
      faceWidthIds; // width group per face (side_<n> -> BoundaryWidth[n])
  bool haveBracing = false;
  bool inputLoaded = false; // mesh read by loadInputs()
  // Input mesh split per face group (bottom, top, side_<n>) and each group's
  // flat color - see buildInputGroups(); widthPalette is the width palette
  // shared with the width field minimap.
  std::vector<zSpace::zObjMesh> inputGroupMeshes;
  std::vector<Color> inputGroupColors;
  zSpace::zScalarArray widthPalette;
  int slicerChoice =
      kSlicerNone; // "Harmonic | Topology" button group (-1 = none lit)
  int lastSlicerChoice = kSlicerNone; // last value seen by update()
  int displayMode =
      0; // index into the active slicer's display modes - see displayModeName()
  int inspectLayer =
      0; // layer shown by the layer inspector mode (up/down arrow)

  std::unique_ptr<SimpleUI> ui;
  int importAction = -1;
  int exportAction = -1;
  // computeProcessedPrintPaths()'s cached result, replayed every frame by
  // drawFieldPrintPaths() - [layer][segment], already resolved to
  // world-space endpoints, with the segment's track (outer = pink).
  struct PathSegment {
    Vec3 a;
    Vec3 b;
    bool outer = false;
  };
  std::vector<std::vector<PathSegment>> fieldPathSegments;
};
void focusCameraOnInputMesh(SlicerState &state, Scene &scene);
zDisplayMeshSetting ghostMeshDisplay(SlicerState &state, Scene &scene);
void drawVLoops(SlicerState &state, Scene &scene);
void drawSectionMeshes(SlicerState &state, Scene &scene);
void drawSlotAnchor(SlicerState &state, Scene &scene, Renderer &renderer);
void drawInputPreview(SlicerState &state, Scene &scene);
void drawBracingGraph(SlicerState &state, Scene &scene);
void drawGhostInputMesh(SlicerState &state, Scene &scene);
void buildInputGroups(SlicerState &state, Scene &scene,
                      std::string &outSummary);
void drawInputColored(SlicerState &state, Scene &scene);
void drawFailedTraces(SlicerState &state, Scene &scene, Renderer &renderer);
void drawTopSuccessTraces(SlicerState &state, Scene &scene, Renderer &renderer);
void drawTrace(SlicerState &state, Scene &scene, Renderer &renderer,
               zSpace::zObjGraph &graph, const zSpace::zPoint &start,
               const Color &color);
void drawFieldPrintPaths(SlicerState &state, Scene &scene, Renderer &renderer);
void drawHarmonicFieldMinimap(SlicerState &state, Scene &scene,
                              Renderer &renderer);
void drawTopologyFieldMinimap(SlicerState &state, Scene &scene,
                              Renderer &renderer);
bool isLayerInspectorMode(SlicerState &state, Scene &scene);
zSpace::zSlicer *activeSlicer(SlicerState &state, Scene &scene);
int inspectorLayerCount(SlicerState &state, Scene &scene);
void drawLayerInspector(SlicerState &state, Scene &scene, Renderer &renderer);
void drawFieldMinimap(SlicerState &state, Scene &scene, Renderer &renderer,
                      const zSpace::zObjMeshScalarField &field,
                      const zSpace::zScalarArray &pathValues,
                      const zSpace::zScalarArray &widthValues,
                      const char *pathLabel,
                      const std::vector<zSpace::PrintPath> *paths2D = nullptr);
void drawTetWireframe(SlicerState &state, Scene &scene);
void drawHarmonicIsoUV(SlicerState &state, Scene &scene);
void rebuildTetWireGraph(SlicerState &state, Scene &scene);
void colorHarmonicIsoByUV(SlicerState &state, Scene &scene);
void drawUVGradientLegend(SlicerState &state, Scene &scene, Renderer &renderer);
} // namespace alice2::slicer

#endif // ALICE2_SLICER_IO_ONLY

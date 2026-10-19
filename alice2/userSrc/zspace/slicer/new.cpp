// #define __MAIN__
#ifdef __MAIN__

#ifndef EIGEN_DONT_PARALLELIZE
#define EIGEN_DONT_PARALLELIZE
#endif

#include <zspace/interface.h>
#include <zspace/io.h>

#include <headers/zToolsets/ts3dp/zTs3DP.h>
#include <headers/zToolsets/ts3dp/utils/Utils.h>

// clang-format off
#include <windows.h>
#include <commdlg.h>
// clang-format on

#include <alice2.h>
#include <sketches/SketchRegistry.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#if defined(_OPENMP)
#include <omp.h>
#endif

using namespace alice2;
using json = nlohmann::json;

using zSpace::zTsUtils::printError;
using zSpace::zTsUtils::printSuccess;

namespace {

// Native Windows "Open File" dialog. Returns true and fills outPath if the
// user picked a file; returns false (outPath untouched) if they cancelled.
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

// Writes json[key] into field if present; leaves field untouched otherwise.
template <typename T>
inline void jsonGetOr(const json &obj, const char *key, T &field) {
  if (obj.contains(key))
    field = obj.at(key).get<T>();
}

// Overrides slicerSettings/printSynSetting fields with any keys found in the
// JSON file at path; fields missing from the JSON (or the whole file itself)
// are left untouched, so callers must pre-populate defaults before calling.
inline bool loadSettingsFromJson(const std::string &path,
                                 zSpace::SlicerSettings &slicerSettings,
                                 zSpace::PrintSynSetting &printSynSetting) {
  std::ifstream file(path);
  if (!file) {
    std::cout << "[zSpaceSlicer] No settings JSON at "
              << std::filesystem::absolute(path).string()
              << " - using built-in defaults." << std::endl;
    return false;
  }

  json j;
  try {
    file >> j;
  } catch (const std::exception &e) {
    printError("[zSpaceSlicer] Failed to parse settings JSON: " +
               std::string(e.what()));
    return false;
  }

  if (j.contains("slicer") && j["slicer"].is_object()) {
    const json &s = j["slicer"];
    jsonGetOr(s, "LayerSpacing", slicerSettings.LayerSpacing);
    jsonGetOr(s, "TTopoSplit", slicerSettings.TTopoSplit);
    jsonGetOr(s, "TAngleSplit", slicerSettings.TAngleSplit);
    jsonGetOr(s, "TAngleSplitTol", slicerSettings.TAngleSplitTol);
    jsonGetOr(s, "DoubleBoundary", slicerSettings.DoubleBoundary);
    jsonGetOr(s, "BoundaryWidth", slicerSettings.BoundaryWidth);
    jsonGetOr(s, "BracingWidth", slicerSettings.BracingWidth);
    jsonGetOr(s, "OverlapWidth", slicerSettings.OverlapWidth);
    jsonGetOr(s, "BracingSlotEven", slicerSettings.BracingSlotEven);
    jsonGetOr(s, "BracingSlotOdd", slicerSettings.BracingSlotOdd);
    jsonGetOr(s, "BoundarySlotOffset", slicerSettings.BoundarySlotOffset);
    jsonGetOr(s, "FieldScaleUp", slicerSettings.FieldScaleUp);
    jsonGetOr(s, "FieldCellSize", slicerSettings.FieldCellSize);
  }

  if (j.contains("printSyn") && j["printSyn"].is_object()) {
    const json &p = j["printSyn"];
    jsonGetOr(p, "ResampleLength", printSynSetting.ResampleLength);
    jsonGetOr(p, "FeatureAngleTol", printSynSetting.FeatureAngleTol);
  }

  std::cout << "[zSpaceSlicer] Applied settings overrides from "
            << std::filesystem::absolute(path).string() << std::endl;
  return true;
}

} // anonymous namespace

//---- RENDER SELECTOR & DISPLAY LIST

struct RenderMeshItem {
  zSpace::zObjMesh *mesh = nullptr;
  zDisplayMeshSetting setting;
};

struct RenderGraphItem {
  zSpace::zObjGraph *graph = nullptr;
  zDisplayGraphSetting setting;
};

struct SlicerDisplaySelector {
  // 1. Cached Geometries
  zSpace::zObjMesh flatMesh;
  zSpace::zObjMesh fieldMesh;
  std::vector<zSpace::zObjGraph> flatSegGraphs;
  zSpace::zObjGraph flatBracingGraph;
  zSpace::zObjGraph flatTrimSlotGraph;
  // Per-ring 2D contour graph for the current section, split per-vertex
  // (inner/outer track) nozzle-circle graphs. The final chained resultant
  // 3D path is shown as a graph line only (no pipe mesh).
  std::vector<zSpace::zObjGraph> flatContourGraphs;
  std::vector<zSpace::zObjGraph> innerNozzleCirclesGraphs;
  std::vector<zSpace::zObjGraph> outerNozzleCirclesGraphs;

  // 2. Active Render Items Selected for Direct Scene Submission
  std::vector<RenderMeshItem> activeMeshes;
  std::vector<RenderGraphItem> activeGraphs;

  // 3. Cache & Dirty States
  bool dirty = true;
  bool pipeCacheDirty = true;
  bool unrolledCacheDirty = true;
  int cachedSectionIndex = -1;
  int cachedFieldMode = -1;

  void invalidate(bool invalidate3DPipe = false) {
    dirty = true;
    if (invalidate3DPipe) {
      pipeCacheDirty = true;
      unrolledCacheDirty = true;
    }
  }

  // Updates geometry caches and populates the active render list on-demand
  void update(zSpace::zTs3DP &slicer, zSpace::zObjMesh &inputMesh,
              zSpace::zObjGraph &inputBracingGraph, int currentSection,
              int currentFieldMode, bool showInputMesh, bool showTopBottomMesh,
              bool showLongitudeLoops, bool displaySections, bool showUnrolled,
              bool showAll3DLayers, bool showPipeMesh, bool showNozzleCircles,
              bool showResultantPath, double unrollOffsetX = -20.0) {
    if (!dirty)
      return;

    dirty = false;
    activeMeshes.clear();
    activeGraphs.clear();

    if (!slicer.isLoaded())
      return;

    //---------------------------------------------------------
    // A. 3D Global Input Models
    //---------------------------------------------------------
    if (showInputMesh) {
      zDisplayMeshSetting inMeshDisplay;
      inMeshDisplay.showFaces = true;
      inMeshDisplay.showEdges = true;
      inMeshDisplay.showVertices = false;
      inMeshDisplay.faceColor = Color(0.76f, 0.82f, 0.88f, 0.40f);
      inMeshDisplay.edgeColor = Color(0.15f, 0.15f, 0.18f, 0.60f);
      inMeshDisplay.edgeWidth = 1.0f;
      activeMeshes.push_back({&slicer.getInputMesh(), inMeshDisplay});

      zSpace::zFnGraph fnBracing(inputBracingGraph);
      if (fnBracing.numVertices() > 0) {
        zDisplayGraphSetting inBracingDisplay;
        inBracingDisplay.showEdges = true;
        inBracingDisplay.showVertices = false;
        inBracingDisplay.edgeColor = Color(0.0f, 0.75f, 1.0f, 0.95f);
        inBracingDisplay.edgeWidth = 3.0f;
        activeGraphs.push_back({&inputBracingGraph, inBracingDisplay});
      }
    }

    if (showTopBottomMesh) {
      zDisplayMeshSetting bottomDisplay;
      bottomDisplay.showFaces = true;
      bottomDisplay.showEdges = true;
      bottomDisplay.faceColor = Color(0.35f, 0.85f, 0.35f, 0.65f);
      bottomDisplay.edgeColor = Color(0.1f, 0.7f, 0.1f, 1.0f);
      bottomDisplay.edgeWidth = 2.0f;
      activeMeshes.push_back({&slicer.getSlicer().getUMesh(), bottomDisplay});
    }

    if (showLongitudeLoops) {
      zDisplayGraphSetting loopDisplay;
      loopDisplay.showEdges = true;
      loopDisplay.showVertices = false;
      loopDisplay.edgeColor = Color(1.0f, 0.55f, 0.0f, 1.0f);
      loopDisplay.edgeWidth = 4.0f;
      for (auto &loop : slicer.getSlicer().getVLoops()) {
        activeGraphs.push_back({&loop, loopDisplay});
      }
    }

    //---------------------------------------------------------
    // B. 3D Slices and Toolpaths
    //---------------------------------------------------------
    if (displaySections) {
      auto &sectionMeshes = slicer.getIsoMeshes();
      auto &sectionGraphs = slicer.getSlicer().getIsoMeshBoundary();

      const int totalLayers = static_cast<int>(sectionMeshes.size());
      if (totalLayers > 0) {
        const int startL = showAll3DLayers ? 0 : currentSection;
        const int endL = showAll3DLayers ? totalLayers : (currentSection + 1);

        for (int i = startL; i < endL && i < totalLayers; i++) {
          const bool isCurrent = (i == currentSection);

          // 3D section mesh
          zDisplayMeshSetting secMeshDisplay;
          secMeshDisplay.showFaces = true;
          secMeshDisplay.showEdges = false;
          secMeshDisplay.showVertices = false;
          secMeshDisplay.faceColor = isCurrent
                                         ? Color(0.2f, 0.7f, 0.9f, 0.45f)
                                         : Color(0.7f, 0.7f, 0.75f, 0.08f);
          activeMeshes.push_back({&sectionMeshes[i], secMeshDisplay});

          // 3D section boundary
          if (i < int(sectionGraphs.size())) {
            zDisplayGraphSetting graphDisplay;
            graphDisplay.showEdges = true;
            graphDisplay.showVertices = false;
            graphDisplay.edgeColor = isCurrent
                                         ? Color(0.05f, 0.95f, 0.05f, 1.0f)
                                         : Color(0.3f, 0.7f, 0.3f, 0.4f);
            graphDisplay.edgeWidth = isCurrent ? 5.0f : 1.5f;
            activeGraphs.push_back({&sectionGraphs[i], graphDisplay});
          }

        }
      }
    }

    // 3D chained resultant path (every layer welded end-to-start into one
    // continuous path, post computeChainedPath) - the only 3D toolpath
    // geometry shown now; no per-layer paths, and no pipe mesh (just the
    // graph line) since buildPathMesh's per-point cross-section quads
    // blow up in size for a path this long.
    if (showResultantPath) {
      const zSpace::PrintPath &resultantPath = slicer.getResultantPrintPath();
      zSpace::zFnGraph fnResultant(const_cast<zSpace::zObjGraph &>(resultantPath.graph));
      if (fnResultant.numVertices() > 0 && fnResultant.numEdges() > 0) {
        zDisplayGraphSetting resultantDisplay;
        resultantDisplay.showEdges = true;
        resultantDisplay.showVertices = false;
        resultantDisplay.edgeColor = Color(1.0f, 0.15f, 0.15f, 1.0f);
        resultantDisplay.edgeWidth = 2.5f;
        activeGraphs.push_back({&resultantPath.graph, resultantDisplay});
      }
    }

    //---------------------------------------------------------
    // C. 2D Unrolled View & Scalar Field
    //---------------------------------------------------------
    if (showUnrolled && currentSection >= 0) {
      if (unrolledCacheDirty || cachedSectionIndex != currentSection ||
          cachedFieldMode != currentFieldMode) {
        unrolledCacheDirty = false;
        cachedSectionIndex = currentSection;
        cachedFieldMode = currentFieldMode;

        double offsetX = unrollOffsetX;
        zSpace::zFnMesh fnIn(slicer.getInputMesh());
        if (fnIn.numVertices() > 0) {
          zSpace::zPoint minBB, maxBB;
          fnIn.getBounds(minBB, maxBB);
          double maxDim = (std::max)({maxBB.x - minBB.x, maxBB.y - minBB.y,
                                      maxBB.z - minBB.z});
          if (maxDim > 1e-4) {
            offsetX = -1.5 * maxDim;
          }
        }
        const zSpace::zVector offsetVec(offsetX, 0.0, 0.0);

        const auto &flatMeshes = slicer.getIsoMeshes2D();
        const auto &flatGraphs = slicer.getSlicer().getIsoMeshBoundary2D();
        const auto &flatBracingGraphs = slicer.getSlicer().getBracingGraphs2D();
        const auto &flatPrintPaths2D = slicer.get2DPrintPaths();
        const auto &splitIndices = slicer.getSlicer().getBoundarySplitIndices();

        if (currentSection < int(flatMeshes.size())) {
          flatMesh = flatMeshes[currentSection];
          zSpace::zTsUtils::translateMesh(flatMesh, offsetVec);
        } else {
          zSpace::zFnMesh(flatMesh).clear();
        }

        if (currentFieldMode != 2) {
          const auto &pathFields = slicer.getPathFields();
          const auto &widthFields = slicer.getWidthFields();
          const zSpace::zScalarArray *targetScalars = nullptr;
          if (currentFieldMode == 0 &&
              currentSection < int(pathFields.size())) {
            targetScalars = &pathFields[currentSection];
          } else if (currentFieldMode == 1 &&
                     currentSection < int(widthFields.size())) {
            targetScalars = &widthFields[currentSection];
          }

          if (targetScalars && !targetScalars->empty()) {
            fieldMesh = static_cast<const zSpace::zObjMesh &>(
                slicer.getScalarField());
            if (currentFieldMode == 1) {
              zSpace::zTsUtils::colorWidthField(fieldMesh, *targetScalars);
            } else {
              zSpace::zTsUtils::colorPathField(fieldMesh, *targetScalars);
            }
            zSpace::zTsUtils::translateMesh(fieldMesh, offsetVec);
          } else {
            zSpace::zFnMesh(fieldMesh).clear();
          }
        } else {
          zSpace::zFnMesh(fieldMesh).clear();
        }

        flatSegGraphs.clear();
        if (currentSection < int(flatGraphs.size())) {
          zSpace::zTsUtils::segmentGraph(flatGraphs[currentSection],
                                         splitIndices, flatSegGraphs);
          for (auto &segG : flatSegGraphs) {
            zSpace::zTsUtils::translateGraph(segG, offsetVec);
          }
        }

        if (currentSection < int(flatBracingGraphs.size())) {
          flatBracingGraph = flatBracingGraphs[currentSection];
          zSpace::zTsUtils::translateGraph(flatBracingGraph, offsetVec);
        } else {
          zSpace::zFnGraph(flatBracingGraph).clear();
        }

        zSpace::zFnGraph(flatTrimSlotGraph).clear();

        flatContourGraphs.clear();
        innerNozzleCirclesGraphs.clear();
        outerNozzleCirclesGraphs.clear();
        if (currentSection < int(flatPrintPaths2D.size())) {
          for (const auto &ringPath : flatPrintPaths2D[currentSection]) {
            zSpace::zObjGraph contourG = ringPath.graph;
            zSpace::zTsUtils::translateGraph(contourG, offsetVec);

            zSpace::zFnGraph fnContour(contourG);
            zSpace::zPointArray pts;
            fnContour.getVertexPositions(pts);

            const auto &contourWidths = ringPath.widths;
            const auto &contourTrackIds = ringPath.trackIds;
            const int numContourVerts = int(pts.size());

            zSpace::zPointArray innerCircleVerts, outerCircleVerts;
            zSpace::zIntArray innerCircleEdges, outerCircleEdges;
            if (numContourVerts >= 2) {
              constexpr int circleSegs = 16;
              for (int v = 0; v < numContourVerts; v++) {
                float printWidth = 0.20f;
                if (v < int(contourWidths.size())) {
                  printWidth = contourWidths[v];
                }
                if (printWidth <= 1e-4f)
                  printWidth = 0.20f;
                const float radius = printWidth * 0.5f;

                const bool isOuter = (v < int(contourTrackIds.size())) &&
                                     contourTrackIds[v] >= 0.5f;
                zSpace::zPointArray &circleVerts =
                    isOuter ? outerCircleVerts : innerCircleVerts;
                zSpace::zIntArray &circleEdges =
                    isOuter ? outerCircleEdges : innerCircleEdges;

                const int baseIdx = static_cast<int>(circleVerts.size());
                for (int c = 0; c < circleSegs; c++) {
                  const float angle =
                      (6.28318530718f * c) / static_cast<float>(circleSegs);
                  circleVerts.push_back(zSpace::zPoint(
                      pts[v].x + std::cos(angle) * radius,
                      pts[v].y + std::sin(angle) * radius, pts[v].z));
                  circleEdges.push_back(baseIdx + c);
                  circleEdges.push_back(baseIdx + ((c + 1) % circleSegs));
                }
              }
            }

            zSpace::zObjGraph innerCirclesG, outerCirclesG;
            if (!innerCircleVerts.empty())
              zSpace::zFnGraph(innerCirclesG)
                  .create(innerCircleVerts, innerCircleEdges);
            if (!outerCircleVerts.empty())
              zSpace::zFnGraph(outerCirclesG)
                  .create(outerCircleVerts, outerCircleEdges);

            flatContourGraphs.push_back(contourG);
            innerNozzleCirclesGraphs.push_back(innerCirclesG);
            outerNozzleCirclesGraphs.push_back(outerCirclesG);
          }
        }
      }

      // 2D unrolled mesh
      zSpace::zFnMesh fnFlat(flatMesh);
      if (fnFlat.numVertices() > 0) {
        zDisplayMeshSetting flatMeshDisplay;
        flatMeshDisplay.showFaces = true;
        flatMeshDisplay.showEdges = true;
        flatMeshDisplay.showVertices = false;
        flatMeshDisplay.faceColor = Color(0.92f, 0.95f, 0.98f, 0.85f);
        flatMeshDisplay.edgeColor = Color(0.35f, 0.45f, 0.55f, 0.60f);
        flatMeshDisplay.edgeWidth = 1.2f;
        activeMeshes.push_back({&flatMesh, flatMeshDisplay});
      }

      // 2D scalar field
      if (currentFieldMode != 2) {
        zSpace::zFnMesh fnField(fieldMesh);
        if (fnField.numVertices() > 0 && fnField.numPolygons() > 0) {
          zDisplayMeshSetting fieldDisplay;
          fieldDisplay.showFaces = true;
          fieldDisplay.showEdges = false;
          fieldDisplay.showVertices = false;
          fieldDisplay.useMeshColors = true;
          fieldDisplay.faceColor = Color(1.0f, 1.0f, 1.0f, 0.85f);
          activeMeshes.push_back({&fieldMesh, fieldDisplay});
        }
      }

      // 2D boundary segment graphs
      const int numSegs = static_cast<int>(flatSegGraphs.size());
      std::vector<zSpace::zColor> segPalette;
      if (numSegs > 0) {
        zSpace::zTsUtils::generateColorPalette(numSegs, segPalette, 1337, 0.85f,
                                               0.50f);
      }
      for (size_t seg = 0; seg < flatSegGraphs.size(); seg++) {
        zSpace::zFnGraph fnSeg(flatSegGraphs[seg]);
        if (fnSeg.numVertices() > 0 && fnSeg.numEdges() > 0 &&
            !segPalette.empty()) {
          const zSpace::zColor &zc = segPalette[seg % segPalette.size()];
          Color c(zc.r, zc.g, zc.b, zc.a);
          zDisplayGraphSetting flatSegDisplay;
          flatSegDisplay.showEdges = true;
          flatSegDisplay.showVertices = false;
          flatSegDisplay.edgeColor = c;
          flatSegDisplay.edgeWidth = 4.5f;
          activeGraphs.push_back({&flatSegGraphs[seg], flatSegDisplay});
        }
      }

      // 2D internal bracing
      zSpace::zFnGraph fnBracing(flatBracingGraph);
      if (fnBracing.numVertices() > 0 && fnBracing.numEdges() > 0) {
        zDisplayGraphSetting flatBracingDisplay;
        flatBracingDisplay.showEdges = true;
        flatBracingDisplay.showVertices = false;
        flatBracingDisplay.edgeColor = Color(0.0f, 0.75f, 1.0f, 1.0f);
        flatBracingDisplay.edgeWidth = 3.5f;
        activeGraphs.push_back({&flatBracingGraph, flatBracingDisplay});
      }

      // 2D trim slots
      zSpace::zFnGraph fnTrimSlot(flatTrimSlotGraph);
      if (fnTrimSlot.numVertices() > 0 && fnTrimSlot.numEdges() > 0) {
        zDisplayGraphSetting slotDisplay;
        slotDisplay.showEdges = true;
        slotDisplay.showVertices = false;
        slotDisplay.edgeColor = Color(0.1f, 0.3f, 1.0f, 1.0f);
        slotDisplay.edgeWidth = 4.0f;
        activeGraphs.push_back({&flatTrimSlotGraph, slotDisplay});
      }

      // 2D toolpaths, per ring (single color for the path; nozzle circles
      // still split inner cyan / outer magenta below)
      for (size_t r = 0; r < flatContourGraphs.size(); r++) {
        zSpace::zFnGraph fnFlatContour(flatContourGraphs[r]);
        if (fnFlatContour.numVertices() > 0 && fnFlatContour.numEdges() > 0) {
          zDisplayGraphSetting contourDisplay;
          contourDisplay.showEdges = true;
          contourDisplay.showVertices = true;
          contourDisplay.edgeColor = Color(0.85f, 0.85f, 0.2f, 1.0f);
          contourDisplay.edgeWidth = 4.5f;
          contourDisplay.vertexColor = Color(1.0f, 0.45f, 0.1f, 1.0f);
          contourDisplay.vertexSize = 4.0f;
          activeGraphs.push_back({&flatContourGraphs[r], contourDisplay});

          if (showNozzleCircles) {
            if (r < innerNozzleCirclesGraphs.size()) {
              zSpace::zFnGraph fnInnerCircles(innerNozzleCirclesGraphs[r]);
              if (fnInnerCircles.numVertices() > 0 &&
                  fnInnerCircles.numEdges() > 0) {
                zDisplayGraphSetting innerCircleDisplay;
                innerCircleDisplay.showEdges = true;
                innerCircleDisplay.showVertices = false;
                innerCircleDisplay.edgeColor = Color(0.0f, 1.0f, 1.0f, 0.85f);
                innerCircleDisplay.edgeWidth = 1.4f;
                activeGraphs.push_back(
                    {&innerNozzleCirclesGraphs[r], innerCircleDisplay});
              }
            }
            if (r < outerNozzleCirclesGraphs.size()) {
              zSpace::zFnGraph fnOuterCircles(outerNozzleCirclesGraphs[r]);
              if (fnOuterCircles.numVertices() > 0 &&
                  fnOuterCircles.numEdges() > 0) {
                zDisplayGraphSetting outerCircleDisplay;
                outerCircleDisplay.showEdges = true;
                outerCircleDisplay.showVertices = false;
                outerCircleDisplay.edgeColor = Color(1.0f, 0.0f, 1.0f, 0.85f);
                outerCircleDisplay.edgeWidth = 1.4f;
                activeGraphs.push_back(
                    {&outerNozzleCirclesGraphs[r], outerCircleDisplay});
              }
            }
          }
        }
      }
    }
  }

  // Draw all selected geometry items directly into scene - O(N) direct loop
  // with 0 conditionals
  void draw(Scene &scene) const {
    for (const auto &item : activeMeshes) {
      if (item.mesh) {
        scene.draw(*item.mesh, item.setting);
      }
    }
    for (const auto &item : activeGraphs) {
      if (item.graph) {
        scene.draw(*item.graph, item.setting);
      }
    }
  }
};

class zSpaceSlicerSketch : public ISketch {
public:
  std::string getName() const override { return "zSpace Slicer"; }
  std::string getDescription() const override {
    return "TopologySlicer in zspace_toolsets.";
  }
  std::string getAuthor() const override { return "Weijie Tao"; }
  std::string getVersion() const override { return "1.0.0"; }

  void setup() override {
    scene().setBackgroundColor(Color(1.0f, 1.0f, 1.0f, 1.0f));
    scene().setShowGrid(false);
    scene().setGridSize(8.0f);
    scene().setGridDivisions(8);
    scene().setShowAxes(false);
    scene().setAxesLength(1.5f);

    std::cout << "[zSpaceSlicer] No mesh loaded yet. Press 'i' in the "
                 "viewport to browse for a mesh/bracing-graph OBJ file."
              << std::endl;
  }

  void update(float) override {}

  void draw(Renderer &renderer, Camera &) override {
    // 1. Update display selector list on-demand (only executes when dirty)
    m_displaySelector.update(My3DP, m_rawInputMesh, m_rawBracingGraph,
                             currentSection, static_cast<int>(currentFieldMode),
                             showInputMesh, showTopBottomMesh,
                             showLongitudeLoops, displaySections, showUnrolled,
                             showAll3DLayers, showPipeMesh, showNozzleCircles,
                             showResultantPath);

    // 2. Direct O(N) draw loop over selected geometry items (0 conditionals, 0
    m_displaySelector.draw(scene());

    // 3. Text overlay, one line at a time: title, stage, hotkeys, mesh,
    // graph, then every slicer/print-syn setting parameter.
    renderer.setColor(Color(0.02f, 0.02f, 0.02f, 1.0f));
    float y = 28.0f;
    const float lineHeight = 18.0f;
    auto drawLine = [&](const std::string &text) {
      renderer.drawString(text, 10, y);
      y += lineHeight;
    };

    // 1. Title
    drawLine(getName());

    // 2. Stage
    std::string stageName = My3DP.isLoaded()
                                ? "Input Mesh (Press 'p' to slice)"
                                : "No mesh loaded (Press 'i' to load)";
    if (currentStage == PipelineStage::SLICER_COMPUTED)
      stageName = "Fields Computed (Structure/Slice/Unroll/Field, Press 'p' "
                  "to extract 2D toolpaths)";
    else if (currentStage == PipelineStage::EXTRACTED)
      stageName = "2D Toolpaths Extracted (Press 'p' to post-process)";
    else if (currentStage == PipelineStage::PRINTSYN_COMPUTED)
      stageName = "2D Toolpaths Post-Processed (Cleaned & Resampled, Press 'p' "
                  "to project 3D)";
    else if (currentStage == PipelineStage::PROJECTED_3D)
      stageName = "Toolpaths Generated (3D & 2D paths ready, Press 'p' to "
                  "chain layers into one path)";
    else if (currentStage == PipelineStage::CHAINED)
      stageName = "Layers Chained (one continuous 3D path ready)";
    drawLine("Stage: " + stageName + " | " + My3DP.getStatus());

    // 3. Hotkeys
    drawLine(
        "[Hotkeys] 'i': import mesh/graph/settings (file dialogs, clears all "
        "first), 'r': reset pipeline (keeps mesh/graph/settings), 'p': next "
        "step, 'u': toggle 2D, 'v': 3D pipe, "
        "'a': 3D all/single, 'c': circles, 'l': field, 'w': chained path, "
        "'Up/Down': layer");

    // 4. Mesh
    drawLine("Mesh: " + meshPath);

    // 5. Graph
    drawLine("Bracing Graph: " + bracingGraphPath);

    // 6. Settings parameters, one per line
    for (const auto &line : buildSettingsLines())
      drawLine(line);
  }

  bool onKeyPress(unsigned char key, int, int) override {
    if (key == 'i' || key == 'I') {
      clearAll();
      promptLoadFiles();
      currentStage = PipelineStage::RAW;
      invalidateCache(true);
      return true;
    }

    if (key == 'r' || key == 'R') {
      resetPipeline();
      return true;
    }

    if (key == 'p' || key == 'P') {
      if (currentStage == PipelineStage::RAW ||
          My3DP.getIsoMeshes().empty()) {
        if (My3DP.computeSlicer()) {
          currentStage = PipelineStage::SLICER_COMPUTED;
          std::cout << "[zSpaceSlicer] [Step 1] Slicer field computation "
                       "finished (structure/slice/unroll/field). Press 'p' "
                       "again to extract 2D toolpaths."
                    << std::endl;
        } else {
          printError("[zSpaceSlicer] [Step 1] Slicer pipeline failed: " +
                     My3DP.getStatus());
        }
      } else if (currentStage == PipelineStage::SLICER_COMPUTED) {
        if (My3DP.computePostProcess()) {
          currentStage = PipelineStage::CHAINED;
          std::cout << "[zSpaceSlicer] [Step 2] Toolpath synthesis finished "
                       "(extracted, resampled, aligned, projected, chained)."
                    << std::endl;
        } else {
          printError("[zSpaceSlicer] [Step 2] Toolpath synthesis failed: " +
                     My3DP.getStatus());
        }
      }
      invalidateCache(true);
      return true;
    }

    if (key == 'b' || key == 'B') {
      showInputMesh = !showInputMesh;
      invalidateCache();
      std::cout << "[zSpaceSlicer] Input mesh & graph display: "
                << (showInputMesh ? "on" : "off") << std::endl;
      return true;
    }

    if (key == 'm' || key == 'M') {
      showTopBottomMesh = !showTopBottomMesh;
      invalidateCache();
      std::cout << "[zSpaceSlicer] Top/Bottom meshes display: "
                << (showTopBottomMesh ? "on" : "off") << std::endl;
      return true;
    }

    if (key == 'l' || key == 'L') {
      if (currentFieldMode == FieldMode::PATH_FIELD) {
        currentFieldMode = FieldMode::WIDTH_FIELD;
        std::cout << "[zSpaceSlicer] Field display: Width Field" << std::endl;
      } else if (currentFieldMode == FieldMode::WIDTH_FIELD) {
        currentFieldMode = FieldMode::NONE;
        std::cout << "[zSpaceSlicer] Field display: Off" << std::endl;
      } else {
        currentFieldMode = FieldMode::PATH_FIELD;
        std::cout << "[zSpaceSlicer] Field display: Path Field" << std::endl;
      }
      invalidateCache();
      return true;
    }

    if (key == 'u' || key == 'U') {
      showUnrolled = !showUnrolled;
      invalidateCache();
      std::cout << "[zSpaceSlicer] 2D Unrolled display: "
                << (showUnrolled ? "on" : "off") << std::endl;
      return true;
    }

    if (key == 'd' || key == 'D') {
      displaySections = !displaySections;
      invalidateCache();
      std::cout << "[zSpaceSlicer] Sections display: "
                << (displaySections ? "on" : "off") << std::endl;
      return true;
    }

    if (key == 'f' || key == 'F') {
      focusOnMesh();
      return true;
    }

    if (key == 'v' || key == 'V') {
      showPipeMesh = !showPipeMesh;
      invalidateCache();
      std::cout << "[zSpaceSlicer] 3D Toolpath Pipe Mesh display: "
                << (showPipeMesh ? "on" : "off") << std::endl;
      return true;
    }

    if (key == 'a' || key == 'A') {
      showAll3DLayers = !showAll3DLayers;
      invalidateCache();
      std::cout << "[zSpaceSlicer] Show all 3D layers: "
                << (showAll3DLayers ? "on" : "current only") << std::endl;
      return true;
    }

    if (key == 'c' || key == 'C') {
      showNozzleCircles = !showNozzleCircles;
      invalidateCache();
      std::cout << "[zSpaceSlicer] Nozzle width circles display: "
                << (showNozzleCircles ? "on" : "off") << std::endl;
      return true;
    }

    if (key == 'w' || key == 'W') {
      showResultantPath = !showResultantPath;
      invalidateCache();
      std::cout << "[zSpaceSlicer] Chained resultant path display: "
                << (showResultantPath ? "on" : "off") << std::endl;
      return true;
    }

    if (key == 201 || key == ']') {
      return changeSection(1);
    }

    if (key == 203 || key == '[') {
      return changeSection(-1);
    }

    return false;
  }

private:
  enum class PipelineStage {
    RAW = 0,
    SLICER_COMPUTED = 1,
    EXTRACTED = 2,
    PRINTSYN_COMPUTED = 3,
    PROJECTED_3D = 4,
    CHAINED = 5
  };

  zSpace::zTs3DP My3DP;
  zSpace::zObjMesh m_rawInputMesh;
  zSpace::zObjGraph m_rawBracingGraph;

  std::string meshPath;
  std::string bracingGraphPath;

  //=========================================================================
  //---- SLICER CONFIGURATION PARAMETERS
  //=========================================================================
  // My3DP.getSettings()/getPrintSynSetting() already carry SlicerSettings'/
  // PrintSynSetting's own struct default member values; this only applies
  // whatever overrides are present in settingsJsonPath (picked via 'i', left
  // empty if that dialog was cancelled) on top of those, then remembers the
  // result so 'r' can restore it onto a freshly reset My3DP.
  void applySlicerSettings() {
    zSpace::SlicerSettings &slicerSettings = My3DP.getSettings();
    zSpace::PrintSynSetting &printSynSetting = My3DP.getPrintSynSetting();
    if (!settingsJsonPath.empty())
      loadSettingsFromJson(settingsJsonPath, slicerSettings, printSynSetting);
    m_savedSlicerSettings = slicerSettings;
    m_savedPrintSynSetting = printSynSetting;
  }

  std::string settingsJsonPath;
  zSpace::SlicerSettings m_savedSlicerSettings;
  zSpace::PrintSynSetting m_savedPrintSynSetting;

  // Formats every SlicerSettings/PrintSynSetting field as one "name: value"
  // line, for the on-screen settings dump in draw().
  std::vector<std::string> buildSettingsLines() const {
    const zSpace::SlicerSettings &s = My3DP.getSettings();
    const zSpace::PrintSynSetting &p = My3DP.getPrintSynSetting();

    auto fmtFloat = [](const char *label, float v) {
      char buf[64];
      std::snprintf(buf, sizeof(buf), "%s: %.4f", label, v);
      return std::string(buf);
    };
    auto fmtInt = [](const char *label, int v) {
      return std::string(label) + ": " + std::to_string(v);
    };
    auto fmtBool = [](const char *label, bool v) {
      return std::string(label) + ": " + (v ? "true" : "false");
    };

    std::vector<std::string> lines;
    lines.push_back("-- Slicer Settings --");
    lines.push_back(fmtFloat("LayerSpacing", s.LayerSpacing));
    lines.push_back(fmtBool("TTopoSplit", s.TTopoSplit));
    lines.push_back(fmtBool("TAngleSplit", s.TAngleSplit));
    lines.push_back(fmtFloat("TAngleSplitTol", s.TAngleSplitTol));
    lines.push_back(fmtBool("DoubleBoundary", s.DoubleBoundary));
    {
      std::string widths = "BoundaryWidth: [";
      for (size_t i = 0; i < s.BoundaryWidth.size(); i++) {
        if (i > 0)
          widths += ", ";
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.3f", s.BoundaryWidth[i]);
        widths += buf;
      }
      widths += "]";
      lines.push_back(widths);
    }
    lines.push_back(fmtFloat("BracingWidth", s.BracingWidth));
    lines.push_back(fmtFloat("OverlapWidth", s.OverlapWidth));
    lines.push_back(fmtFloat("BracingSlotEven", s.BracingSlotEven));
    lines.push_back(fmtFloat("BracingSlotOdd", s.BracingSlotOdd));
    lines.push_back(fmtFloat("BoundarySlotOffset", s.BoundarySlotOffset));
    lines.push_back(fmtFloat("FieldScaleUp", s.FieldScaleUp));
    lines.push_back(fmtFloat("FieldCellSize", s.FieldCellSize));

    lines.push_back("-- PrintSyn Settings --");
    lines.push_back(fmtFloat("ResampleLength", p.ResampleLength));
    lines.push_back(fmtFloat("FeatureAngleTol", p.FeatureAngleTol));
    lines.push_back(fmtFloat("SeamOffset", p.SeamOffset));
    lines.push_back(fmtFloat("SeamSize", p.SeamSize));

    return lines;
  }

  enum class FieldMode { PATH_FIELD, WIDTH_FIELD, NONE };

  PipelineStage currentStage = PipelineStage::RAW;
  bool showInputMesh = true;
  bool showTopBottomMesh = false;
  bool showLongitudeLoops = false;
  bool displaySections = true;
  bool showUnrolled = true;
  bool showAll3DLayers = true;
  bool showNozzleCircles = true;
  bool showPipeMesh = true;
  bool showResultantPath = true;
  FieldMode currentFieldMode = FieldMode::PATH_FIELD;
  int currentSection = 0;

  // 2D/3D Render Display Selector
  SlicerDisplaySelector m_displaySelector;

  void invalidateCache(bool invalidate3DPipe = false) {
    m_displaySelector.invalidate(invalidate3DPipe);
  }

  static Vec3 toVec3(const zSpace::zVector &p) {
    return Vec3(static_cast<float>(p.x), static_cast<float>(p.y),
                static_cast<float>(p.z));
  }

  bool getMeshBoundsInfo(Vec3 &outCenter, float &outMaxDim) {
    if (!My3DP.isLoaded())
      return false;
    zSpace::zPoint minBB, maxBB;
    zSpace::zFnMesh fn(My3DP.getInputMesh());
    fn.getBounds(minBB, maxBB);
    outCenter = (toVec3(minBB) + toVec3(maxBB)) * 0.5f;
    outMaxDim = std::max({static_cast<float>(maxBB.x - minBB.x),
                          static_cast<float>(maxBB.y - minBB.y),
                          static_cast<float>(maxBB.z - minBB.z)});
    return true;
  }

  // Resets computed pipeline state (My3DP outputs, current stage/section)
  // but keeps the currently loaded input mesh/bracing graph/settings, so
  // 'r' can re-run from scratch without reloading files.
  void resetPipeline() {
    My3DP = zSpace::zTs3DP();
    if (zSpace::zFnMesh(m_rawInputMesh).numVertices() > 0)
      My3DP.setInputMesh(m_rawInputMesh);
    if (zSpace::zFnGraph(m_rawBracingGraph).numVertices() > 0)
      My3DP.setBracingGraph(m_rawBracingGraph);
    My3DP.getSettings() = m_savedSlicerSettings;
    My3DP.getPrintSynSetting() = m_savedPrintSynSetting;

    currentStage = PipelineStage::RAW;
    currentSection = 0;
    invalidateCache(true);

    printSuccess(
        "[zSpaceSlicer] Pipeline reset. Input mesh/graph/settings kept.");
  }

  // Clears every loaded input and computed/rendered result, taking the
  // sketch back to its just-launched state. Called before 'i' re-imports,
  // so pressing 'i' always starts from a clean slate.
  void clearAll() {
    My3DP = zSpace::zTs3DP();
    zSpace::zFnMesh(m_rawInputMesh).clear();
    zSpace::zFnGraph(m_rawBracingGraph).clear();
    meshPath.clear();
    bracingGraphPath.clear();
    settingsJsonPath.clear();
    m_savedSlicerSettings = zSpace::SlicerSettings();
    m_savedPrintSynSetting = zSpace::PrintSynSetting();

    currentStage = PipelineStage::RAW;
    currentSection = 0;
    invalidateCache(true);
  }

  // Opens native "Open File" dialogs for the mesh, bracing graph, and
  // settings JSON, in that order (blocks the render loop while a dialog is
  // up, same as any modal dialog). Cancelling the mesh dialog aborts the
  // whole load; cancelling the bracing graph or settings dialogs just
  // leaves that path empty (bracing graph / slicer settings default values
  // are used).
  void promptLoadFiles() {
    static const char *kObjFilter =
        "Wavefront OBJ (*.obj)\0*.obj\0All Files (*.*)\0*.*\0";
    static const char *kJsonFilter =
        "JSON Files (*.json)\0*.json\0All Files (*.*)\0*.*\0";

    std::string chosenMesh;
    if (!openFileDialog(kObjFilter, "Select Mesh OBJ File", chosenMesh)) {
      printError("[zSpaceSlicer] Mesh selection cancelled.");
      return;
    }
    meshPath = chosenMesh;

    std::string chosenBracing;
    if (openFileDialog(kObjFilter,
                       "Select Bracing Graph OBJ File (Cancel to skip)",
                       chosenBracing)) {
      bracingGraphPath = chosenBracing;
    } else {
      printError("[zSpaceSlicer] Bracing graph selection cancelled, keeping: " +
                 bracingGraphPath);
    }

    std::string chosenSettings;
    if (openFileDialog(kJsonFilter,
                       "Select Slicer Settings JSON (Cancel to use defaults)",
                       chosenSettings)) {
      settingsJsonPath = chosenSettings;
    } else {
      printError(
          "[zSpaceSlicer] Settings JSON selection cancelled, using struct "
          "defaults.");
    }

    loadMesh();
  }

  bool loadMesh() {
    applySlicerSettings();

    auto result = zSpace::zIO::readMesh(meshPath, m_rawInputMesh);
    if (!result) {
      printError("[zSpaceSlicer] Could not load mesh: " + result.message());
      return false;
    }

    My3DP.setInputMesh(m_rawInputMesh);
    loadBracingGraph();

    currentStage = PipelineStage::RAW;
    showInputMesh = true;
    focusOnMesh();

    zSpace::zFnMesh fn(m_rawInputMesh);
    printSuccess("[zSpaceSlicer] Loaded mesh: vertices=" +
                 std::to_string(fn.numVertices()) +
                 ", faces=" + std::to_string(fn.numPolygons()));
    return true;
  }

  bool loadBracingGraph() {
    // zIO::readGraph does not register the .obj extension for graphs (only
    // readMesh supports it), so .obj bracing graphs go through zTsUtils'
    // dedicated OBJ graph importer instead.
    std::filesystem::path srcPath(bracingGraphPath);
    if (srcPath.extension() == ".obj" || srcPath.extension() == ".OBJ") {
      if (!zSpace::zTsIO::readGraphFromOBJ(bracingGraphPath,
                                              m_rawBracingGraph)) {
        printError("[zSpaceSlicer][bracing] Could not parse OBJ graph: " +
                   bracingGraphPath);
        return false;
      }
    } else {
      auto result = zSpace::zIO::readGraph(bracingGraphPath, m_rawBracingGraph);
      if (!result) {
        printError("[zSpaceSlicer][bracing] Could not load graph: " +
                   result.message());
        return false;
      }
    }

    My3DP.setBracingGraph(m_rawBracingGraph);
    zSpace::zFnGraph fnGraph(m_rawBracingGraph);
    printSuccess("[zSpaceSlicer] Loaded graph: " +
                 std::to_string(fnGraph.numEdges()) + " edges.");
    return true;
  }

  void focusOnMesh() {
    Vec3 center;
    float maxDim;
    if (!getMeshBoundsInfo(center, maxDim))
      return;

    auto &camController = Application::getInstance()->getCameraController();
    camController.setOrbitCenter(center);
    camController.setOrbitDistance(maxDim * 1.8f);
  }

  bool changeSection(int delta) {
    const int total =
        static_cast<int>(My3DP.getIsoMeshes().size());
    if (total <= 0)
      return false;
    currentSection = (currentSection + delta + total) % total;
    invalidateCache(false);
    std::cout << "[zSpaceSlicer] Current layer: " << currentSection << " / "
              << (total - 1) << std::endl;
    return true;
  }
};

ALICE2_REGISTER_SKETCH_AUTO(zSpaceSlicerSketch)

#endif // __MAIN__

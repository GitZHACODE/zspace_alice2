#include "slicer.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
namespace alice2::slicer {
void focusCameraOnInputMesh(SlicerState &state, Scene &scene) {

  zSpace::zPoint minBB;
  zSpace::zPoint maxBB;
  zSpace::zFnMesh fn(state.inputMesh);
  if (fn.numVertices() == 0)
    return;
  fn.getBounds(minBB, maxBB);
  const auto includePoint = [&](const zSpace::zPoint &p) {
    minBB.x = (std::min)(minBB.x, p.x);
    minBB.y = (std::min)(minBB.y, p.y);
    minBB.z = (std::min)(minBB.z, p.z);
    maxBB.x = (std::max)(maxBB.x, p.x);
    maxBB.y = (std::max)(maxBB.y, p.y);
    maxBB.z = (std::max)(maxBB.z, p.z);
  };
  if (state.haveBracing) {
    zSpace::zPointArray points;
    zSpace::zFnGraph(state.bracingGraph).getVertexPositions(points);
    for (const auto &p : points)
      includePoint(p);
  }
  if (state.haveSlotAnchor)
    includePoint(state.slotAnchor);

  auto *app = Application::getInstance();
  auto &camera = app->getCamera();
  auto &controller = app->getCameraController();
  const Vec3 low(static_cast<float>(minBB.x), static_cast<float>(minBB.y),
                 static_cast<float>(minBB.z));
  const Vec3 high(static_cast<float>(maxBB.x), static_cast<float>(maxBB.y),
                  static_cast<float>(maxBB.z));
  const float radius = (std::max)(0.001f, (high - low).length() * 0.5f);
  if (!std::isfinite(radius))
    return;
  const float aspect = (std::max)(0.01f, camera.getAspectRatio());
  const float halfFov =
      (std::max)(1.0f, (std::min)(170.0f, camera.getFieldOfView())) *
      0.00872664626f;
  const float limitingAngle =
      (std::min)(halfFov, std::atan(std::tan(halfFov) * aspect));
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

zDisplayMeshSetting ghostMeshDisplay(SlicerState &state, Scene &scene) {

  zDisplayMeshSetting meshDisplay;
  meshDisplay.showVertices = false;
  meshDisplay.showEdges = true;
  meshDisplay.showFaces = true;
  meshDisplay.faceColor = Color(0.75f, 0.8f, 0.9f, 0.35f);
  meshDisplay.edgeColor = Color(0.75f, 0.8f, 0.9f, 0.45f);
  meshDisplay.edgeWidth = 1.0f;
  return meshDisplay;
}

void drawVLoops(SlicerState &state, Scene &scene) {

  for (zSpace::zObjGraph &g : state.topoSlicer->getVLoops())
    scene.draw(g, Display::lines(Color(0.05f, 0.05f, 0.05f, 1.0f), 3.0f));
}

void drawSectionMeshes(SlicerState &state, Scene &scene) {

  zDisplayMeshSetting wire;
  wire.showVertices = false;
  wire.showFaces = false;
  wire.showEdges = true;
  wire.edgeColor = Color(0.1f, 0.25f, 0.8f, 0.8f);
  wire.edgeWidth = 1.0f;
  for (zSpace::zObjMesh &m : state.topoSlicer->getIsoMeshes())
    scene.draw(m, wire);
}

void drawSlotAnchor(SlicerState &state, Scene &scene, Renderer &renderer) {

  if (!state.haveSlotAnchor)
    return;
  renderer.drawPoint(
      Vec3(static_cast<float>(state.slotAnchor.x),
           static_cast<float>(state.slotAnchor.y),
           static_cast<float>(state.slotAnchor.z)),
      Color(0.85f, 0.1f, 0.6f, 1.0f),
      (state.displayMode == 0 || state.slicerChoice == kSlicerNone) ? 20.0f
                                                                    : 12.0f);
}

void drawInputPreview(SlicerState &state, Scene &scene) {

  if (!state.inputLoaded)
    return;
  auto display = ghostMeshDisplay(state, scene);
  display.faceColor = Color(0.75f, 0.8f, 0.9f, 0.2f);
  display.edgeColor = Color(0.15f, 0.2f, 0.3f, 0.8f);
  display.edgeWidth = 1.5f;
  scene.draw(state.inputMesh, display);
  if (state.haveBracing)
    scene.draw(state.bracingGraph,
               Display::lines(Color(0.9f, 0.45f, 0.0f, 1.0f), 6.0f));
}

void drawBracingGraph(SlicerState &state, Scene &scene) {

  if (state.haveBracing)
    scene.draw(state.bracingGraph,
               Display::lines(Color(0.9f, 0.45f, 0.0f, 1.0f), 3.0f));
}

void drawGhostInputMesh(SlicerState &state, Scene &scene) {

  scene.draw(state.inputMesh, ghostMeshDisplay(state, scene));
}

void buildInputGroups(SlicerState &state, Scene &scene,
                      std::string &outSummary) {

  state.inputGroupMeshes.clear();
  state.inputGroupColors.clear();
  state.widthPalette.clear();
  zSpace::zFnMesh fn(state.inputMesh);
  zSpace::zPointArray positions;
  fn.getVertexPositions(positions);
  zSpace::zIntArray connects, counts;
  fn.getPolygonData(connects, counts);
  if (positions.empty() || counts.size() != state.faceTags.size())
    return;

  // Group key per face: -2 bottom, -1 top, n >= 0 side_<n>.
  constexpr int kBottomKey = -2, kTopKey = -1;
  zSpace::zIntArray faceOffset(counts.size(), 0);
  std::map<int, zSpace::zIntArray> groupFaces;
  int offset = 0;
  for (size_t f = 0; f < counts.size(); ++f) {
    faceOffset[f] = offset;
    offset += counts[f];
    int key = (f < state.faceWidthIds.size()) ? state.faceWidthIds[f] : 0;
    if (state.faceTags[f] == static_cast<int>(zSpace::VertexTag::Bottom))
      key = kBottomKey;
    else if (state.faceTags[f] == static_cast<int>(zSpace::VertexTag::Top))
      key = kTopKey;
    groupFaces[key].push_back(static_cast<int>(f));
  }

  // The width field palette: printed width of every side group (+ bracing).
  const auto printed = [&](int group) {
    return zSpace::zTsField::printedWidth(
        zSpace::zTsField::widthOfGroup(state.settings.BoundaryWidth, group),
        true, state.settings.DoubleBoundary, state.settings.OverlapWidth);
  };
  zSpace::zScalarArray sideValues;
  for (const auto &entry : groupFaces)
    if (entry.first >= 0)
      sideValues.push_back(printed(entry.first));
  state.widthPalette = sideValues;
  if (state.haveBracing)
    state.widthPalette.push_back(zSpace::zTsField::printedWidth(
        state.settings.BracingWidth, false, state.settings.DoubleBoundary,
        state.settings.OverlapWidth));
  zSpace::zColorArray sideColors;
  zSpace::zTsUtils::colorWidthField(sideValues, sideColors,
                                    &state.widthPalette);

  // One exploded mesh per group (std::map: bottom, top, then side_0,
  // side_1, ...), each created in place in the pre-sized array - a
  // zObjMesh is never moved after creation.
  state.inputGroupMeshes.assign(groupFaces.size(), zSpace::zObjMesh());
  size_t groupIndex = 0;
  size_t sideIndex = 0;
  std::ostringstream summary;
  for (const auto &entry : groupFaces) {
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
    zSpace::zFnMesh(state.inputGroupMeshes[groupIndex++])
        .create(groupPositions, groupCounts, groupConnects);

    Color color(1.0f, 1.0f, 1.0f, 1.0f);
    if (entry.first == kBottomKey) {
      color = Color(0.0f, 0.0f, 0.0f, 1.0f);
      summary << " bottom=" << entry.second.size();
    } else if (entry.first == kTopKey) {
      summary << " top=" << entry.second.size();
    } else {
      const zSpace::zColor &c = sideColors[sideIndex++];
      color = Color(c.r, c.g, c.b, 1.0f);
      summary << " side_" << entry.first << "=" << entry.second.size()
              << " (w=" << printed(entry.first) << ")";
    }
    state.inputGroupColors.push_back(color);
  }
  outSummary = summary.str();
}

void drawInputColored(SlicerState &state, Scene &scene) {

  zDisplayMeshSetting group;
  group.showVertices = false;
  group.showEdges = true;
  group.edgeColor = Color(0.35f, 0.35f, 0.35f, 1.0f);
  group.edgeWidth = 1.0f;
  group.showFaces = true;
  for (size_t g = 0;
       g < state.inputGroupMeshes.size() && g < state.inputGroupColors.size();
       ++g) {
    group.faceColor = state.inputGroupColors[g];
    scene.draw(state.inputGroupMeshes[g], group);
  }
}

void drawFailedTraces(SlicerState &state, Scene &scene, Renderer &renderer) {

  zSpace::zObjGraphArray &graphs = state.slicer->getTraceGraphs();
  const auto &valid = state.slicer->getVolumetricParameterizationValidity();
  const auto &vertices = state.slicer->getTetVolume().Vertices;
  const auto &markers = state.slicer->getTetVolume().vertextag;
  const Color color(0.95f, 0.15f, 0.1f, 1.0f);
  for (size_t i = 0;
       i < graphs.size() && i < valid.size() && i < vertices.size(); ++i) {
    if (valid[i] ||
        (i < markers.size() && markers[i] == zSpace::VertexTag::Bottom))
      continue;
    drawTrace(state, scene, renderer, graphs[i], vertices[i].Position, color);
  }
}

void drawTopSuccessTraces(SlicerState &state, Scene &scene,
                          Renderer &renderer) {

  zSpace::zObjGraphArray &graphs = state.slicer->getTraceGraphs();
  const auto &valid = state.slicer->getVolumetricParameterizationValidity();
  const auto &vertices = state.slicer->getTetVolume().Vertices;
  const Color color(0.1f, 0.7f, 0.2f, 1.0f);
  const auto &tags = state.slicer->getTetVolume().vertextag;
  for (size_t i = 0; i < tags.size(); ++i) {
    if (tags[i] != static_cast<int>(zSpace::VertexTag::Top))
      continue;
    if (i >= graphs.size() || i >= valid.size() || i >= vertices.size() ||
        !valid[i])
      continue;
    drawTrace(state, scene, renderer, graphs[i], vertices[i].Position, color);
  }
}

void drawTrace(SlicerState &state, Scene &scene, Renderer &renderer,
               zSpace::zObjGraph &graph, const zSpace::zPoint &start,
               const Color &color) {

  zSpace::zFnGraph fn(graph);
  zSpace::zPoint end = start;
  if (fn.numVertices() > 0) {
    scene.draw(graph, Display::lines(color, 2.0f));
    zSpace::zPointArray positions;
    fn.getVertexPositions(positions);
    end = positions.back();
  }
  renderer.drawPoint(Vec3(static_cast<float>(end.x), static_cast<float>(end.y),
                          static_cast<float>(end.z)),
                     color, 6.0f);
}

void drawFieldPrintPaths(SlicerState &state, Scene &scene, Renderer &renderer) {

  const Color innerColor(0.1f, 0.35f, 0.95f, 1.0f);
  const Color outerColor(1.0f, 0.4f, 0.75f, 1.0f);
  for (const auto &segments : state.fieldPathSegments) {
    for (const SlicerState::PathSegment &seg : segments) {
      renderer.drawLine(seg.a, seg.b, seg.outer ? outerColor : innerColor,
                        4.0f);
    }
  }
}

void drawHarmonicFieldMinimap(SlicerState &state, Scene &scene,
                              Renderer &renderer) {

  if (!state.slicer)
    return;
  const auto &pathFields = state.slicer->getPathFields();
  const auto &widthFields = state.slicer->getWidthFields();
  if (pathFields.empty() || widthFields.empty())
    return;
  drawFieldMinimap(state, scene, renderer, state.slicer->getScalarField(),
                   pathFields[0], widthFields[0], "Path (layer 0)");
}

void drawTopologyFieldMinimap(SlicerState &state, Scene &scene,
                              Renderer &renderer) {

  if (!state.topoSlicer)
    return;
  const auto &pathFields = state.topoSlicer->getPathFields();
  const auto &widthFields = state.topoSlicer->getWidthFields();
  if (pathFields.empty() || widthFields.empty())
    return;
  drawFieldMinimap(state, scene, renderer, state.topoSlicer->getScalarField(),
                   pathFields[0], widthFields[0], "Path (layer 0)");
}

bool isLayerInspectorMode(SlicerState &state, Scene &scene) {

  return (state.slicerChoice == kSlicerHarmonic && state.slicer &&
          state.displayMode == kHarmonicModeCount - 1) ||
         (state.slicerChoice == kSlicerTopology && state.topoSlicer &&
          state.displayMode == kTopologyModeCount - 1);
}

zSpace::zSlicer *activeSlicer(SlicerState &state, Scene &scene) {

  if (state.slicerChoice == kSlicerHarmonic)
    return state.slicer.get();
  if (state.slicerChoice == kSlicerTopology)
    return state.topoSlicer.get();
  return nullptr;
}

int inspectorLayerCount(SlicerState &state, Scene &scene) {

  if (state.slicerChoice == kSlicerHarmonic && state.slicer)
    return static_cast<int>(state.slicer->getPathFields().size());
  if (state.slicerChoice == kSlicerTopology && state.topoSlicer)
    return static_cast<int>(state.topoSlicer->getPathFields().size());
  return 0;
}

void drawLayerInspector(SlicerState &state, Scene &scene, Renderer &renderer) {

  zSpace::zSlicer *slicer = activeSlicer(state, scene);
  const int count = inspectorLayerCount(state, scene);
  if (!slicer || count == 0)
    return;
  state.inspectLayer = (std::max)(0, (std::min)(count - 1, state.inspectLayer));
  const int l = state.inspectLayer;

  const auto &layers3D = slicer->getPrintPaths3D();
  const auto &layers2D = slicer->getPrintPaths2D();
  const Color innerColor(0.1f, 0.35f, 0.95f, 1.0f);
  const Color outerColor(1.0f, 0.4f, 0.75f, 1.0f);
  const auto toVec3 = [](const zSpace::zPoint &p) {
    return Vec3(static_cast<float>(p.x), static_cast<float>(p.y),
                static_cast<float>(p.z));
  };
  std::ostringstream info;
  info << "Layer " << l << " / " << (count - 1)
       << " (up/down arrow) | corners: yellow";
  if (l < static_cast<int>(layers3D.size())) {
    info << " | paths: " << layers3D[l].size();
    for (size_t k = 0; k < layers3D[l].size(); ++k) {
      const zSpace::PrintPath &path = layers3D[l][k];
      zSpace::zFnGraph fn(const_cast<zSpace::zObjGraph &>(path.graph));
      zSpace::zPointArray positions;
      zSpace::zIntArray edges;
      fn.getVertexPositions(positions);
      fn.getEdgeData(edges);
      if (positions.empty())
        continue;
      std::vector<int> degree(positions.size(), 0);
      for (size_t e = 0; e + 1 < edges.size(); e += 2) {
        const int a = edges[e], b = edges[e + 1];
        ++degree[a];
        ++degree[b];
        const bool outer = static_cast<size_t>(a) < path.trackIds.size() &&
                           static_cast<size_t>(b) < path.trackIds.size() &&
                           0.5f * (path.trackIds[a] + path.trackIds[b]) >= 0.5f;
        renderer.drawLine(toVec3(positions[a]), toVec3(positions[b]),
                          outer ? outerColor : innerColor, 4.0f);
      }
      // Detect in the 2D chart, so surface curvature does not create false
      // corners.
      if (l < static_cast<int>(layers2D.size()) && k < layers2D[l].size()) {
        const auto corners = zSpace::zPrintSyn::cornerVertexIds(
            layers2D[l][k], state.printSettings.FeatureAngleTol);
        for (int id : corners)
          if (id >= 0 && static_cast<size_t>(id) < positions.size())
            renderer.drawPoint(toVec3(positions[id]),
                               Color(1.0f, 0.85f, 0.0f, 1.0f), 9.0f);
      }
      int openEnds = 0, branches = 0;
      for (size_t v = 0; v < positions.size(); ++v) {
        if (degree[v] == 1) {
          ++openEnds;
          renderer.drawPoint(toVec3(positions[v]),
                             Color(0.95f, 0.1f, 0.1f, 1.0f), 10.0f);
        } else if (degree[v] > 2) {
          ++branches;
          renderer.drawPoint(toVec3(positions[v]),
                             Color(1.0f, 0.55f, 0.0f, 1.0f), 10.0f);
        }
      }
      renderer.drawPoint(toVec3(positions[0]), Color(0.1f, 0.75f, 0.2f, 1.0f),
                         12.0f);
      if (k == 0)
        info << " | first path: " << positions.size() << " vertices, "
             << edges.size() / 2 << " edges, " << openEnds << " open ends, "
             << branches << " branch vertices";
    }
  } else {
    info << " | no print paths on this layer";
  }

  const std::vector<zSpace::PrintPath> *paths2D =
      (l < static_cast<int>(layers2D.size())) ? &layers2D[l] : nullptr;
  const std::string label = "Path (layer " + std::to_string(l) + ")";
  if (state.slicerChoice == kSlicerHarmonic && state.slicer) {
    const auto &pathFields = state.slicer->getPathFields();
    const auto &widthFields = state.slicer->getWidthFields();
    if (l < static_cast<int>(pathFields.size()) &&
        l < static_cast<int>(widthFields.size()))
      drawFieldMinimap(state, scene, renderer, state.slicer->getScalarField(),
                       pathFields[l], widthFields[l], label.c_str(), paths2D);
  } else if (state.slicerChoice == kSlicerTopology && state.topoSlicer) {
    const auto &pathFields = state.topoSlicer->getPathFields();
    const auto &widthFields = state.topoSlicer->getWidthFields();
    if (l < static_cast<int>(pathFields.size()) &&
        l < static_cast<int>(widthFields.size()))
      drawFieldMinimap(state, scene, renderer,
                       state.topoSlicer->getScalarField(), pathFields[l],
                       widthFields[l], label.c_str(), paths2D);
  }

  renderer.setColor(Color(0.08f, 0.09f, 0.12f, 1.0f));
  renderer.drawString(info.str(), 10, 68);
  renderer.drawString("Slicer status: " + slicer->getStatus(), 10, 86);
}

void drawFieldMinimap(SlicerState &state, Scene &scene, Renderer &renderer,
                      const zSpace::zObjMeshScalarField &field,
                      const zSpace::zScalarArray &pathValues,
                      const zSpace::zScalarArray &widthValues,
                      const char *pathLabel,
                      const std::vector<zSpace::PrintPath> *paths2D) {

  zSpace::zFnMesh fn(const_cast<zSpace::zObjMeshScalarField &>(field));
  zSpace::zPointArray positions;
  fn.getVertexPositions(positions);
  zSpace::zIntArray polyConnects;
  zSpace::zIntArray polyCounts;
  fn.getPolygonData(polyConnects, polyCounts);
  if (positions.empty() || polyCounts.empty())
    return;

  zSpace::zPoint minBB, maxBB;
  fn.getBounds(minBB, maxBB);
  const double rangeX = (maxBB.x - minBB.x > 1e-9) ? (maxBB.x - minBB.x) : 1.0;
  const double rangeY = (maxBB.y - minBB.y > 1e-9) ? (maxBB.y - minBB.y) : 1.0;

  zSpace::zColorArray pathColors;
  zSpace::zTsUtils::colorPathField(pathValues, pathColors);
  zSpace::zColorArray widthColors;
  // Same palette as the input face groups (buildInputGroups(state, scene)), so
  // each width matches its side_<n> color even on a layer missing some groups.
  zSpace::zTsUtils::colorWidthField(
      widthValues, widthColors,
      state.widthPalette.empty() ? nullptr : &state.widthPalette);

  GLint viewport[4];
  glGetIntegerv(GL_VIEWPORT, viewport);

  constexpr float kPanelSize = 440.0f; // longer side of each panel
  constexpr float kMargin =
      100.0f; // all HUDs use a 100px margin from the window edge
  constexpr float kGap =
      24.0f; // leaves room for the path label between the two panels
  // Same scale on both axes, so the field shows at its real proportions.
  const float scale =
      kPanelSize / static_cast<float>((std::max)(rangeX, rangeY));
  const float panelW = static_cast<float>(rangeX) * scale;
  const float panelH = static_cast<float>(rangeY) * scale;
  const float right = static_cast<float>(viewport[2]) - kMargin;
  const float left = right - panelW;
  const float pathTop = kMargin;
  const float pathBottom = pathTop + panelH;
  const float widthTop = pathBottom + kGap;
  const float widthBottom = widthTop + panelH;

  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  glOrtho(0, viewport[2], viewport[3], 0, -1, 1);
  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();

  const auto drawPanel = [&](float top, float bottom,
                             const zSpace::zColorArray &colors) {
    if (colors.size() != positions.size())
      return;
    const auto toScreen = [&](const zSpace::zPoint &p) {
      const float sx =
          left + static_cast<float>((p.x - minBB.x) / rangeX) * (right - left);
      const float sy = bottom - static_cast<float>((p.y - minBB.y) / rangeY) *
                                    (bottom - top);
      return std::make_pair(sx, sy);
    };

    int offset = 0;
    for (int count : polyCounts) {
      if (count != 4) {
        offset += count;
        continue;
      } // quad grid (triMesh=false)
      const int ia = polyConnects[offset + 0];
      const int ib = polyConnects[offset + 1];
      const int ic = polyConnects[offset + 2];
      const int id = polyConnects[offset + 3];
      offset += count;
      if (ia < 0 || ib < 0 || ic < 0 || id < 0 ||
          static_cast<size_t>(ia) >= positions.size() ||
          static_cast<size_t>(ib) >= positions.size() ||
          static_cast<size_t>(ic) >= positions.size() ||
          static_cast<size_t>(id) >= positions.size())
        continue;

      const auto va = toScreen(positions[ia]);
      const auto vb = toScreen(positions[ib]);
      const auto vc = toScreen(positions[ic]);
      const auto vd = toScreen(positions[id]);

      glBegin(GL_QUADS);
      glColor4f(colors[ia].r, colors[ia].g, colors[ia].b, colors[ia].a);
      glVertex2f(va.first, va.second);
      glColor4f(colors[ib].r, colors[ib].g, colors[ib].b, colors[ib].a);
      glVertex2f(vb.first, vb.second);
      glColor4f(colors[ic].r, colors[ic].g, colors[ic].b, colors[ic].a);
      glVertex2f(vc.first, vc.second);
      glColor4f(colors[id].r, colors[id].g, colors[id].b, colors[id].a);
      glVertex2f(vd.first, vd.second);
      glEnd();
    }

    if (paths2D) {
      glLineWidth(2.0f);
      for (const zSpace::PrintPath &path : *paths2D) {
        zSpace::zFnGraph fnPath(const_cast<zSpace::zObjGraph &>(path.graph));
        zSpace::zPointArray pathPoints;
        zSpace::zIntArray pathEdges;
        fnPath.getVertexPositions(pathPoints);
        fnPath.getEdgeData(pathEdges);
        glBegin(GL_LINES);
        for (size_t e = 0; e + 1 < pathEdges.size(); e += 2) {
          const int a = pathEdges[e], b = pathEdges[e + 1];
          const bool outer =
              static_cast<size_t>(a) < path.trackIds.size() &&
              static_cast<size_t>(b) < path.trackIds.size() &&
              0.5f * (path.trackIds[a] + path.trackIds[b]) >= 0.5f;
          if (outer)
            glColor4f(1.0f, 0.4f, 0.75f, 1.0f);
          else
            glColor4f(0.1f, 0.35f, 0.95f, 1.0f);
          const auto pa = toScreen(pathPoints[a]);
          const auto pb = toScreen(pathPoints[b]);
          glVertex2f(pa.first, pa.second);
          glVertex2f(pb.first, pb.second);
        }
        glEnd();
        const auto corners = zSpace::zPrintSyn::cornerVertexIds(
            path, state.printSettings.FeatureAngleTol);
        glPointSize(7.0f);
        glColor4f(1.0f, 0.85f, 0.0f, 1.0f);
        glBegin(GL_POINTS);
        for (int id : corners) {
          if (id < 0 || static_cast<size_t>(id) >= pathPoints.size())
            continue;
          const auto point = toScreen(pathPoints[id]);
          glVertex2f(point.first, point.second);
        }
        glEnd();
        glPointSize(1.0f);
        if (!pathPoints.empty()) { // start vertex
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

  drawPanel(pathTop, pathBottom, pathColors);
  drawPanel(widthTop, widthBottom, widthColors);

  glPopMatrix();
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);

  renderer.setColor(Color(0.05f, 0.05f, 0.05f, 1.0f));
  renderer.drawString(pathLabel, left, pathBottom + 16.0f);
  renderer.drawString("Width", left, widthBottom + 16.0f);
}

void drawTetWireframe(SlicerState &state, Scene &scene) {

  const zDisplayGraphSetting tetLines =
      Display::lines(Color(0.15f, 0.35f, 0.85f, 1.0f), 1.0f);
  scene.draw(state.tetWireGraph, tetLines);
}

void drawHarmonicIsoUV(SlicerState &state, Scene &scene) {

  zDisplayMeshSetting uvCheck;
  uvCheck.showVertices = false;
  uvCheck.showEdges = false;
  uvCheck.showFaces = true;
  uvCheck.useMeshColors = true;
  for (zSpace::zObjMesh &m : state.slicer->getIsoMeshes())
    scene.draw(m, uvCheck);
}

void rebuildTetWireGraph(SlicerState &state, Scene &scene) {

  zSpace::zFnGraph clearFn(state.tetWireGraph);
  clearFn.clear();

  const zSpace::zTetrahedron &tet = state.slicer->getTetVolume();
  if (tet.Volumes.empty())
    return;

  static constexpr int tetEdges[6][2] = {{0, 1}, {0, 2}, {0, 3},
                                         {1, 2}, {1, 3}, {2, 3}};
  std::set<std::pair<int, int>> uniqueEdges;
  for (const auto &volume : tet.Volumes) {
    const auto &t = volume.VertexIds;
    for (const auto &e : tetEdges) {
      const int a = t[e[0]];
      const int b = t[e[1]];
      uniqueEdges.insert(a < b ? std::make_pair(a, b) : std::make_pair(b, a));
    }
  }

  zSpace::zIntArray edgeConnects;
  edgeConnects.reserve(uniqueEdges.size() * 2);
  for (const auto &e : uniqueEdges) {
    edgeConnects.push_back(e.first);
    edgeConnects.push_back(e.second);
  }

  zSpace::zPointArray positions;
  positions.reserve(tet.Vertices.size());
  for (const auto &vertex : tet.Vertices)
    positions.push_back(vertex.Position);
  zSpace::zFnGraph fn(state.tetWireGraph);
  fn.create(positions, edgeConnects);
}

void colorHarmonicIsoByUV(SlicerState &state, Scene &scene) {

  const std::vector<zSpace::zPointArray> &uvBySlice =
      state.slicer->getIsoMeshUV();
  zSpace::zObjMeshArray &meshes = state.slicer->getIsoMeshes();
  if (uvBySlice.empty() || meshes.empty())
    return;

  for (size_t k = 0; k < meshes.size() && k < uvBySlice.size(); ++k) {
    const zSpace::zPointArray &uv = uvBySlice[k];
    if (uv.empty())
      continue;

    zSpace::zColorArray colors;
    colors.reserve(uv.size());
    for (const zSpace::zPoint &p : uv) {
      colors.emplace_back(p.x, p.y, 1.0f - p.x * p.y, 1.0f);
    }

    zSpace::zFnMesh fn(meshes[k]);
    if (fn.numVertices() == static_cast<int>(colors.size()))
      fn.setVertexColors(colors, false);
  }
}

void drawUVGradientLegend(SlicerState &state, Scene &scene,
                          Renderer &renderer) {

  GLint viewport[4];
  glGetIntegerv(GL_VIEWPORT, viewport);

  constexpr float kSize = 200.0f; // 2x the original 100
  constexpr float kMargin =
      100.0f; // all HUDs use a 100px margin from the window edge
  const float left = static_cast<float>(viewport[2]) - kMargin - kSize;
  const float right = static_cast<float>(viewport[2]) - kMargin;
  const float top = kMargin; // smaller screen Y = top of screen
  const float bottom = kMargin + kSize;

  const auto uvColor = [](float u, float v) {
    return Color(u, v, 1.0f - u * v, 1.0f);
  };
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
  glColor4f(colorU0V0.r, colorU0V0.g, colorU0V0.b, colorU0V0.a);
  glVertex2f(left, bottom); // (0,0)
  glColor4f(colorU1V0.r, colorU1V0.g, colorU1V0.b, colorU1V0.a);
  glVertex2f(right, bottom); // (1,0)
  glColor4f(colorU1V1.r, colorU1V1.g, colorU1V1.b, colorU1V1.a);
  glVertex2f(right, top); // (1,1)
  glColor4f(colorU0V1.r, colorU0V1.g, colorU0V1.b, colorU0V1.a);
  glVertex2f(left, top); // (0,1)
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
} // namespace alice2::slicer

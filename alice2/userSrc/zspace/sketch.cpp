// #define __MAIN__ // disabled: sketch_harmonic_slicer_compile_check.cpp is the sole active sketch for now
#ifdef __MAIN__

#include <headers/zToolsets/zprototype/zTsPrototype.h>

// clang-format off
#include <windows.h>
#include <commdlg.h>
// clang-format on

#include <alice2.h>
#include <sketches/SketchRegistry.h>

#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace alice2;

namespace {

// Native Windows "Save File" dialog. Returns true and fills outPath if the
// user picked a location; returns false (outPath untouched) if they
// cancelled.
inline bool saveFileDialog(const char *filter, const char *title,
                           const char *defExt, std::string &outPath) {
  char fileBuffer[MAX_PATH] = {0};
  OPENFILENAMEA ofn = {};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = GetActiveWindow();
  ofn.lpstrFilter = filter;
  ofn.lpstrFile = fileBuffer;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrTitle = title;
  ofn.lpstrDefExt = defExt;
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

  if (GetSaveFileNameA(&ofn)) {
    outPath = fileBuffer;
    return true;
  }
  return false;
}

// Native Windows "Open File" dialog. Returns true and fills outPath if the
// user picked an existing file; returns false (outPath untouched) if they
// cancelled.
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

// Reads a graph straight out of an OBJ file's own "v" (vertex) and "l"
// (line/polyline) elements - "v x y z" for each vertex, "l i1 i2 i3 ..."
// for each polyline (each becomes i1-i2, i2-i3, ... edges). OBJ indices
// are 1-based, and a negative index counts back from the end of the
// vertex list so far, same as OBJ's own convention. Ignores every other
// element (vt/vn/f/...) - a "graph" export just needs points and lines.
// Returns false if the file couldn't be opened, or no usable vertex+edge
// data was found.
//
// Most tools export each polyline as its OWN "l" element with its own
// vertex indices, even where two polylines physically meet - e.g. the top
// chord and each column-drop line typically do NOT share a vertex index
// at their junction, just its position. zTsPrototype's computePiers finds
// each pier by walking actual graph EDGES (not just position), so without
// welding, a junction split across two unconnected vertices is invisible
// to it - no pier gets detected there at all. So vertices within
// weldEpsilon of each other are merged into one id here, before edges are
// recorded, regardless of which "l" element they came from.
inline bool loadGraphFromOBJFile(const std::string &path,
                                 zSpace::zPointArray &outPositions,
                                 zSpace::zIntArray &outEdgeConnects) {
  std::ifstream ifs(path);
  if (!ifs.is_open())
    return false;

  zSpace::zPointArray rawPositions;
  std::vector<std::pair<int, int>> rawEdges;

  std::string line;
  while (std::getline(ifs, line)) {
    std::istringstream iss(line);
    std::string tag;
    iss >> tag;

    if (tag == "v") {
      float x, y, z;
      if (iss >> x >> y >> z)
        rawPositions.push_back(zSpace::zPoint(x, y, z));
    } else if (tag == "l") {
      std::vector<int> indices;
      int idx;
      while (iss >> idx) {
        if (idx < 0)
          idx = int(rawPositions.size()) + idx + 1;
        indices.push_back(idx - 1);
      }
      for (size_t k = 0; k + 1 < indices.size(); k++)
        rawEdges.push_back(std::make_pair(indices[k], indices[k + 1]));
    }
  }

  if (rawPositions.empty() || rawEdges.empty())
    return false;

  const float weldEpsilon = 1e-4f;
  std::vector<int> weldMap(rawPositions.size(), -1);
  outPositions.clear();

  for (size_t i = 0; i < rawPositions.size(); i++) {
    int foundId = -1;
    for (size_t j = 0; j < outPositions.size(); j++) {
      float dx = rawPositions[i].x - outPositions[j].x;
      float dy = rawPositions[i].y - outPositions[j].y;
      float dz = rawPositions[i].z - outPositions[j].z;
      if (dx * dx + dy * dy + dz * dz < weldEpsilon * weldEpsilon) {
        foundId = int(j);
        break;
      }
    }
    if (foundId < 0) {
      foundId = int(outPositions.size());
      outPositions.push_back(rawPositions[i]);
    }
    weldMap[i] = foundId;
  }

  outEdgeConnects.clear();
  for (const auto &e : rawEdges) {
    outEdgeConnects.push_back(weldMap[e.first]);
    outEdgeConnects.push_back(weldMap[e.second]);
  }

  return !outPositions.empty() && !outEdgeConnects.empty();
}

} // namespace

class zPrototypeSketch : public ISketch {
public:
  std::string getName() const override { return "zTsPrototype Arch Bridge"; }
  std::string getDescription() const override {
    return "Generates the zTsPrototype arch-bridge mesh topology from an "
           "input graph.";
  }
  std::string getAuthor() const override { return "zSpace Prototype Toolset"; }
  std::string getVersion() const override { return "1.0.0"; }

  void setup() override {
    scene().setBackgroundColor(Color(1.0f, 1.0f, 1.0f, 1.0f));
    scene().setShowGrid(false);
    scene().setGridSize(8.0f);
    scene().setGridDivisions(8);
    scene().setShowAxes(false);
    scene().setAxesLength(1.5f);

    // Width/Height/pier count+positions are no longer separate scalar
    // inputs - zTsPrototype auto-detects them (and the bridge's own plane)
    // entirely from an input "comb" graph (zTsPrototype::i_Graph), imported
    // via 'i' (see importGraphViaDialog). There's no UI-built fallback
    // graph any more - nothing computes until a graph is imported.

    // Every other field of PrototypeSettings itself defaults to 0, so it
    // must be set explicitly here. Each arch's own rise is no longer a
    // single global height - it's computed per span as that span's
    // horizontal radius * ArchHeightCoefficient (k), so wider spans crest
    // higher.
    zSpace::PrototypeSettings settings;
    settings.ColumnHeight = 2.0f;
    settings.ArchHeightCoefficient = 0.5f;
    settings.ColumnWidth = 0.3f;
    settings.BlockSize = 0.35f;

    // Per-vertex thickness (along the bridge's own plane normal): t0 at
    // the beam top, t1 at every arch crown, t2 at every spring (also the
    // column's own top), t3 at the column base - the two offset meshes
    // place each vertex at +thickness/2 and -thickness/2 along that normal.
    settings.BeamTopOffset = 0.5f;      // t0
    settings.ArchTopOffset = 0.3f;      // t1
    settings.ArchBottomOffset = 0.3f;   // t2
    settings.ColumnBottomOffset = 0.3f; // t3

    // Marks the beam's bottom edge as a crease, Catmull-Clark subdivides
    // the two offset meshes SubdivisionCount times keeping it sharp, splits
    // along it into independent face groups and bridges each group's
    // positive/negative copies into a closed solid (see getClosedMeshes()).
    settings.SubdivisionCount = 0;

    // Kept as a reference for importGraphViaDialog(): all the settings
    // above are absolute units, tuned for a bridge of this height
    // (kBaseHeight) - an imported graph authored at a very different scale
    // needs them rescaled proportionally, or e.g. ColumnHeight alone can
    // swallow the whole (much smaller) beam headroom and fail validation
    // outright.
    m_baseSettings = settings;
    m_baseHeight = kBaseHeight;

    m_prototype.setSettings(settings);

    auto &camController = Application::getInstance()->getCameraController();
    camController.setOrbitCenter(Vec3(0.0f, 0.0f, 0.0f));
    camController.setOrbitDistance(kBaseHeight * 2.0f);

    setupUI();
    snapshotSettings();
  }

  void update(float /*deltaTime*/) override {
    // Sliders write straight into m_prototype's settings via bound
    // pointers, so there's no per-key callback - detect a change by
    // comparing against the last-known snapshot and recompute on the fly.
    if (settingsChangedSinceSnapshot()) {
      recompute();
      snapshotSettings();
    }
  }

  void draw(Renderer &renderer, Camera & /*camera*/) override {
    if (m_viewMode == ViewMode::ClosedMeshes) {
      // computeBlock() already bakes a distinct colour per original-face id
      // into each o_ClosedMeshes entry via setFaceColor(..., true) - just
      // read those back instead of re-deriving a colour from array index.
      for (int i = 0; i < m_prototype.getClosedMeshCount(); i++) {
        zDisplayMeshSetting closedDisplay;
        closedDisplay.showVertices = false;
        closedDisplay.showEdges = true;
        closedDisplay.showFaces = true;
        closedDisplay.useMeshColors = true;
        closedDisplay.edgeColor = Color(0.05f, 0.05f, 0.05f, 1.0f);
        closedDisplay.edgeWidth = 1.0f;
        scene().draw(m_prototype.getClosedMesh(i), closedDisplay);
      }
    } else if (m_viewMode == ViewMode::InputGraph) {
      // The raw graph zTsPrototype's compute() reads Width/Height/plane/
      // piers back out of - drawn straight from m_importedGraphPositions/
      // m_importedGraphEdgeConnects, not the computed mesh, so this stays
      // meaningful even when compute() rejected the graph. Bold black
      // edges, bold pink points, per the requested look.
      for (size_t e = 0; e + 1 < m_importedGraphEdgeConnects.size(); e += 2) {
        const zSpace::zPoint &a =
            m_importedGraphPositions[m_importedGraphEdgeConnects[e]];
        const zSpace::zPoint &b =
            m_importedGraphPositions[m_importedGraphEdgeConnects[e + 1]];
        renderer.drawLine(Vec3(a.x, a.y, a.z), Vec3(b.x, b.y, b.z),
                          Color(0.0f, 0.0f, 0.0f, 1.0f), 3.0f);
      }
      for (const zSpace::zPoint &p : m_importedGraphPositions) {
        renderer.drawPoint(Vec3(p.x, p.y, p.z), Color(1.0f, 0.4f, 0.7f, 1.0f),
                           10.0f);
      }
    } else {
      // getFaceBlocks() splits the flat mesh into one small mesh per
      // zTsPrototype::_PanelGroupId id, each already uniformly coloured
      // (computeFaceBlocks, called every recompute) - drawn as separate
      // meshes so no vertex is shared between two different ids, unlike
      // colouring o_MeshPositive face-by-face, where a boundary vertex's
      // single colour slot gets interpolated across both ids and blurs
      // the seam between them.
      for (int i = 0; i < m_prototype.getFaceBlockCount(); i++) {
        zDisplayMeshSetting blockDisplay;
        blockDisplay.showVertices = false;
        blockDisplay.showEdges = true;
        blockDisplay.showFaces = true;
        blockDisplay.useMeshColors = true;
        blockDisplay.edgeColor = Color(0.05f, 0.05f, 0.05f, 1.0f);
        blockDisplay.edgeWidth = 1.5f;
        scene().draw(m_prototype.getFaceBlock(i), blockDisplay);
      }
    }

    if (m_showFlatMesh) {
      zDisplayMeshSetting flatDisplay;
      flatDisplay.showVertices = false;
      flatDisplay.showEdges = true;
      flatDisplay.showFaces = false;
      flatDisplay.edgeColor = Color(0.85f, 0.15f, 0.15f, 0.6f);
      flatDisplay.edgeWidth = 1.0f;
      scene().draw(m_prototype.getMesh(), flatDisplay);
    }

    renderer.setColor(Color(0.0f, 0.0f, 0.0f));
    float y = 28.0f;
    const float lineHeight = 20.0f;
    auto drawLine = [&](const std::string &text) {
      renderer.drawString(text, 10, y);
      y += lineHeight;
    };

    drawLine(getName());
    drawLine("Status: " + m_prototype.getStatus());

    const zSpace::PrototypeSettings &s = m_prototype.getSettings();

    drawLine("Piers=" + std::to_string(m_prototype.getPierCount()) +
             " Spans=" + std::to_string(m_prototype.getSpanCount()));

    zSpace::zFnMesh fnMesh(m_prototype.getMesh());
    drawLine("Mesh: V=" + std::to_string(fnMesh.numVertices()) +
             " E=" + std::to_string(fnMesh.numEdges()) +
             " F=" + std::to_string(fnMesh.numPolygons()));

    std::string closedStr =
        "SubdivisionCount=" + std::to_string(s.SubdivisionCount) +
        "  ClosedMeshes=" + std::to_string(m_prototype.getClosedMeshCount());
    drawLine(closedStr);

    drawLine("'f' focus   'm' toggle flat mesh outline   'k' cycle "
             "face-blocks/closed-mesh/input-graph view   'e' export "
             "current view to OBJ   'i' import graph from OBJ");

    // Sliders are drawn last so they sit on top of the text/help lines.
    if (m_ui)
      m_ui->draw(renderer);
  }

  bool onKeyPress(unsigned char key, int, int) override {
    switch (key) {
    case 'f':
    case 'F':
      focusOnMesh();
      return true;
    case 'm':
    case 'M':
      m_showFlatMesh = !m_showFlatMesh;
      return true;
    case 'k':
    case 'K':
      m_viewMode =
          static_cast<ViewMode>((static_cast<int>(m_viewMode) + 1) %
                                (static_cast<int>(ViewMode::InputGraph) + 1));
      return true;
    case 'e':
    case 'E':
      exportCurrentViewViaDialog();
      return true;
    case 'i':
    case 'I':
      importGraphViaDialog();
      return true;
    default:
      return false;
    }
  }

  bool onMousePress(int button, int state, int x, int y) override {
    if (m_ui && m_ui->onMousePress(button, state, x, y))
      return true;
    return false;
  }

  bool onMouseMove(int x, int y) override {
    if (m_ui && m_ui->onMouseMove(x, y))
      return true;
    return false;
  }

private:
  zSpace::zTsPrototype m_prototype;
  std::unique_ptr<SimpleUI> m_ui;
  bool m_showFlatMesh = false;

  // 'k' cycles through these three views. computeBlock() (closed solids) is
  // temporarily disabled in compute(), so o_ClosedMeshes is always empty
  // right now - ClosedMeshes will show nothing until it's re-enabled.
  enum class ViewMode { FaceBlocks, ClosedMeshes, InputGraph };
  ViewMode m_viewMode = ViewMode::FaceBlocks;

  // The absolute height setup() tunes ColumnHeight/ColumnWidth/BlockSize/
  // the thickness offsets for - see m_baseSettings/m_baseHeight below.
  static constexpr float kBaseHeight = 3.0f;

  // Snapshot of every slider-bound value, used purely to detect drag edits
  // and trigger a recompute (sliders write straight into these fields /
  // m_prototype's settings, there's no per-change callback).
  zSpace::PrototypeSettings m_lastSettings;

  // Set by importGraphViaDialog() ('i' key) - once true, recompute() uses
  // m_importedGraph* instead of building one from the Width/Height/
  // ColumnPosition sliders (those keep working underneath, in case the
  // user wants to switch back to the UI-built graph, but nothing currently
  // does - there's no 'i'-toggle-off yet, only re-importing another file).
  bool m_hasImportedGraph = false;
  zSpace::zPointArray m_importedGraphPositions;
  zSpace::zIntArray m_importedGraphEdgeConnects;

  // Snapshot of the settings/Height setup() configured, kept purely as a
  // rescale reference for importGraphViaDialog() - see there.
  zSpace::PrototypeSettings m_baseSettings;
  float m_baseHeight = 0.0f;

  void setupUI() {
    zSpace::PrototypeSettings &s = m_prototype.getSettings();
    m_ui = std::make_unique<SimpleUI>(input());
    // SimpleUI::UITheme::Dark is (despite the name) the palette whose text
    // colour is black - SimpleUI defaults to Light (white text), which is
    // invisible against this sketch's white background.
    m_ui->setTheme(SimpleUI::UITheme::Dark);

    const float panelX = 10.0f;
    float y = 150.0f;
    const float rowH = 34.0f;
    const float w = 220.0f;

    auto floatSlider = [&](const std::string &label, float minV, float maxV,
                           float &bound) {
      m_ui->addSlider(label, Vec2{panelX, y}, w, minV, maxV, bound, 0.1f);
      y += rowH;
    };
    auto intSlider = [&](const std::string &label, int minV, int maxV,
                         int &bound) {
      m_ui->addSliderInt(label, Vec2{panelX, y}, w, minV, maxV, bound);
      y += rowH;
    };

    floatSlider("ColumnHeight", 0.1f, 8.0f, s.ColumnHeight);
    floatSlider("ArchHeightCoefficient", 0.05f, 2.0f, s.ArchHeightCoefficient);
    floatSlider("ColumnWidth", 0.1f, 3.0f, s.ColumnWidth);
    floatSlider("BlockSize", 0.05f, 3.0f, s.BlockSize);

    intSlider("SubdivisionCount", 0, 4, s.SubdivisionCount);

    floatSlider("BeamTopOffset (t0)", 0.0f, 3.0f, s.BeamTopOffset);
    floatSlider("ArchTopOffset (t1)", 0.0f, 3.0f, s.ArchTopOffset);
    floatSlider("ArchBottomOffset (t2)", 0.0f, 3.0f, s.ArchBottomOffset);
    floatSlider("ColumnBottomOffset (t3)", 0.0f, 3.0f, s.ColumnBottomOffset);
  }

  void snapshotSettings() { m_lastSettings = m_prototype.getSettings(); }

  bool settingsChangedSinceSnapshot() const {
    const zSpace::PrototypeSettings &s = m_prototype.getSettings();
    const zSpace::PrototypeSettings &p = m_lastSettings;

    if (s.ColumnHeight != p.ColumnHeight ||
        s.ArchHeightCoefficient != p.ArchHeightCoefficient ||
        s.ColumnWidth != p.ColumnWidth || s.BlockSize != p.BlockSize ||
        s.SubdivisionCount != p.SubdivisionCount ||
        s.BeamTopOffset != p.BeamTopOffset ||
        s.ArchTopOffset != p.ArchTopOffset ||
        s.ArchBottomOffset != p.ArchBottomOffset ||
        s.ColumnBottomOffset != p.ColumnBottomOffset)
      return true;

    return false;
  }

  void recompute() {
    // Nothing to compute until a graph has actually been imported (see
    // importGraphViaDialog / 'i') - there's no UI-built fallback graph any
    // more.
    if (!m_hasImportedGraph)
      return;

    zSpace::zFnGraph fnGraph(m_prototype.i_Graph);
    fnGraph.create(m_importedGraphPositions, m_importedGraphEdgeConnects);

    // computeFaceBlocks() (inside compute()) already builds and colours
    // getFaceBlocks() - nothing left to do here.
    if (!m_prototype.compute()) {
      std::cout << "[zTsPrototype] compute() failed: "
                << m_prototype.getStatus() << std::endl;
    }
  }

  // Opens a native "Open File" dialog for an OBJ, parses its own "v"/"l"
  // elements into a graph (loadGraphFromOBJFile), and switches recompute()
  // over to using it (see m_hasImportedGraph) instead of the Width/Height/
  // ColumnPosition sliders' built-in comb graph.
  void importGraphViaDialog() {
    const char *filter = "Wavefront OBJ (*.obj)\0*.obj\0All Files (*.*)\0*.*\0";
    std::string path;
    if (!openFileDialog(filter, "Import Graph from OBJ", path))
      return;

    zSpace::zPointArray positions;
    zSpace::zIntArray edgeConnects;
    if (!loadGraphFromOBJFile(path, positions, edgeConnects)) {
      std::cout << "[zTsPrototype] Import failed (no usable v/l data, or "
                   "could not open file): "
                << path << std::endl;
      return;
    }

    m_importedGraphPositions = positions;
    m_importedGraphEdgeConnects = edgeConnects;
    m_hasImportedGraph = true;

    recompute();

    // computePiers() always runs (and sets getHeight()) even when
    // compute() goes on to reject the result, so this catches the most
    // likely failure mode for a graph authored at a different scale from
    // m_baseSettings: ColumnHeight (absolute units, tuned for m_baseHeight)
    // leaving zero or negative headroom under a much smaller (or larger)
    // imported Height. Rescale every absolute-unit setting proportionally
    // FROM m_baseSettings (not from whatever the current, possibly
    // already-rescaled-by-a-previous-import settings are, so repeated
    // imports at different scales don't compound drift) and retry once.
    if (m_prototype.getStatus() != "Ready" && m_baseHeight > 0.0f &&
        m_prototype.getHeight() > 0.0f) {
      float scale = m_prototype.getHeight() / m_baseHeight;

      zSpace::PrototypeSettings &s = m_prototype.getSettings();
      s.ColumnHeight = m_baseSettings.ColumnHeight * scale;
      s.ColumnWidth = m_baseSettings.ColumnWidth * scale;
      s.BlockSize = m_baseSettings.BlockSize * scale;
      s.BeamTopOffset = m_baseSettings.BeamTopOffset * scale;
      s.ArchTopOffset = m_baseSettings.ArchTopOffset * scale;
      s.ArchBottomOffset = m_baseSettings.ArchBottomOffset * scale;
      s.ColumnBottomOffset = m_baseSettings.ColumnBottomOffset * scale;

      std::cout << "[zTsPrototype] compute() rejected the imported graph "
                   "at its original scale ("
                << m_prototype.getStatus()
                << ") - rescaling ColumnHeight/ColumnWidth/BlockSize/"
                   "offsets by "
                << scale << "x and retrying." << std::endl;

      recompute();
    }

    snapshotSettings();
    focusOnGraphPlane();

    std::cout << "[zTsPrototype] Imported graph (" << positions.size()
              << " vertices, " << edgeConnects.size() / 2 << " edges) from "
              << path << std::endl;

    // What zTsPrototype itself actually made of that graph - Width/Height
    // and every pier it recognised (in the bridge's own local plane x, so
    // 0 = the left phantom edge, Width = the right one) - independent of
    // whether compute() succeeded, so a rejected/degenerate graph's
    // "0 real piers" is visible here too, not just as a status string.
    std::cout << "[zTsPrototype] Detected Width=" << m_prototype.getWidth()
              << " Height=" << m_prototype.getHeight()
              << " Piers=" << m_prototype.getPierCount() << " [";
    for (int i = 0; i < m_prototype.getPierCount(); i++) {
      const zSpace::ColumnPier &pier = m_prototype.getPier(i);
      if (i > 0)
        std::cout << ", ";
      std::cout << (pier.isPhantom ? "phantom" : "real") << "@" << pier.center;
    }
    std::cout << "]" << std::endl;
  }

  void focusOnMesh() {
    zSpace::zFnMesh fnMesh(m_prototype.getMesh());
    if (fnMesh.numVertices() == 0)
      return;

    zSpace::zPoint minBB, maxBB;
    fnMesh.getBounds(minBB, maxBB);

    Vec3 center(static_cast<float>((minBB.x + maxBB.x) * 0.5),
                static_cast<float>((minBB.y + maxBB.y) * 0.5),
                static_cast<float>((minBB.z + maxBB.z) * 0.5));

    float maxDim = static_cast<float>(
        std::max({maxBB.x - minBB.x, maxBB.y - minBB.y, maxBB.z - minBB.z}));

    auto &camController = Application::getInstance()->getCameraController();
    camController.setOrbitCenter(center);
    camController.setOrbitDistance(maxDim * 1.5f);
  }

  // Points the camera straight at the input graph's own plane, head-on
  // along its normal, centred on that plane's own bounding
  // box (Width x Height, with the origin at its own bottom-left corner -
  // see zTsPrototype::computePlane) - unlike focusOnMesh() (which only
  // recentres/redistances, keeping whatever viewing ANGLE the camera
  // already had), this forces the orientation itself, so an import always
  // starts from a consistent, plane-facing view regardless of where the
  // camera was left pointing before.
  void focusOnGraphPlane() {
    float w = m_prototype.getWidth();
    float h = m_prototype.getHeight();
    if (w <= 0.0f || h <= 0.0f)
      return;

    // zPoint's operator* isn't const-qualified, so these need to be plain
    // values (not const refs) to be usable on the right of a '*' below.
    zSpace::zPoint po = m_prototype.getPlaneOrigin();
    zSpace::zPoint px = m_prototype.getPlaneXAxis();
    zSpace::zPoint py = m_prototype.getPlaneYAxis();
    zSpace::zPoint pn = m_prototype.getPlaneNormal();

    zSpace::zPoint c = po + px * (w * 0.5f) + py * (h * 0.5f);
    Vec3 center(static_cast<float>(c.x), static_cast<float>(c.y),
                static_cast<float>(c.z));
    Vec3 normal(static_cast<float>(pn.x), static_cast<float>(pn.y),
                static_cast<float>(pn.z));
    Vec3 up(static_cast<float>(py.x), static_cast<float>(py.y),
            static_cast<float>(py.z));

    float distance = std::max(w, h) * 1.5f;

    auto &camController = Application::getInstance()->getCameraController();
    CameraState state = camController.getCurrentCameraState();
    state.mode = CameraMode::Orbit;
    state.orbitCenter = center;
    state.orbitDistance = distance;
    state.position = center + normal * distance;
    // Eigen also declares a (template) Quaternion pulled in via the zSpace
    // headers, so an unqualified "Quaternion" here is ambiguous - alice2's
    // own type must be named explicitly.
    state.rotation = alice2::Quaternion::lookAt((-normal).normalized(), up);
    camController.setCameraState(state);
  }

  // Writes every mesh returned by getMesh(0..count-1) to one multi-object
  // OBJ file, one "o"/"g" group per mesh, with its baked-in per-vertex
  // colour (setFaceColor(..., true), from computeBlock or
  // computeFaceBlocks) as the "v x y z r g b" extension zTsUtils' own
  // coloured-mesh exporter uses.
  bool exportMeshesToOBJ(int count,
                         const std::function<zSpace::zObjMesh &(int)> &getMesh,
                         const std::string &filePath) {
    if (count == 0)
      return false;

    std::ofstream ofs(filePath);
    if (!ofs.is_open())
      return false;

    ofs << "# zTsPrototype - one object per mesh, vertex colours (v x y z "
           "r g b)\n";
    size_t globalVertexBase = 1;

    for (int i = 0; i < count; i++) {
      zSpace::zFnMesh fnMesh(getMesh(i));

      zSpace::zPointArray positions;
      fnMesh.getVertexPositions(positions);
      if (positions.empty())
        continue;

      zSpace::zColor *colors = fnMesh.getRawVertexColors();

      zSpace::zIntArray polyConnects, polyCounts;
      fnMesh.getPolygonData(polyConnects, polyCounts);

      ofs << "o group_" << i << "\n";
      ofs << "g group_" << i << "\n";

      for (size_t v = 0; v < positions.size(); v++) {
        const auto &p = positions[v];
        ofs << "v " << p.x << " " << p.y << " " << p.z;
        if (colors) {
          const auto &c = colors[v];
          ofs << " " << c.r << " " << c.g << " " << c.b;
        }
        ofs << "\n";
      }

      size_t offset = 0;
      for (int n : polyCounts) {
        ofs << "f";
        for (int k = 0; k < n; k++)
          ofs << " " << (globalVertexBase + polyConnects[offset + k]);
        ofs << "\n";
        offset += n;
      }

      globalVertexBase += positions.size();
    }

    ofs.close();
    return true;
  }

  // Exports whatever draw() is currently showing: getClosedMeshes() or
  // getFaceBlocks(), matching the 'k' view - so the exported OBJ always
  // matches the viewport. The input-graph view has no mesh to export.
  void exportCurrentViewViaDialog() {
    if (m_viewMode == ViewMode::InputGraph) {
      std::cout << "[zTsPrototype] Export failed: the input-graph view has "
                   "no mesh to export - press 'k' to switch to a mesh view "
                   "first."
                << std::endl;
      return;
    }

    const char *filter = "Wavefront OBJ (*.obj)\0*.obj\0All Files (*.*)\0*.*\0";
    std::string path;
    if (!saveFileDialog(filter, "Export Current View to OBJ", "obj", path))
      return;

    bool closed = (m_viewMode == ViewMode::ClosedMeshes);
    int count = closed ? m_prototype.getClosedMeshCount()
                       : m_prototype.getFaceBlockCount();
    std::function<zSpace::zObjMesh &(int)> getMesh =
        [this, closed](int i) -> zSpace::zObjMesh & {
      return closed ? m_prototype.getClosedMesh(i)
                    : m_prototype.getFaceBlock(i);
    };

    if (exportMeshesToOBJ(count, getMesh, path)) {
      std::cout << "[zTsPrototype] Exported " << count
                << (closed ? " closed meshes to " : " face blocks to ") << path
                << std::endl;
    } else {
      std::cout << "[zTsPrototype] Export failed (nothing in the current "
                   "view, or could not open file): "
                << path << std::endl;
    }
  }
};

ALICE2_REGISTER_SKETCH_AUTO(zPrototypeSketch)

#endif // __MAIN__

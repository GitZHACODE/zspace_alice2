#define ALICE2_SLICER_IO_ONLY
#include "slicer.h"
// Sketch-owned Slicer USD schema: generic USD operations live in Core IO.
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <zspace/zIO/zUSDapi.h>
#include <zspace/zSlicingSettings.h>
#include <zspace/zToolsets/fabrication/slicing/utils/Utils.h>
namespace zSpace::aliceSlicingUSD {
namespace {
inline std::string formatPrintPathUSDA(const std::string &text) {
  constexpr size_t maxLineLength = 500;
  std::istringstream input(text);
  std::ostringstream output;
  std::string line;
  while (std::getline(input, line)) {
    const auto first = line.find_first_not_of(" \t");
    const std::string declaration =
        first == std::string::npos ? "" : line.substr(first);
    const bool statisticArray =
        declaration.find("double[] printheightdomain = ") == 0 ||
        declaration.find("double[] printwidthdomain = ") == 0;
    const bool statisticScalar =
        declaration.find("double printlength = ") == 0 ||
        declaration.find("double printvolume = ") == 0;
    if (statisticArray || statisticScalar) {
      const auto equals = line.find("= ");
      std::string values = line.substr(equals + 2);
      for (char &c : values)
        if (c == '[' || c == ']' || c == ',')
          c = ' ';
      std::istringstream numbers(values);
      double value;
      output << line.substr(0, equals + 2) << std::fixed
             << std::setprecision(2);
      if (statisticArray)
        output << '[';
      bool firstValue = true;
      while (numbers >> value) {
        if (!firstValue)
          output << ", ";
        output << value;
        firstValue = false;
      }
      if (statisticArray)
        output << ']';
      output << '\n';
      continue;
    }
    const bool vectors =
        declaration.find("custom point3f[] points = [") == 0 ||
        declaration.find("custom normal3f[] printNormals = [") == 0;
    const bool scalars =
        declaration.find("custom float[] printWidths = [") == 0 ||
        declaration.find("custom float[] printHeight = [") == 0;
    const auto begin = line.find("= [");
    const auto end = line.rfind(']');
    if ((!vectors && !scalars) || begin == std::string::npos ||
        end == std::string::npos || end <= begin + 3) {
      output << line << '\n';
      continue;
    }
    const std::string indent(first, ' ');
    const std::string itemIndent = indent + "    ";
    output << line.substr(0, begin + 3) << '\n' << itemIndent;
    size_t column = itemIndent.size();
    size_t itemStart = begin + 3;
    int depth = 0;
    for (size_t i = itemStart; i <= end; ++i) {
      const char c = line[i];
      if (c == '(')
        ++depth;
      if (c == ')')
        --depth;
      if (i != end && (c != ',' || depth != 0))
        continue;
      std::string item = line.substr(itemStart, i - itemStart);
      itemStart = i + 1;
      const auto start = item.find_first_not_of(" \t\r\n");
      if (start == std::string::npos)
        continue;
      item = item.substr(start, item.find_last_not_of(" \t\r\n") - start + 1);
      if (i != end)
        item += ',';
      if (column > itemIndent.size()) {
        if (column + 1 + item.size() > maxLineLength) {
          output << '\n' << itemIndent;
          column = itemIndent.size();
        } else {
          output << ' ';
          ++column;
        }
      }
      output << item;
      column += item.size();
    }
    output << '\n' << indent << line.substr(end) << '\n';
  }
  return output.str();
}

inline void requireType(USDdoc &doc, const std::string &path,
                        const char *type) {
  if (doc.readPrim(path).type != type)
    throw std::runtime_error("Expected " + std::string(type) + " at " + path);
}
// Scale lengths; ratios and directions use scale=1.
double format(double value, double scale = 1.0) {
  const double result = value * scale;
  if (!std::isfinite(result))
    throw std::runtime_error("Numeric conversion overflow.");
  return result;
}
// Import Y-up to Z-up; export applies the inverse rotation.
std::array<double, 3> format(const std::array<double, 3> &value, double scale,
                             bool yUp, bool toInternal = true) {
  std::array<double, 3> result{format(value[0], scale), format(value[1], scale),
                               format(value[2], scale)};
  if (yUp) {
    const double y = result[1], z = result[2];
    result[1] = toInternal ? -z : z;
    result[2] = toInternal ? y : -y;
  }
  return result;
}
void readSettings(USDdoc &doc, double scale, zFloatArray &boundary,
                  zFloatArray &bracing, SlicerUSDInput &out) {
  out.Settings = alice2::slicing::defaultSlicerSettings();
  out.PrintSettings = alice2::slicing::defaultPrintSynSettings();
  auto &s = out.Settings;
  auto &p = out.PrintSettings;
  // Defaults are already in mm; only authored values are converted.
  boundary = s.BoundaryWidth;
  bracing = {s.BracingWidth};
  for (const auto &name : doc.readPrim("/Slicer_setting").attributes) {
    if (name == "slicer:BoundaryWidth") {
      auto values = doc.readFloatArray("/Slicer_setting", name);
      if (!values.empty()) {
        for (auto &v : values)
          v = static_cast<float>(format(v, scale));
        boundary = std::move(values);
      }
    } else if (name == "slicer:BracingWidth") {
      auto values = doc.readFloatArray("/Slicer_setting", name);
      if (!values.empty()) {
        for (auto &v : values)
          v = static_cast<float>(format(v, scale));
        bracing = std::move(values);
      }
    } else if (name == "slicer:DoubleBoundary")
      s.DoubleBoundary = doc.readBool("/Slicer_setting", name);
    else if (name == "slicer:OverlapWidth")
      s.OverlapWidth = format(doc.readDouble("/Slicer_setting", name), scale);
    else if (name == "slicer:SlotOffset")
      s.SlotOffset = format(doc.readDouble("/Slicer_setting", name));
    else if (name == "slicer:BracingSlot")
      s.BracingSlot = format(doc.readDouble("/Slicer_setting", name));
    else if (name == "slicer:FieldScaleUp")
      s.FieldScaleUp = format(doc.readDouble("/Slicer_setting", name));
    else if (name == "slicer:FieldCellSize")
      s.FieldCellSize = format(doc.readDouble("/Slicer_setting", name), scale);
    else if (name == "slicer:LayerSpacing")
      s.LayerSpacing = format(doc.readDouble("/Slicer_setting", name), scale);
    else if (name == "slicer:_HTraceStepFraction")
      s._HTraceStepFraction = format(doc.readDouble("/Slicer_setting", name));
    else if (name == "slicer:_HTraceMaxSteps")
      s._HTraceMaxSteps = doc.readInt("/Slicer_setting", name);
    else if (name == "slicer:_HEqualiseIterations")
      s._HEqualiseIterations = doc.readInt("/Slicer_setting", name);
    else if (name == "slicer:_HArapIterations")
      s._HArapIterations = doc.readInt("/Slicer_setting", name);
    else if (name == "slicer:_HUnrollStart")
      s._HUnrollStart = doc.readInt("/Slicer_setting", name);
    else if (name == "printSyn:ResampleLength")
      p.ResampleLength = format(doc.readDouble("/Slicer_setting", name), scale);
    else if (name == "printSyn:FeatureAngleTol")
      p.FeatureAngleTol = format(doc.readDouble("/Slicer_setting", name));
    else if (name == "printSyn:SeamOffset")
      p.SeamOffset = format(doc.readDouble("/Slicer_setting", name), scale);
    else if (name == "printSyn:SeamSize")
      p.SeamSize = format(doc.readDouble("/Slicer_setting", name), scale);
    else if (name.rfind("slicer:", 0) == 0 || name.rfind("printSyn:", 0) == 0)
      out.UnknownSettings.push_back(name);
  }
  s.BoundaryWidth = boundary;
  s.BracingWidth = bracing.front();
}
inline void readBlock(USDdoc &doc, const std::string &blockPath, double scale,
                      bool yUp, const zFloatArray &boundary,
                      SlicerUSDInput &out) {
  const auto coordinates = doc.readPoints(blockPath);
  zPointArray points;
  points.reserve(coordinates.size());
  for (const auto &point : coordinates) {
    const auto p = format(point, scale, yUp);
    points.emplace_back(p[0], p[1], p[2]);
  }
  auto counts = doc.readIntArray(blockPath, "faceVertexCounts"),
       indices = doc.readIntArray(blockPath, "faceVertexIndices");
  if (counts.empty())
    throw std::runtime_error("Block contains no faces.");
  size_t total = 0;
  for (int c : counts) {
    if (c < 3)
      throw std::runtime_error("Invalid polygon size.");
    total += c;
  }
  if (total != indices.size())
    throw std::runtime_error("Face counts do not match indices.");
  for (int id : indices)
    if (id < 0 || size_t(id) >= points.size())
      throw std::runtime_error("Invalid vertex index.");
  out.FaceTags = doc.readIntArray("/Slicer_data", "block:FaceTag");
  out.FaceWidthIds = doc.readIntArray("/Slicer_data", "block:widthid");
  if (out.FaceTags.size() != counts.size() ||
      out.FaceWidthIds.size() != counts.size())
    throw std::runtime_error("Face attribute count mismatch.");
  for (size_t i = 0; i < counts.size(); ++i) {
    int t = out.FaceTags[i], w = out.FaceWidthIds[i];
    if (t < 1 || t > 3 ||
        (t == 3 ? (w < 0 || size_t(w) >= boundary.size()) : w != -1))
      throw std::runtime_error("Invalid face tag or widthid.");
  }
  const bool reverse = (doc.readString(blockPath, "orientation") ==
                        "leftHanded") != doc.readPrim(blockPath).mirrored;
  if (reverse) {
    size_t start = 0;
    for (int c : counts) {
      std::reverse(indices.begin() + start, indices.begin() + start + c);
      start += c;
    }
  }
  zFnMesh(out.Block).create(points, counts, indices);
  if (zFnMesh(out.Block).numVertices() != points.size() ||
      zFnMesh(out.Block).numPolygons() != counts.size())
    throw std::runtime_error("Mesh construction changed indexing.");
}
inline void readBracing(USDdoc &doc, const std::string &bracingPath,
                        double scale, bool yUp, const zFloatArray &bracing,
                        SlicerUSDInput &out) {
  if (doc.readString(bracingPath, "type") != "linear")
    throw std::runtime_error("Bracing requires linear curves.");
  const auto wrap = doc.readString(bracingPath, "wrap");
  if (wrap != "periodic" && wrap != "nonperiodic")
    throw std::runtime_error("Invalid curve wrap.");
  const bool closed = wrap == "periodic";
  const auto coordinates = doc.readPoints(bracingPath);
  zPointArray points;
  points.reserve(coordinates.size());
  for (const auto &point : coordinates) {
    const auto p = format(point, scale, yUp);
    points.emplace_back(p[0], p[1], p[2]);
  }
  auto counts = doc.readIntArray(bracingPath, "curveVertexCounts");
  out.BracingWidthIds = doc.readIntArray("/Slicer_data", "bracing:widthid");
  size_t total = 0;
  for (int c : counts) {
    if (c < (closed ? 3 : 2))
      throw std::runtime_error("Curve has too few points.");
    total += c;
  }
  if (counts.empty() || total != points.size() ||
      out.BracingWidthIds.size() != counts.size())
    throw std::runtime_error("Curve array count mismatch.");
  out.BracingCurveCount = counts.size();
  size_t start = 0;
  zIntArray edges;
  for (size_t i = 0; i < counts.size(); ++i) {
    int n = counts[i], id = out.BracingWidthIds[i];
    if (id < 0 || size_t(id) >= bracing.size())
      throw std::runtime_error("Invalid bracing widthid.");

    for (int j = 0; j < (closed ? n : n - 1); ++j) {
      int next = (j + 1) % n;
      if (zTsUtils::vecDistSq(points[start + j], points[start + next]) <= 0)
        throw std::runtime_error("Zero-length bracing segment.");
      edges.push_back(static_cast<int>(start) + j);
      edges.push_back(static_cast<int>(start) + next);
    }
    out.BracingPrintWidths.push_back(bracing[id]);
    start += n;
  }
  zFnGraph(out.Bracing).create(points, edges);
}

inline zUSDEdits writePrintPath(const PrintPath &path, double scale,
                                    bool yUp, int layerCount, int blockid) {
  if (layerCount <= 0)
    throw std::runtime_error("Layer count must be positive.");
  std::vector<zTsUtils::GraphPolyline> curves;
  if (!zTsUtils::graphPolylines(path.graph, curves) || curves.size() != 1)
    throw std::runtime_error(
        "Print path must be one connected, unbranched curve.");
  zPointArray positions;
  zFnGraph(const_cast<zObjGraph &>(path.graph)).getVertexPositions(positions);
  if (path.normals.size() != positions.size() ||
      path.widths.size() != positions.size() ||
      path.heights.size() != positions.size())
    throw std::runtime_error("Path attribute count mismatch.");
  auto ids = curves[0].Vertices;
  if (ids.size() > 1 && ids.front() == ids.back())
    ids.pop_back();
  if (ids.size() != positions.size() ||
      ids.size() < (curves[0].Closed ? 3u : 2u))
    throw std::runtime_error("Invalid path vertices.");
  std::vector<float> points, normals, widths, heights;
  points.reserve(ids.size() * 3);
  normals.reserve(ids.size() * 3);
  widths.reserve(ids.size());
  heights.reserve(ids.size());
  const auto f = [](double v) {
    if (!std::isfinite(v) || std::abs(v) > std::numeric_limits<float>::max())
      throw std::runtime_error("Export float overflow.");
    return static_cast<float>(v);
  };
  double length = 0, volume = 0;
  for (size_t i = 0; i < ids.size(); ++i) {
    int id = ids[i];
    if (id < 0 || size_t(id) >= positions.size())
      throw std::runtime_error("Invalid path index.");
    const auto &p = positions[id];
    const auto &n = path.normals[id];
    for (double v :
         {double(p.x), double(p.y), double(p.z), double(n.x), double(n.y),
          double(n.z), double(path.widths[id]), double(path.heights[id])})
      if (!std::isfinite(v))
        throw std::runtime_error("Non-finite path data.");
    if (path.widths[id] < 0 || path.heights[id] < 0)
      throw std::runtime_error("Negative print width/height.");
    const auto point =
        format(std::array<double, 3>{p.x, p.y, p.z}, scale, yUp, false);
    const auto normal =
        format(std::array<double, 3>{n.x, n.y, n.z}, 1.0, yUp, false);
    for (double v : point)
      points.push_back(f(v));
    for (double v : normal)
      normals.push_back(f(v));
    widths.push_back(f(format(path.widths[id], scale)));
    heights.push_back(f(format(path.heights[id], scale)));
    if (i + 1 < ids.size() || curves[0].Closed) {
      double d = std::sqrt(zTsUtils::vecDistSq(
                     p, positions[ids[(i + 1) % ids.size()]])) *
                 scale;
      length += d;
      volume += path.widths[id] * scale * path.heights[id] * scale * d;
    }
  }
  const auto round = [](double v) {
    if (!std::isfinite(v))
      throw std::runtime_error("Export overflow.");
    return std::round(v * 100) / 100;
  };
  auto domain = [&](const auto &a) {
    double lo = format(double(a[0]), scale), hi = lo;
    for (const auto &v : a) {
      lo = std::min(lo, format(double(v), scale));
      hi = std::max(hi, format(double(v), scale));
    }
    return std::vector<double>{round(lo), round(hi)};
  };
  zUSDEdits edits;
  edits.remove = {"/TDP_gcode", "/Gcode", "/printpath"};
  zUSDPrimEdit prim;
  prim.path = "/TDP_gcode";
  prim.type = "Scope";
  // Compute domains before moving the arrays into the transaction.
  edits.customLayerData = {
      {"blockid", {"int", blockid}},
      {"layercount", {"int", layerCount}},
      {"printheightdomain", {"double[]", domain(path.heights)}},
      {"printwidthdomain", {"double[]", domain(path.widths)}},
      {"printlength", {"double", round(length)}},
      {"printvolume", {"double", round(volume)}}};
  USDdoc::writePoints(prim, "points", points);
  USDdoc::writeNormals(prim, "printNormals", normals);
  USDdoc::writeFloatArray(prim, "printWidths", widths);
  USDdoc::writeFloatArray(prim, "printHeight", heights);
  edits.prims.push_back(std::move(prim));
  edits.removeCustomLayerData = {"printstatisticsvalid"};
  return edits;
}

} // namespace

bool importUSD(const std::string &filePath, SlicerUSDInput &out,
                        std::string *outError, const std::string &blockPath,
                        const std::string &bracingPath,
                        const std::string &startPath) {
  out = SlicerUSDInput();
  if (outError)
    outError->clear();
  try {
    USDdoc doc;
    doc.open(filePath);
    const auto metadata = doc.readMetadata();
    const double scale = format(metadata.metersPerUnit, 1000.0);
    if (scale <= 0 || (metadata.upAxis != "Y" && metadata.upAxis != "Z"))
      throw std::runtime_error("Invalid USD units or up axis.");
    const bool yUp = metadata.upAxis == "Y";
    out.MetersPerUnit = metadata.metersPerUnit;
    out.UpAxis = metadata.upAxis;
    requireType(doc, blockPath, "Mesh");
    requireType(doc, bracingPath, "BasisCurves");
    requireType(doc, startPath, "Points");
    requireType(doc, "/Slicer_setting", "Scope");
    requireType(doc, "/Slicer_data", "Scope");
    zFloatArray boundaryWidths, bracingWidths;
    readSettings(doc, scale, boundaryWidths, bracingWidths, out);
    readBlock(doc, blockPath, scale, yUp, boundaryWidths, out);
    readBracing(doc, bracingPath, scale, yUp, bracingWidths, out);
    const auto coordinates = doc.readPoints(startPath);
    if (coordinates.size() != 1)
      throw std::runtime_error("Slot requires exactly one point.");
    const auto anchor = format(coordinates.front(), scale, yUp);
    out.StartPoint = zPoint(anchor[0], anchor[1], anchor[2]);
    return true;
  } catch (const std::exception &e) {
    out = SlicerUSDInput();
    if (outError)
      *outError = e.what();
    return false;
  }
}

bool exportUSD(const PrintPath &path, const std::string &input,
                        const std::string &output, int layerCount,
                        std::string *error, int blockid) {
  const char *phase = "open source document";
  if (error)
    error->clear();
  try {
    USDdoc doc;
    doc.open(input);
    const auto metadata = doc.readMetadata();
    if (!std::isfinite(metadata.metersPerUnit) || metadata.metersPerUnit <= 0 ||
        (metadata.upAxis != "Y" && metadata.upAxis != "Z"))
      throw std::runtime_error("Invalid USD units or up axis.");
    const double scale = format(0.001 / metadata.metersPerUnit);
    if (scale <= 0)
      throw std::runtime_error("Invalid USD scale.");
    const bool yUp = metadata.upAxis == "Y";
    requireType(doc, "/AG_block", "Mesh");
    requireType(doc, "/Slicer_bracing", "BasisCurves");
    requireType(doc, "/Slicer_slot", "Points");
    phase = "prepare print path";
    const auto edits =
        writePrintPath(path, scale, yUp, layerCount, blockid);
    phase = "author output document";
    doc.applyEdits(edits);
    phase = "save output document";
    const auto dot = output.find_last_of('.');
    auto suffix = dot == std::string::npos ? std::string() : output.substr(dot);
    std::transform(suffix.begin(), suffix.end(), suffix.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    if (suffix == ".usda") {
      // Complete serialization before truncating the destination (which may be
      // the input).
      const auto text = formatPrintPathUSDA(doc.text());
      std::ofstream file(output, std::ios::binary | std::ios::trunc);
      file << text;
      file.close();
      if (!file)
        throw std::runtime_error("Cannot save USDA.");
    } else if (suffix == ".usd" || suffix == ".usdc")
      doc.save(output);
    else
      throw std::runtime_error("Expected .usd, .usda or .usdc.");
    return true;
  } catch (const std::exception &e) {
    if (error)
      *error = std::string(phase) + ": " + e.what();
    return false;
  }
}

} // namespace zSpace::aliceSlicingUSD

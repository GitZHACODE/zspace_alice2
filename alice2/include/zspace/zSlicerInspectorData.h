#pragma once

#include <zspace/slicing.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace alice2::slicing {

// Owned display data. Build once after a solve/extraction and render without
// touching graph handles or reclassifying outer junctions on every frame.
struct InspectorPathSnapshot {
    zSpace::zPointArray Points;
    zSpace::zIntArray EdgeConnects;
    zSpace::zFloatArray TrackIds;
    zSpace::zIntArray EdgeFaceTags;
    zSpace::zIntArray FaceTags;
    zSpace::zIntArray FeatureIds;
    zSpace::zIntArray Degree;
    std::map<int, int> FaceTagPointCounts;
    std::map<int, int> FaceTagPieceCounts;
    int OpenEnds = 0;
    int Branches = 0;
};

inline InspectorPathSnapshot inspectorPathSnapshot(const zSpace::PrintPath& path, float featureAngle)
{
    InspectorPathSnapshot result;
    zSpace::zFnGraph graph(const_cast<zSpace::zObjGraph&>(path.graph));
    graph.getVertexPositions(result.Points);
    graph.getEdgeData(result.EdgeConnects);
    result.TrackIds = path.trackIds;
    result.EdgeFaceTags = path.edgeFaceTags;
    result.FaceTags = path.faceTags;
    result.FeatureIds = zSpace::zPrintSyn::featureVertexIds(path, featureAngle);
    for (const auto& piece : path.faceTagSegments) {
        result.FaceTagPointCounts[piece.tag] += static_cast<int>(piece.vertices.size());
        ++result.FaceTagPieceCounts[piece.tag];
    }
    result.Degree.assign(result.Points.size(), 0);
    if (result.EdgeConnects.size() % 2)
        throw std::invalid_argument("Inspector path requires edge pairs.");
    for (int id : result.EdgeConnects) {
        if (id < 0 || static_cast<size_t>(id) >= result.Points.size())
            throw std::invalid_argument("Inspector path has an invalid edge vertex.");
        ++result.Degree[id];
    }
    for (int degree : result.Degree) {
        result.OpenEnds += degree == 1;
        result.Branches += degree > 2;
    }
    return result;
}

struct FieldGridSnapshot {
    int Nx = 0;
    int Ny = 0;
    zSpace::zPointArray Positions;
    zSpace::zPoint Min;
    zSpace::zPoint Max;
};

// zSpace's vertex field grid uses x-major order: index = x * Ny + y.
// Validate once, so texture packing never silently transposes a non-square
// grid or accepts a field whose values are stored on faces instead of vertices.
inline FieldGridSnapshot fieldGridSnapshot(zSpace::zPointArray positions, int nx, int ny)
{
    if (nx < 2 || ny < 2 || static_cast<size_t>(nx) > positions.size() / static_cast<size_t>(ny)
        || static_cast<size_t>(nx) * static_cast<size_t>(ny) != positions.size())
        throw std::invalid_argument("Inspector field requires a regular vertex grid of at least 2 x 2.");
    FieldGridSnapshot result;
    result.Nx = nx;
    result.Ny = ny;
    result.Min = result.Max = positions.front();
    double coordinateScale = 1;
    for (const auto& p : positions) {
        for (double value : {double(p.x), double(p.y), double(p.z)}) {
            if (!std::isfinite(value)) throw std::invalid_argument("Inspector field has nonfinite positions.");
            coordinateScale = (std::max)(coordinateScale, std::abs(value));
        }
        result.Min.x = (std::min)(result.Min.x, p.x); result.Max.x = (std::max)(result.Max.x, p.x);
        result.Min.y = (std::min)(result.Min.y, p.y); result.Max.y = (std::max)(result.Max.y, p.y);
        result.Min.z = (std::min)(result.Min.z, p.z); result.Max.z = (std::max)(result.Max.z, p.z);
    }
    const double dx = (double(positions[static_cast<size_t>(nx - 1) * ny].x) - positions[0].x) / (nx - 1);
    const double dy = (double(positions[ny - 1].y) - positions[0].y) / (ny - 1);
    const double tolerance = 8 * std::numeric_limits<float>::epsilon() * coordinateScale;
    if (!(dx > 0) || !(dy > 0))
        throw std::invalid_argument("Inspector field has a collapsed or reversed grid axis.");
    for (int x = 0; x < nx; ++x) for (int y = 0; y < ny; ++y) {
        const auto& p = positions[static_cast<size_t>(x) * ny + y];
        if (std::abs(double(p.x) - (positions[0].x + x * dx)) > tolerance
            || std::abs(double(p.y) - (positions[0].y + y * dy)) > tolerance
            || std::abs(double(p.z) - positions[0].z) > tolerance)
            throw std::invalid_argument("Inspector field positions are not an x-major regular XY grid.");
    }
    result.Positions = std::move(positions);
    return result;
}

inline FieldGridSnapshot fieldGridSnapshot(const zSpace::zObjMeshScalarField& field)
{
    auto& object = const_cast<zSpace::zObjMeshScalarField&>(field);
    zSpace::zFnMeshScalarField fn(object);
    int nx = 0, ny = 0;
    fn.getResolution(nx, ny);
    zSpace::zPointArray positions;
    zSpace::zFnMesh(object).getVertexPositions(positions);
    // Legacy getValuesPerVertexBoolean() is function-set-local and defaults
    // false on a fresh wrapper. The owned value/vertex counts are authoritative.
    if (positions.size() != static_cast<size_t>(fn.numFieldValues()))
        throw std::invalid_argument("Inspector textures require one field value per vertex.");
    return fieldGridSnapshot(std::move(positions), nx, ny);
}

struct TexturePixels {
    int Width = 0;
    int Height = 0;
    std::vector<unsigned char> Rgba;
};

inline TexturePixels fieldPixels(const FieldGridSnapshot& grid,
                                const zSpace::zColorArray& colors, bool categorical = false)
{
    if (grid.Nx < 2 || grid.Ny < 2
        || static_cast<size_t>(grid.Nx) * static_cast<size_t>(grid.Ny) != grid.Positions.size()
        || colors.size() != grid.Positions.size())
        throw std::invalid_argument("Inspector field colors must match the cached vertex grid.");
    TexturePixels pixels;
    pixels.Width = grid.Nx - (categorical ? 1 : 0);
    pixels.Height = grid.Ny - (categorical ? 1 : 0);
    const size_t count = static_cast<size_t>(pixels.Width) * pixels.Height;
    if (count > pixels.Rgba.max_size() / 4)
        throw std::invalid_argument("Inspector texture exceeds the supported pixel count.");
    pixels.Rgba.resize(count * 4);
    const auto byte = [](float value) {
        return static_cast<unsigned char>(std::lround(std::clamp(double(value), 0.0, 1.0) * 255));
    };
    for (int y = 0; y < pixels.Height; ++y) for (int x = 0; x < pixels.Width; ++x) {
        // A categorical pixel is the lower-left vertex of its corresponding
        // quad, matching the prior flat FaceTag panel; IDs are never blended.
        const auto& color = colors[static_cast<size_t>(x) * grid.Ny + y];
        const size_t offset = (static_cast<size_t>(y) * pixels.Width + x) * 4;
        if (!std::isfinite(color.r) || !std::isfinite(color.g)
            || !std::isfinite(color.b) || !std::isfinite(color.a)) {
            pixels.Rgba[offset] = pixels.Rgba[offset + 1] = pixels.Rgba[offset + 2] = 220;
            pixels.Rgba[offset + 3] = 255;
        } else {
            pixels.Rgba[offset] = byte(color.r); pixels.Rgba[offset + 1] = byte(color.g);
            pixels.Rgba[offset + 2] = byte(color.b); pixels.Rgba[offset + 3] = byte(color.a);
        }
    }
    return pixels;
}

} // namespace alice2::slicing

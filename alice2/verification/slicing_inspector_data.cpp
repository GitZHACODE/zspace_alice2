#include <zspace/zSlicerInspectorData.h>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace zSpace;
using namespace alice2::slicing;
static void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
template<class Action> static void rejects(Action action, const char* message)
{
    try { action(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error(message);
}
static void snapshots()
{
    zPointArray points{{0,0,7},{1,0,7},{1,1,7},{2,1,7}};
    zIntArray edges{0,1,1,2,2,3};
    PrintPath path;
    zFnGraph(path.graph).create(points, edges);
    path.trackIds = {0,0,1,1}; path.edgeFaceTags = {3,4,5}; path.faceTags = {3,3,4,5};
    path.faceTagSegments = {{3,{0,1}}, {4,{1,2,3}}, {3,{2,3}}};
    path.featureFlags = {0,PrintPath::AngleFeature,0,PrintPath::IntersectionFeature};
    const auto display = inspectorPathSnapshot(path, 91);
    require(display.Points.size() == 4 && display.EdgeConnects == edges, "Snapshot lost final geometry");
    require(display.Degree == zIntArray{1,2,2,1} && display.OpenEnds == 2 && display.Branches == 0,
            "Cached path degrees do not describe its topology");
    require(display.FeatureIds == zIntArray{1,2}, "Snapshot did not re-detect geometric corners");
    require(display.TrackIds == path.trackIds && display.FaceTags == path.faceTags
            && display.EdgeFaceTags == path.edgeFaceTags, "Snapshot lost path ownership");
    require(display.FaceTagPointCounts == std::map<int,int>{{3,4},{4,3}}
            && display.FaceTagPieceCounts == std::map<int,int>{{3,2},{4,1}},
            "Snapshot did not retain endpoint-inclusive FaceTag piece totals");
    zPointArray after; zIntArray afterEdges;
    zFnGraph(path.graph).getVertexPositions(after); zFnGraph(path.graph).getEdgeData(afterEdges);
    require(afterEdges == edges && after.size() == points.size(), "Snapshot changed graph topology");
    for (size_t i = 0; i < points.size(); ++i)
        require(zTsUtils::vecDistSq(points[i], after[i]) == 0, "Snapshot moved a final target");
    path.featureFlags.assign(points.size(), 0);
    require(inspectorPathSnapshot(path, 91).FeatureIds == zIntArray{1,2}, "Zero masks hid geometric corners");
    path.featureFlags.clear();
    require(inspectorPathSnapshot(path, 91).FeatureIds == zIntArray{1,2}, "Missing masks changed geometric corners");
    points[0].x = 50;
    zFnGraph(path.graph).create(points, edges);
    path.faceTags[0] = 6;
    require(display.Points[0].x == 0 && display.FaceTags[0] == 3, "Snapshot retained mutable source storage");
    PrintPath empty;
    const auto none = inspectorPathSnapshot(empty, 90);
    require(none.Points.empty() && none.Degree.empty() && none.OpenEnds == 0, "Empty path retained display data");
}
static void textures()
{
    // Non-square grid: x-major vertices, but texture bytes are y-major rows.
    const zPointArray points{{10,20,0},{10,22,0},{10,24,0},{13,20,0},{13,22,0},{13,24,0}};
    const auto grid = fieldGridSnapshot(points, 2, 3);
    require(grid.Min.x == 10 && grid.Min.y == 20 && grid.Max.x == 13 && grid.Max.y == 24,
            "Grid bounds were not taken from the actual vertices");
    zColorArray colors{{1,0,0,1},{0,1,0,1},{0,0,1,1},{1,1,0,1},{0,1,1,1},{1,0,1,1}};
    const auto continuous = fieldPixels(grid, colors);
    require(continuous.Width == 2 && continuous.Height == 3, "Continuous texture resolution changed");
    const std::vector<unsigned char> expected{
        255,0,0,255, 255,255,0,255,
        0,255,0,255, 0,255,255,255,
        0,0,255,255, 255,0,255,255};
    require(continuous.Rgba == expected, "Texture transposed or vertically flipped the grid");
    const auto categorical = fieldPixels(grid, colors, true);
    require(categorical.Width == 1 && categorical.Height == 2
            && categorical.Rgba == std::vector<unsigned char>{255,0,0,255,0,255,0,255},
            "Categorical texture blended labels or changed lower-left ownership");
    colors[0] = zColor(-1,2,.5f,1.5f);
    colors[3].r = std::numeric_limits<float>::quiet_NaN();
    const auto checked = fieldPixels(grid, colors);
    require(std::vector<unsigned char>(checked.Rgba.begin(), checked.Rgba.begin()+8)
            == std::vector<unsigned char>{0,255,128,255,220,220,220,255},
            "Texture color validation did not clamp values or mark nonfinite colors");
    rejects([&] { fieldPixels(grid, {}); }, "Mismatched colors were accepted");
    rejects([&] { fieldGridSnapshot(points, 3, 2); }, "Transposed vertex layout was accepted");
    rejects([&] { fieldGridSnapshot(points, 2, 2); }, "Mismatched grid dimensions were accepted");
    rejects([&] { fieldGridSnapshot({}, 0, 0); }, "Empty field was accepted");
    auto invalid = points; invalid[0].x = std::numeric_limits<float>::infinity();
    rejects([&] { fieldGridSnapshot(invalid, 2, 3); }, "Nonfinite grid was accepted");

    zObjMeshScalarField object;
    zFnMeshScalarField fn(object);
    fn.create(zPoint(0,0,0), zPoint(4,6,0), 2, 3, 1, true, false);
    const auto actual = fieldGridSnapshot(object);
    require(actual.Nx == 2 && actual.Ny == 3 && actual.Positions.size() == 6,
            "Native field snapshot did not retain its vertex resolution");
    zObjMeshScalarField faceValues;
    zFnMeshScalarField(faceValues).create(zPoint(0,0,0), zPoint(4,6,0), 2, 3, 1, false, false);
    rejects([&] { fieldGridSnapshot(faceValues); }, "Per-face values were accepted as per-vertex colors");
}
int main()
{
    try {
        snapshots(); textures();
        std::cout << "PASS owned inspector snapshots and regular-grid texture packing\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n'; return 1;
    }
}

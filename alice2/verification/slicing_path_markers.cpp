#include <zspace/zSlicerPathMarkers.h>
#include <iostream>
#include <stdexcept>

using namespace zSpace;
static void require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}
int main()
{
    try {
        PrintPath path;
        zPointArray finalPoints{{0,0,7},{2,0,7},{3,1,7},{4,2,7},{6,2,7}};
        zIntArray edges{0,1,1,2,2,3,3,4};
        zFnGraph(path.graph).create(finalPoints, edges);
        // The flat middle target is the red/green junction; its two neighboring
        // angle supports remain ordinary targets, regardless of stale masks.
        path.edgeFaceTags = {6,6,3,3};
        path.trackIds.assign(finalPoints.size(),1);
        path.featureFlags = {0,PrintPath::AngleFeature,0,PrintPath::IntersectionFeature,0};
        const auto checkPoint = [](const zPoint& actual, const zPoint& expected) {
            require(zTsUtils::vecDistSq(actual, expected) < 1e-12, "Marker moved off final printpath");
        };
        const auto markers = alice2::slicing::printPathMarkers(path, 150);
        require(markers.Targets.size() == 4 && markers.Features.size() == 1,
                "Final targets and features were not partitioned");
        checkPoint(markers.Targets[0], finalPoints[0]);
        checkPoint(markers.Targets[1], finalPoints[1]);
        checkPoint(markers.Targets[2], finalPoints[3]);
        checkPoint(markers.Targets[3], finalPoints[4]);
        checkPoint(markers.Features[0], finalPoints[2]);
        zPointArray after; zIntArray afterEdges;
        zFnGraph(path.graph).getVertexPositions(after); zFnGraph(path.graph).getEdgeData(afterEdges);
        require(after.size() == finalPoints.size() && afterEdges == edges,
                "Display classification changed the printpath topology");
        for (size_t i = 0; i < after.size(); ++i) checkPoint(after[i], finalPoints[i]);
        path.featureFlags.assign(finalPoints.size(), 0);
        const auto ordinary = alice2::slicing::printPathMarkers(path, 91);
        require(ordinary.Targets.size() == 4 && ordinary.Features.size() == 1,
                "Zero masks hid the outer FaceTag junction");
        path.featureFlags.assign(finalPoints.size(), PrintPath::AngleFeature | PrintPath::IntersectionFeature);
        const auto features = alice2::slicing::printPathMarkers(path, 91);
        require(features.Targets.size() == 4 && features.Features.size() == 1,
                "Stale angle identities created non-junction markers");
        const auto flat = alice2::slicing::printPathMarkers(path, 90);
        require(flat.Targets.size() == 4 && flat.Features.size() == 1,
                "Angle threshold changed junction selection");
        path.edgeFaceTags = {6,0,3,3};
        require(alice2::slicing::printPathMarkers(path,150).Features.empty(),
                "Slot endpoints became outer junction markers");
        path.edgeFaceTags = {6,6,3,3}; path.trackIds.assign(finalPoints.size(),0);
        require(alice2::slicing::printPathMarkers(path,150).Features.empty(),
                "An inner path acquired an outer junction marker");
        path.trackIds.assign(finalPoints.size(),1); path.edgeFaceTags.clear();
        require(alice2::slicing::printPathMarkers(path,150).Features.empty(),
                "Untagged geometric corners became feature markers");
        PrintPath empty;
        const auto none = alice2::slicing::printPathMarkers(empty, 90);
        require(none.Targets.empty() && none.Features.empty(), "Empty result retained markers");
        std::cout << "PASS final postprocessed target/feature markers and unchanged path\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n'; return 1;
    }
}

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
        zPointArray finalPoints{{0,0,7},{2,0,7},{2,2,7},{4,2,7}};
        zIntArray edges{0,1,1,2,2,3};
        zFnGraph(path.graph).create(finalPoints, edges);
        // Stale masks deliberately disagree with the final geometric corners.
        path.featureFlags = {0,PrintPath::AngleFeature,0,PrintPath::IntersectionFeature};
        const auto checkPoint = [](const zPoint& actual, const zPoint& expected) {
            require(zTsUtils::vecDistSq(actual, expected) < 1e-12, "Marker moved off final printpath");
        };
        const auto markers = alice2::slicing::printPathMarkers(path, 91);
        require(markers.Targets.size() == 2 && markers.Features.size() == 2,
                "Final targets and features were not partitioned");
        checkPoint(markers.Targets[0], finalPoints[0]);
        checkPoint(markers.Targets[1], finalPoints[3]);
        checkPoint(markers.Features[0], finalPoints[1]);
        checkPoint(markers.Features[1], finalPoints[2]);
        zPointArray after; zIntArray afterEdges;
        zFnGraph(path.graph).getVertexPositions(after); zFnGraph(path.graph).getEdgeData(afterEdges);
        require(after.size() == finalPoints.size() && afterEdges == edges,
                "Display classification changed the printpath topology");
        for (size_t i = 0; i < after.size(); ++i) checkPoint(after[i], finalPoints[i]);
        path.featureFlags.assign(finalPoints.size(), 0);
        const auto ordinary = alice2::slicing::printPathMarkers(path, 91);
        require(ordinary.Targets.size() == 2 && ordinary.Features.size() == 2,
                "Zero masks hid final geometric corners");
        path.featureFlags.assign(finalPoints.size(), PrintPath::AngleFeature | PrintPath::IntersectionFeature);
        const auto features = alice2::slicing::printPathMarkers(path, 91);
        require(features.Targets.size() == 2 && features.Features.size() == 2,
                "Stale feature identities created non-geometric markers");
        const auto flat = alice2::slicing::printPathMarkers(path, 90);
        require(flat.Targets.size() == 4 && flat.Features.empty(),
                "Feature markers ignored the angle threshold");
        PrintPath empty;
        const auto none = alice2::slicing::printPathMarkers(empty, 90);
        require(none.Targets.empty() && none.Features.empty(), "Empty result retained markers");
        std::cout << "PASS final postprocessed target/feature markers and unchanged path\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n'; return 1;
    }
}

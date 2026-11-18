#pragma once

#include <zspace/slicing.h>

namespace alice2::slicing {

struct PrintPathMarkers {
    zSpace::zPointArray Targets;
    zSpace::zPointArray Features;
};

// Use the final graph's positions and outer FaceTag junctions. Never
// reconstruct targets from the source contour, or change the printpath itself.
inline PrintPathMarkers printPathMarkers(const zSpace::PrintPath& path, float featureAngle)
{
    zSpace::zPointArray points;
    zSpace::zFnGraph(const_cast<zSpace::zObjGraph&>(path.graph)).getVertexPositions(points);
    std::vector<char> feature(points.size(), false);
    for (int id : zSpace::zPrintSyn::featureVertexIds(path, featureAngle))
        if (id >= 0 && static_cast<size_t>(id) < points.size()) feature[id] = true;
    PrintPathMarkers markers;
    for (size_t i = 0; i < points.size(); ++i)
        (feature[i] ? markers.Features : markers.Targets).push_back(points[i]);
    return markers;
}

} // namespace alice2::slicing

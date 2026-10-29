#pragma once

#include <zspace/slicing.h>
#include <cmath>

namespace alice2::slicing {

struct PrintPathMarkers {
    zSpace::zPointArray Targets;
    zSpace::zPointArray Features;
    zSpace::zPointArray OutsideDomain;
};

// Use the final graph's positions and outer FaceTag junctions. Never
// reconstruct targets from the source contour, or change the printpath itself.
inline PrintPathMarkers printPathMarkers(const zSpace::PrintPath& path, float featureAngle, const zSpace::PrintHeightOptimizationSettings* domain = nullptr)
{
    zSpace::zPointArray points;
    zSpace::zFnGraph(const_cast<zSpace::zObjGraph&>(path.graph)).getVertexPositions(points);
    std::vector<char> feature(points.size(), false);
    for (int id : zSpace::zPrintSyn::featureVertexIds(path, featureAngle))
        if (id >= 0 && static_cast<size_t>(id) < points.size()) feature[id] = true;
    PrintPathMarkers markers;
    for (size_t i = 0; i < points.size(); ++i)
        if(domain && domain->Enabled && i<path.heights.size() && (!std::isfinite(path.heights[i]) || path.heights[i]<domain->Minimum-domain->Tolerance || path.heights[i]>domain->Maximum+domain->Tolerance)) markers.OutsideDomain.push_back(points[i]);
        else (feature[i] ? markers.Features : markers.Targets).push_back(points[i]);
    return markers;
}

} // namespace alice2::slicing

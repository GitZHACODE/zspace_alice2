#pragma once
#include "BrickGenerator.h"
#include <limits>
#include <map>
#include <tuple>

namespace alice2::bricks::detail {
struct BrickSpan {
    double begin, end;
    bool isA;
    int plane = -1, segment = -1;
    double reference = std::numeric_limits<double>::quiet_NaN();
    bool boundaryHalf = false;
};
using EqualDivisions = std::map<std::tuple<int,int,int>,int>;
EqualDivisions equalDivisionCounts(const std::vector<ContourSegment>& segments,const BrickOptions& options);
std::vector<BrickSpan> layoutPath(const ContourPath& path, int pathIndex,
    const std::vector<double>& arc, const std::vector<AlignmentPlane>& planes,
    const BrickOptions& options, std::vector<ContourSegment>& segments,
    const EqualDivisions* equalDivisions=nullptr);
}

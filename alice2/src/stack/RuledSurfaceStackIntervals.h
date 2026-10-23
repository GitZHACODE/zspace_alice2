#pragma once

#include "RuledSurfaceStackGeometry.h"

#include <Eigen/Core>

#include <array>
#include <cstdint>
#include <vector>

namespace alice2::stack {

struct StackInterval {
    double lo = 0.0;
    double hi = 0.0;
};

struct ForbiddenIntervalSet {
    std::vector<StackInterval> intervals;

    void addInterval(double lo, double hi, double mergeEpsilon);
    void finalize(double mergeEpsilon);
    int findContainingInterval(double delta, double epsilon) const;
};

struct AABB2 {
    Eigen::Vector2d min = Eigen::Vector2d::Zero();
    Eigen::Vector2d max = Eigen::Vector2d::Zero();
};

bool overlaps(const AABB2& a, const AABB2& b, double epsilon);

struct StackTriangleProxy {
    std::array<Eigen::Vector2d, 3> xy{};
    RuledSurfacePlane plane;
    AABB2 bounds;
};

struct OrientedStackSurface {
    RuledSurface geometry;
    RuledSurfaceBounds2D foamBounds;
    double localMinZ = 0.0;
    double localMaxZ = 0.0;
    std::vector<StackTriangleProxy> finiteTriangles;
    std::vector<StackTriangleProxy> topTriangles;
    std::vector<StackTriangleProxy> bottomTriangles;
    std::vector<StackTriangleProxy> extendedTriangles;
    AABB2 finiteBounds;
};

struct SurfaceOrientationVariants {
    OrientedStackSurface normal;
    std::optional<OrientedStackSurface> flipped;
};

struct PairConstraintData {
    int i = -1;
    int j = -1;
    bool valid[2][2] = {{true, false}, {false, false}};
    ForbiddenIntervalSet finite[2][2];
    ForbiddenIntervalSet hotWire[2][2];
};

struct StackGeometrySettings {
    double clearance = 0.05;
    double geomEpsilon = 1e-10;
    double mergeEpsilon = 1e-9;
};

struct StackGeometryStats {
    std::uint64_t trianglePairCandidates = 0;
    std::uint64_t trianglePairAABBRejected = 0;
    std::uint64_t trianglePairClipped = 0;
    std::uint64_t intervalsGenerated = 0;
    std::uint64_t intervalsMerged = 0;
};

OrientedStackSurface preprocessStackSurface(const RuledSurface& surface,
                                            const RuledSurfaceBounds2D& foamBounds,
                                            double geomEpsilon);
std::vector<SurfaceOrientationVariants> makeStackSurfaceVariants(
    const std::vector<RuledSurface>& surfaces,
    const RuledSurfaceBounds2D& foamBounds,
    double geomEpsilon);

bool appendTrianglePairInterval(const StackTriangleProxy& triI,
                                const StackTriangleProxy& triJ,
                                double clearance,
                                double geomEpsilon,
                                ForbiddenIntervalSet& out,
                                StackGeometryStats* stats = nullptr);

std::vector<PairConstraintData> buildPairConstraintData(
    const std::vector<SurfaceOrientationVariants>& surfaces,
    const StackGeometrySettings& settings,
    StackGeometryStats* stats = nullptr);

size_t pairIndex(int i, int j, int count);

} // namespace alice2::stack

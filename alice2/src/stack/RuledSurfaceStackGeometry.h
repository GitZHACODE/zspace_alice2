#pragma once

#include <Eigen/Core>
#include <Eigen/Geometry>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace alice2::stack {

// A plane is stored as n.dot(p) + d == 0.  Stackable faces must have nz > 0,
// so their height is a single-valued function over XY.
struct RuledSurfacePlane {
    Eigen::Vector3d normal = Eigen::Vector3d::UnitZ();
    double d = 0.0;
};

struct RuledSurfaceFace {
    std::array<Eigen::Vector3d, 4> vertices{};
    RuledSurfacePlane plane;
};

struct RuledSurfaceRuling {
    Eigen::Vector3d a = Eigen::Vector3d::Zero();
    Eigen::Vector3d b = Eigen::Vector3d::Zero();
};

struct RuledSurface {
    std::vector<RuledSurfaceFace> faces;
    std::vector<RuledSurfaceRuling> rulings;
    // Optional second skin using the same ruling stations as rulings.  When
    // populated, its faces are appended to faces so the stack solver tests
    // both actual material skins.  Hot-wire validation additionally derives
    // the boundary walls from these matching ruling stations.
    std::vector<RuledSurfaceRuling> bottomRulings;
};

struct RuledSurfaceBounds2D {
    Eigen::Vector2d min = Eigen::Vector2d::Zero();
    Eigen::Vector2d max = Eigen::Vector2d::Zero();
};

struct RuledSurfaceProceduralSettings {
    int surfaceCount = 20;
    int planesPerSurface = 8;
    double randomVariation = 0.35;
    double lengthVariation = 0.35;
    double rulingTurn = 0.45;
    double width = 4.0;
    double baseLength = 7.0;
    double baseRise = 1.2;
    std::uint32_t seed = 1;
};

RuledSurface makeRuledSurface(const std::vector<RuledSurfaceRuling>& rulings);
std::optional<RuledSurface> flipRuledSurfaceForStack(const RuledSurface& surface,
                                                      double normalEpsilon = 1e-12);
std::vector<RuledSurface> makeProceduralRuledSurfaces(const RuledSurfaceProceduralSettings& settings);
bool isValidForStackDirection(const RuledSurface& surface, double normalEpsilon = 1e-12);
double ruledSurfacePlaneHeight(const RuledSurfacePlane& plane, const Eigen::Vector2d& xy);
RuledSurfaceBounds2D ruledSurfaceBoundsXY(const RuledSurface& surface);
RuledSurfaceBounds2D ruledSurfaceGroupBoundsXY(const std::vector<RuledSurface>& surfaces);

} // namespace alice2::stack

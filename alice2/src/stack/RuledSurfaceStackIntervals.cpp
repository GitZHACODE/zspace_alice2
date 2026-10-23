#include "RuledSurfaceStackIntervals.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>

namespace alice2::stack {
namespace {
using Polygon2D = std::vector<Eigen::Vector2d>;

double cross2D(const Eigen::Vector2d& a, const Eigen::Vector2d& b) {
    return a.x() * b.y() - a.y() * b.x();
}
double polygonAreaTwice(const Polygon2D& p) {
    double area = 0.0;
    for (size_t i = 0; i < p.size(); ++i) area += cross2D(p[i], p[(i + 1) % p.size()]);
    return area;
}
AABB2 boundsFor(const std::array<Eigen::Vector2d, 3>& tri) {
    AABB2 b{tri[0], tri[0]};
    for (int i = 1; i < 3; ++i) b.min = b.min.cwiseMin(tri[i]), b.max = b.max.cwiseMax(tri[i]);
    return b;
}

Polygon2D clipPolygonAgainstLinear(const Polygon2D& input, const Eigen::Vector2d& a,
                                   const Eigen::Vector2d& b, double orientation, double eps) {
    Polygon2D out;
    if (input.empty()) return out;
    const Eigen::Vector2d edge = b - a;
    auto signedDistance = [&](const Eigen::Vector2d& p) { return orientation * cross2D(edge, p - a); };
    Eigen::Vector2d previous = input.back();
    double previousDistance = signedDistance(previous);
    bool previousInside = previousDistance >= -eps;
    for (const auto& current : input) {
        const double currentDistance = signedDistance(current);
        const bool currentInside = currentDistance >= -eps;
        if (currentInside != previousInside) {
            const double denom = previousDistance - currentDistance;
            if (std::abs(denom) > eps) out.push_back(previous + (current - previous) * (previousDistance / denom));
        }
        if (currentInside) out.push_back(current);
        previous = current;
        previousDistance = currentDistance;
        previousInside = currentInside;
    }
    return out;
}

Polygon2D clipTriangles(const StackTriangleProxy& subject, const StackTriangleProxy& clip, double eps) {
    Polygon2D p(subject.xy.begin(), subject.xy.end());
    const double orientation = polygonAreaTwice(Polygon2D(clip.xy.begin(), clip.xy.end())) >= 0.0 ? 1.0 : -1.0;
    for (int i = 0; i < 3 && !p.empty(); ++i) p = clipPolygonAgainstLinear(p, clip.xy[i], clip.xy[(i + 1) % 3], orientation, eps);
    return p;
}

bool addTriangle(std::vector<StackTriangleProxy>& out, const RuledSurfacePlane& plane,
                 const Eigen::Vector3d& a, const Eigen::Vector3d& b, const Eigen::Vector3d& c,
                 double eps) {
    StackTriangleProxy t{{a.head<2>(), b.head<2>(), c.head<2>()}, plane, {}};
    if (std::abs(cross2D(t.xy[1] - t.xy[0], t.xy[2] - t.xy[0])) <= eps * eps) return false;
    t.bounds = boundsFor(t.xy);
    out.push_back(t);
    return true;
}

bool extendRulingToBounds(const RuledSurfaceRuling& r, const RuledSurfaceBounds2D& bounds,
                          Eigen::Vector2d& a, Eigen::Vector2d& b, double eps) {
    const Eigen::Vector2d origin = r.a.head<2>();
    const Eigen::Vector2d direction = r.b.head<2>() - origin;
    if (direction.squaredNorm() <= eps * eps) return false;
    double lo = -std::numeric_limits<double>::infinity();
    double hi = std::numeric_limits<double>::infinity();
    for (int axis = 0; axis < 2; ++axis) {
        if (std::abs(direction[axis]) <= eps) {
            if (origin[axis] < bounds.min[axis] - eps || origin[axis] > bounds.max[axis] + eps) return false;
            continue;
        }
        double t0 = (bounds.min[axis] - origin[axis]) / direction[axis];
        double t1 = (bounds.max[axis] - origin[axis]) / direction[axis];
        if (t0 > t1) std::swap(t0, t1);
        lo = std::max(lo, t0); hi = std::min(hi, t1);
    }
    if (lo > hi - eps) return false;
    a = origin + lo * direction; b = origin + hi * direction;
    return true;
}

const OrientedStackSurface* variantAt(const SurfaceOrientationVariants& v, int flipped) {
    if (!flipped) return &v.normal;
    return v.flipped ? &*v.flipped : nullptr;
}

void appendSet(const std::vector<StackTriangleProxy>& a, const std::vector<StackTriangleProxy>& b,
               double clearance, double eps, ForbiddenIntervalSet& out, StackGeometryStats* stats) {
    for (const auto& ta : a) for (const auto& tb : b)
        appendTrianglePairInterval(ta, tb, clearance, eps, out, stats);
}

// A two-skin solid is collision-free only when j is completely below i or
// completely above i.  Skin-crossing tests alone miss the case where j fits
// between i's top and bottom without crossing either sheet.  Build one broad
// forbidden delta interval from the actual offset bottom and top skins.
bool appendVolumeExclusionInterval(const std::vector<StackTriangleProxy>& belowLeft,
                                   const std::vector<StackTriangleProxy>& belowRight,
                                   const std::vector<StackTriangleProxy>& aboveLeft,
                                   const std::vector<StackTriangleProxy>& aboveRight,
                                   double clearance, double eps, ForbiddenIntervalSet& out,
                                   StackGeometryStats* stats) {
    if (belowLeft.empty() || belowRight.empty() || aboveLeft.empty() || aboveRight.empty()) return false;
    ForbiddenIntervalSet below;
    ForbiddenIntervalSet above;
    appendSet(belowLeft, belowRight, clearance, eps, below, stats);
    appendSet(aboveLeft, aboveRight, clearance, eps, above, stats);
    if (below.intervals.empty() || above.intervals.empty()) return false;
    double lower = std::numeric_limits<double>::infinity();
    double upper = -std::numeric_limits<double>::infinity();
    for (const auto& interval : below.intervals) lower = std::min(lower, interval.lo);
    for (const auto& interval : above.intervals) upper = std::max(upper, interval.hi);
    if (lower >= upper) return false;
    out.addInterval(lower, upper, eps);
    return true;
}

bool appendSolidVolumeInterval(const OrientedStackSurface& i, const OrientedStackSurface& j,
                               double clearance, double eps, ForbiddenIntervalSet& out,
                               StackGeometryStats* stats) {
    // delta = z_j - z_i. j below i uses B_i - T_j; j above i uses T_i - B_j.
    return appendVolumeExclusionInterval(i.bottomTriangles, j.topTriangles,
                                         i.topTriangles, j.bottomTriangles,
                                         clearance, eps, out, stats);
}

// Unlike the surface interval tests above, a wire has to be tested against a
// *closed* volume: its top and bottom sheets as well as the four boundary
// walls.  For a fixed XY crossing of a wire segment and a triangle, changing
// the relative stack height only changes Z.  The set of crossing heights is
// therefore one interval.  Enumerating the vertices of the small linear
// feasibility problem below gives its exact endpoints for non-degenerate
// projected triangles.
struct StackTriangle3D {
    Eigen::Vector3d a;
    Eigen::Vector3d b;
    Eigen::Vector3d c;
    AABB2 xyBounds;
};

AABB2 xyBoundsFor(const Eigen::Vector3d& a, const Eigen::Vector3d& b, const Eigen::Vector3d& c) {
    AABB2 bounds{a.head<2>(), a.head<2>()};
    bounds.min = bounds.min.cwiseMin(b.head<2>()).cwiseMin(c.head<2>());
    bounds.max = bounds.max.cwiseMax(b.head<2>()).cwiseMax(c.head<2>());
    return bounds;
}

void appendQuadTriangles(const Eigen::Vector3d& a, const Eigen::Vector3d& b,
                         const Eigen::Vector3d& c, const Eigen::Vector3d& d,
                         std::vector<StackTriangle3D>& out) {
    out.push_back({a, b, c, xyBoundsFor(a, b, c)});
    out.push_back({a, c, d, xyBoundsFor(a, c, d)});
}

std::vector<StackTriangle3D> closedSolidTriangles(const RuledSurface& surface) {
    std::vector<StackTriangle3D> out;
    const size_t stations = std::min(surface.rulings.size(), surface.bottomRulings.size());
    if (stations < 2) return out;
    const size_t spans = stations - 1;
    // The face list contains the top strip followed by the actual offset
    // bottom strip.  Keep this independent of face winding.
    for (size_t k = 0; k < spans && k < surface.faces.size(); ++k) {
        const auto& f = surface.faces[k].vertices;
        appendQuadTriangles(f[0], f[1], f[2], f[3], out);
    }
    for (size_t k = 0; k < spans && spans + k < surface.faces.size(); ++k) {
        const auto& f = surface.faces[spans + k].vertices;
        appendQuadTriangles(f[0], f[1], f[2], f[3], out);
    }
    const auto& top = surface.rulings;
    const auto& bottom = surface.bottomRulings;
    // Longitudinal walls.
    for (size_t k = 0; k < spans; ++k) {
        appendQuadTriangles(top[k].a, top[k + 1].a, bottom[k + 1].a, bottom[k].a, out);
        appendQuadTriangles(top[k].b, bottom[k].b, bottom[k + 1].b, top[k + 1].b, out);
    }
    // The two end caps.
    appendQuadTriangles(top.front().a, bottom.front().a, bottom.front().b, top.front().b, out);
    appendQuadTriangles(top.back().a, top.back().b, bottom.back().b, bottom.back().a, out);
    return out;
}

bool extendedRulingSegment(const RuledSurfaceRuling& ruling, const RuledSurfaceBounds2D& bounds,
                           double eps, Eigen::Vector3d& start, Eigen::Vector3d& end) {
    Eigen::Vector2d xyStart, xyEnd;
    if (!extendRulingToBounds(ruling, bounds, xyStart, xyEnd, eps)) return false;
    const Eigen::Vector2d direction = ruling.b.head<2>() - ruling.a.head<2>();
    const int axis = std::abs(direction.x()) >= std::abs(direction.y()) ? 0 : 1;
    if (std::abs(direction[axis]) <= eps) return false;
    const double startT = (xyStart[axis] - ruling.a[axis]) / direction[axis];
    const double endT = (xyEnd[axis] - ruling.a[axis]) / direction[axis];
    start = ruling.a + startT * (ruling.b - ruling.a);
    end = ruling.a + endT * (ruling.b - ruling.a);
    return true;
}

bool appendSweptSegmentTriangleInterval(const Eigen::Vector3d& lineStart, const Eigen::Vector3d& lineEnd,
                                        const StackTriangle3D& triangle, double clearance, double eps,
                                        ForbiddenIntervalSet& out, StackGeometryStats* stats) {
    const Eigen::Vector3d lineDirection = lineEnd - lineStart;
    const Eigen::Vector3d edgeA = triangle.b - triangle.a;
    const Eigen::Vector3d edgeB = triangle.c - triangle.a;
    const AABB2 lineBounds{lineStart.head<2>().cwiseMin(lineEnd.head<2>()),
                           lineStart.head<2>().cwiseMax(lineEnd.head<2>())};
    if (!overlaps(lineBounds, triangle.xyBounds, eps)) return false;

    static constexpr std::array<std::array<double, 4>, 5> kBounds = {{{1.0, 0.0, 0.0, 0.0},
                                                                         {1.0, 0.0, 0.0, 1.0},
                                                                         {0.0, 1.0, 0.0, 0.0},
                                                                         {0.0, 0.0, 1.0, 0.0},
                                                                         {0.0, 1.0, 1.0, 1.0}}};
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    bool found = false;
    bool degenerateProjection = false;
    for (const auto& bound : kBounds) {
        Eigen::Matrix3d system;
        system << lineDirection.x(), -edgeA.x(), -edgeB.x(),
                  lineDirection.y(), -edgeA.y(), -edgeB.y(),
                  bound[0], bound[1], bound[2];
        Eigen::FullPivLU<Eigen::Matrix3d> lu(system);
        if (lu.rank() < 3) {
            degenerateProjection = true;
            continue;
        }
        const Eigen::Vector3d rhs(triangle.a.x() - lineStart.x(), triangle.a.y() - lineStart.y(), bound[3]);
        const Eigen::Vector3d value = lu.solve(rhs);
        const double t = value.x(), u = value.y(), v = value.z();
        if (t < -eps || t > 1.0 + eps || u < -eps || v < -eps || u + v > 1.0 + eps) continue;
        const double delta = (lineStart + t * lineDirection).z() - (triangle.a + u * edgeA + v * edgeB).z();
        minimum = std::min(minimum, delta); maximum = std::max(maximum, delta);
        found = true;
    }
    if (!found && degenerateProjection) {
        // A vertical wall can project to a line.  Coincident projected edges
        // have a higher-dimensional solution; retain a safe interval there.
        const double lineMin = std::min(lineStart.z(), lineEnd.z());
        const double lineMax = std::max(lineStart.z(), lineEnd.z());
        const double triangleMin = std::min({triangle.a.z(), triangle.b.z(), triangle.c.z()});
        const double triangleMax = std::max({triangle.a.z(), triangle.b.z(), triangle.c.z()});
        minimum = lineMin - triangleMax; maximum = lineMax - triangleMin;
        found = true;
    }
    if (!found) return false;
    out.addInterval(minimum - clearance, maximum + clearance, eps);
    if (stats) ++stats->intervalsGenerated;
    return true;
}

// Adds intervals in the convention delta = z_solid - z_wire.
bool appendWireClosedSolidIntervals(const OrientedStackSurface& wire, const OrientedStackSurface& solid,
                                    const RuledSurfaceBounds2D& foamBounds, double clearance, double eps,
                                    ForbiddenIntervalSet& out, StackGeometryStats* stats) {
    const std::vector<StackTriangle3D> triangles = closedSolidTriangles(solid.geometry);
    bool appended = false;
    for (const auto& ruling : wire.geometry.rulings) {
        Eigen::Vector3d start, end;
        if (!extendedRulingSegment(ruling, foamBounds, eps, start, end)) continue;
        for (const auto& triangle : triangles)
            appended |= appendSweptSegmentTriangleInterval(start, end, triangle, clearance, eps, out, stats);
    }
    return appended;
}
} // namespace

void ForbiddenIntervalSet::addInterval(double lo, double hi, double) {
    if (lo > hi) std::swap(lo, hi);
    intervals.push_back({lo, hi});
}
void ForbiddenIntervalSet::finalize(double mergeEpsilon) {
    if (intervals.empty()) return;
    std::sort(intervals.begin(), intervals.end(), [](const StackInterval& a, const StackInterval& b) {
        return a.lo == b.lo ? a.hi < b.hi : a.lo < b.lo;
    });
    std::vector<StackInterval> merged;
    merged.reserve(intervals.size());
    for (const auto& interval : intervals) {
        if (merged.empty() || interval.lo > merged.back().hi + mergeEpsilon) merged.push_back(interval);
        else merged.back().hi = std::max(merged.back().hi, interval.hi);
    }
    intervals = std::move(merged);
}
int ForbiddenIntervalSet::findContainingInterval(double delta, double epsilon) const {
    auto it = std::upper_bound(intervals.begin(), intervals.end(), delta,
        [](double value, const StackInterval& interval) { return value < interval.lo; });
    if (it == intervals.begin()) return -1;
    --it;
    return delta > it->lo + epsilon && delta < it->hi - epsilon
        ? static_cast<int>(it - intervals.begin()) : -1;
}

bool overlaps(const AABB2& a, const AABB2& b, double epsilon) {
    return a.min.x() <= b.max.x() + epsilon && b.min.x() <= a.max.x() + epsilon &&
           a.min.y() <= b.max.y() + epsilon && b.min.y() <= a.max.y() + epsilon;
}

OrientedStackSurface preprocessStackSurface(const RuledSurface& surface,
                                            const RuledSurfaceBounds2D& foamBounds,
                                            double geomEpsilon) {
    OrientedStackSurface out;
    out.geometry = surface;
    out.foamBounds = foamBounds;
    out.localMinZ = std::numeric_limits<double>::infinity();
    out.localMaxZ = -std::numeric_limits<double>::infinity();
    out.finiteBounds.min = Eigen::Vector2d::Constant(std::numeric_limits<double>::infinity());
    out.finiteBounds.max = Eigen::Vector2d::Constant(-std::numeric_limits<double>::infinity());
    const size_t topFaceCount = surface.rulings.size() > 1 ? surface.rulings.size() - 1 : 0;
    const size_t bottomFaceCount = surface.bottomRulings.size() > 1 ? surface.bottomRulings.size() - 1 : 0;
    for (size_t faceIndex = 0; faceIndex < surface.faces.size(); ++faceIndex) {
        const auto& face = surface.faces[faceIndex];
        for (const auto& p : face.vertices) {
            out.localMinZ = std::min(out.localMinZ, p.z()); out.localMaxZ = std::max(out.localMaxZ, p.z());
            out.finiteBounds.min = out.finiteBounds.min.cwiseMin(p.head<2>());
            out.finiteBounds.max = out.finiteBounds.max.cwiseMax(p.head<2>());
        }
        addTriangle(out.finiteTriangles, face.plane, face.vertices[0], face.vertices[1], face.vertices[2], geomEpsilon);
        addTriangle(out.finiteTriangles, face.plane, face.vertices[0], face.vertices[2], face.vertices[3], geomEpsilon);
        std::vector<StackTriangleProxy>* skin = nullptr;
        if (faceIndex < topFaceCount) skin = &out.topTriangles;
        else if (faceIndex >= topFaceCount && faceIndex < topFaceCount + bottomFaceCount) skin = &out.bottomTriangles;
        if (skin) {
            addTriangle(*skin, face.plane, face.vertices[0], face.vertices[1], face.vertices[2], geomEpsilon);
            addTriangle(*skin, face.plane, face.vertices[0], face.vertices[2], face.vertices[3], geomEpsilon);
        }
    }
    if (surface.rulings.size() >= 2) for (size_t k = 0; k + 1 < surface.rulings.size(); ++k) {
        Eigen::Vector2d a0, b0, a1, b1;
        if (!extendRulingToBounds(surface.rulings[k], foamBounds, a0, b0, geomEpsilon) ||
            !extendRulingToBounds(surface.rulings[k + 1], foamBounds, a1, b1, geomEpsilon)) continue;
        const auto& plane = surface.faces[k].plane;
        const Eigen::Vector3d p0(a0.x(), a0.y(), ruledSurfacePlaneHeight(plane, a0));
        const Eigen::Vector3d p1(a1.x(), a1.y(), ruledSurfacePlaneHeight(plane, a1));
        const Eigen::Vector3d p2(b1.x(), b1.y(), ruledSurfacePlaneHeight(plane, b1));
        const Eigen::Vector3d p3(b0.x(), b0.y(), ruledSurfacePlaneHeight(plane, b0));
        addTriangle(out.extendedTriangles, plane, p0, p1, p2, geomEpsilon);
        addTriangle(out.extendedTriangles, plane, p0, p2, p3, geomEpsilon);
    }
    if (!std::isfinite(out.localMinZ)) out.localMinZ = out.localMaxZ = 0.0, out.finiteBounds = {};
    return out;
}

std::vector<SurfaceOrientationVariants> makeStackSurfaceVariants(
    const std::vector<RuledSurface>& surfaces, const RuledSurfaceBounds2D& foamBounds, double geomEpsilon) {
    std::vector<SurfaceOrientationVariants> out;
    out.reserve(surfaces.size());
    for (const auto& surface : surfaces) {
        if (!isValidForStackDirection(surface, geomEpsilon)) throw std::invalid_argument("surface is not a +Z height field");
        SurfaceOrientationVariants variants;
        variants.normal = preprocessStackSurface(surface, foamBounds, geomEpsilon);
        if (auto flipped = flipRuledSurfaceForStack(surface, geomEpsilon))
            variants.flipped = preprocessStackSurface(*flipped, foamBounds, geomEpsilon);
        out.push_back(std::move(variants));
    }
    return out;
}

bool appendTrianglePairInterval(const StackTriangleProxy& triI, const StackTriangleProxy& triJ,
                                double clearance, double geomEpsilon, ForbiddenIntervalSet& out,
                                StackGeometryStats* stats) {
    if (stats) ++stats->trianglePairCandidates;
    if (!overlaps(triI.bounds, triJ.bounds, geomEpsilon)) { if (stats) ++stats->trianglePairAABBRejected; return false; }
    Polygon2D overlap = clipTriangles(triI, triJ, geomEpsilon);
    if (overlap.size() < 3 || std::abs(polygonAreaTwice(overlap)) <= geomEpsilon * geomEpsilon) return false;
    if (stats) ++stats->trianglePairClipped;
    double qMin = std::numeric_limits<double>::infinity();
    double qMax = -std::numeric_limits<double>::infinity();
    for (const auto& p : overlap) {
        const double q = ruledSurfacePlaneHeight(triI.plane, p) - ruledSurfacePlaneHeight(triJ.plane, p);
        qMin = std::min(qMin, q); qMax = std::max(qMax, q);
    }
    out.addInterval(qMin - clearance, qMax + clearance, geomEpsilon);
    if (stats) ++stats->intervalsGenerated;
    return true;
}

size_t pairIndex(int i, int j, int count) {
    assert(i >= 0 && i < j && j < count);
    return static_cast<size_t>(i) * static_cast<size_t>(2 * count - i - 1) / 2 + static_cast<size_t>(j - i - 1);
}

std::vector<PairConstraintData> buildPairConstraintData(
    const std::vector<SurfaceOrientationVariants>& surfaces, const StackGeometrySettings& settings,
    StackGeometryStats* stats) {
    const int n = static_cast<int>(surfaces.size());
    std::vector<PairConstraintData> pairs(static_cast<size_t>(n) * (n - 1) / 2);
    for (int i = 0; i < n; ++i) for (int j = i + 1; j < n; ++j) {
        PairConstraintData& pair = pairs[pairIndex(i, j, n)]; pair.i = i; pair.j = j;
        for (int fi = 0; fi < 2; ++fi) for (int fj = 0; fj < 2; ++fj) {
            const auto* si = variantAt(surfaces[i], fi); const auto* sj = variantAt(surfaces[j], fj);
            pair.valid[fi][fj] = si && sj;
            if (!pair.valid[fi][fj]) continue;
            const bool hasSolidSkins = appendSolidVolumeInterval(*si, *sj, settings.clearance,
                                                                 settings.geomEpsilon, pair.finite[fi][fj], stats);
            if (!hasSolidSkins) {
                appendSet(si->finiteTriangles, sj->finiteTriangles, settings.clearance,
                          settings.geomEpsilon, pair.finite[fi][fj], stats);
            }
            pair.finite[fi][fj].finalize(settings.mergeEpsilon);
            pair.hotWire[fi][fj] = pair.finite[fi][fj];
            // These constraints use every closed-solid face, including the
            // side walls.  The pair convention is delta = z_j - z_i.
            appendWireClosedSolidIntervals(*si, *sj, si->foamBounds, settings.clearance,
                                           settings.geomEpsilon, pair.hotWire[fi][fj], stats);
            ForbiddenIntervalSet wireJAgainstSolidI;
            appendWireClosedSolidIntervals(*sj, *si, sj->foamBounds, settings.clearance,
                                           settings.geomEpsilon, wireJAgainstSolidI, stats);
            for (const StackInterval& interval : wireJAgainstSolidI.intervals)
                pair.hotWire[fi][fj].addInterval(-interval.hi, -interval.lo, settings.geomEpsilon);
            const size_t before = pair.hotWire[fi][fj].intervals.size();
            pair.hotWire[fi][fj].finalize(settings.mergeEpsilon);
            if (stats) stats->intervalsMerged += before - pair.hotWire[fi][fj].intervals.size();
        }
    }
    return pairs;
}

} // namespace alice2::stack

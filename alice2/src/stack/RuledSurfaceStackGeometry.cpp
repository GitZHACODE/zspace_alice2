#include "RuledSurfaceStackGeometry.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <stdexcept>

namespace alice2::stack {
namespace {

RuledSurfacePlane planeFor(const std::array<Eigen::Vector3d, 4>& v) {
    Eigen::Vector3d n = (v[1] - v[0]).cross(v[2] - v[0]);
    const double length = n.norm();
    if (length <= 1e-15) {
        return {};
    }
    n /= length;
    return {n, -n.dot(v[0])};
}

Eigen::Vector2d xy(const Eigen::Vector3d& p) { return p.head<2>(); }

} // namespace

RuledSurface makeRuledSurface(const std::vector<RuledSurfaceRuling>& rulings) {
    RuledSurface out;
    out.rulings = rulings;
    if (rulings.size() < 2) return out;
    out.faces.reserve(rulings.size() - 1);
    for (size_t i = 0; i + 1 < rulings.size(); ++i) {
        RuledSurfaceFace face;
        // Longitudinal direction first, then across the ruling: +Z normal.
        face.vertices = {rulings[i].a, rulings[i + 1].a,
                         rulings[i + 1].b, rulings[i].b};
        face.plane = planeFor(face.vertices);
        out.faces.push_back(face);
    }
    return out;
}

std::optional<RuledSurface> flipRuledSurfaceForStack(const RuledSurface& surface,
                                                      double normalEpsilon) {
    if (surface.rulings.size() < 2) return std::nullopt;
    Eigen::Vector3d centre = Eigen::Vector3d::Zero();
    for (const auto& ruling : surface.rulings) centre += ruling.a + ruling.b;
    centre /= static_cast<double>(surface.rulings.size() * 2);
    const Eigen::Vector3d firstCentre = 0.5 * (surface.rulings.front().a + surface.rulings.front().b);
    const Eigen::Vector3d lastCentre = 0.5 * (surface.rulings.back().a + surface.rulings.back().b);
    Eigen::Vector3d axis(lastCentre.x() - firstCentre.x(), lastCentre.y() - firstCentre.y(), 0.0);
    if (axis.norm() <= normalEpsilon) axis = Eigen::Vector3d::UnitX();
    else axis.normalize();
    const auto flipPoint = [&](const Eigen::Vector3d& point) {
        const Eigen::Vector3d relative = point - centre;
        // 180-degree rotation around the strip's horizontal longitudinal axis.
        return (centre + 2.0 * axis * axis.dot(relative) - relative).eval();
    };
    std::vector<RuledSurfaceRuling> flippedTop;
    std::vector<RuledSurfaceRuling> flippedBottom;
    flippedTop.reserve(surface.rulings.size());
    flippedBottom.reserve(surface.bottomRulings.size());
    for (const auto& ruling : surface.rulings) {
        flippedTop.push_back({flipPoint(ruling.a), flipPoint(ruling.b)});
    }
    for (const auto& ruling : surface.bottomRulings) {
        flippedBottom.push_back({flipPoint(ruling.a), flipPoint(ruling.b)});
    }
    const auto makeWithBottomSkin = [](const std::vector<RuledSurfaceRuling>& top,
                                       const std::vector<RuledSurfaceRuling>& bottom) {
        RuledSurface result = makeRuledSurface(top);
        if (bottom.empty()) return result;
        RuledSurface lower = makeRuledSurface(bottom);
        result.bottomRulings = bottom;
        result.faces.insert(result.faces.end(), lower.faces.begin(), lower.faces.end());
        return result;
    };
    // A physical 180-degree turn sends the original upper skin to the lower
    // side.  Preserve geometric roles (top first, bottom second) so volume
    // interval constraints remain valid for flipped solids.
    const bool hasBottomSkin = !flippedBottom.empty();
    RuledSurface flipped = hasBottomSkin ? makeWithBottomSkin(flippedBottom, flippedTop)
                                         : makeWithBottomSkin(flippedTop, flippedBottom);
    if (isValidForStackDirection(flipped, normalEpsilon)) return flipped;
    for (auto& ruling : flippedTop) std::swap(ruling.a, ruling.b);
    for (auto& ruling : flippedBottom) std::swap(ruling.a, ruling.b);
    flipped = hasBottomSkin ? makeWithBottomSkin(flippedBottom, flippedTop)
                             : makeWithBottomSkin(flippedTop, flippedBottom);
    return isValidForStackDirection(flipped, normalEpsilon) ? std::optional<RuledSurface>(std::move(flipped)) : std::nullopt;
}

bool isValidForStackDirection(const RuledSurface& surface, double normalEpsilon) {
    if (surface.faces.empty()) return false;
    for (const auto& face : surface.faces) {
        if (!std::isfinite(face.plane.normal.z()) || face.plane.normal.z() <= normalEpsilon) {
            return false;
        }
    }
    return true;
}

double ruledSurfacePlaneHeight(const RuledSurfacePlane& plane, const Eigen::Vector2d& p) {
    return -(plane.normal.x() * p.x() + plane.normal.y() * p.y() + plane.d) /
           plane.normal.z();
}

RuledSurfaceBounds2D ruledSurfaceBoundsXY(const RuledSurface& surface) {
    RuledSurfaceBounds2D out;
    out.min = Eigen::Vector2d::Constant(std::numeric_limits<double>::infinity());
    out.max = Eigen::Vector2d::Constant(-std::numeric_limits<double>::infinity());
    for (const auto& face : surface.faces) {
        for (const auto& p : face.vertices) {
            out.min = out.min.cwiseMin(xy(p));
            out.max = out.max.cwiseMax(xy(p));
        }
    }
    if (!std::isfinite(out.min.x())) out.min.setZero(), out.max.setZero();
    return out;
}

RuledSurfaceBounds2D ruledSurfaceGroupBoundsXY(const std::vector<RuledSurface>& surfaces) {
    RuledSurfaceBounds2D out;
    out.min = Eigen::Vector2d::Constant(std::numeric_limits<double>::infinity());
    out.max = Eigen::Vector2d::Constant(-std::numeric_limits<double>::infinity());
    for (const auto& surface : surfaces) {
        const auto b = ruledSurfaceBoundsXY(surface);
        out.min = out.min.cwiseMin(b.min);
        out.max = out.max.cwiseMax(b.max);
    }
    if (!std::isfinite(out.min.x())) out.min.setZero(), out.max.setZero();
    return out;
}

std::vector<RuledSurface> makeProceduralRuledSurfaces(
    const RuledSurfaceProceduralSettings& settings) {
    const int count = std::max(0, settings.surfaceCount);
    const int segments = std::max(1, settings.planesPerSurface);
    std::mt19937 rng(settings.seed);
    std::uniform_real_distribution<double> unit(-1.0, 1.0);
    std::vector<RuledSurface> result;
    result.reserve(count);

    for (int s = 0; s < count; ++s) {
        const double length = settings.baseLength *
            (1.0 + 0.45 * settings.lengthVariation * unit(rng));
        const double width = settings.width *
            (1.0 + 0.25 * settings.randomVariation * unit(rng));
        const double baseAngle = settings.rulingTurn * unit(rng);
        const double xShift = 0.35 * settings.randomVariation * settings.baseLength * unit(rng);
        const double yShift = 0.35 * settings.randomVariation * settings.baseLength * unit(rng);
        std::vector<RuledSurfaceRuling> rulings;
        rulings.reserve(segments + 1);
        double z = 0.0;
        for (int k = 0; k <= segments; ++k) {
            const double t = static_cast<double>(k) / segments;
            const double angle = baseAngle + settings.rulingTurn * 0.65 * (t - 0.5) +
                                 0.20 * settings.randomVariation * unit(rng);
            const Eigen::Vector2d longitudinal(std::cos(angle), std::sin(angle));
            const Eigen::Vector2d across(-longitudinal.y(), longitudinal.x());
            const double along = (t - 0.5) * length;
            if (k > 0) {
                z += settings.baseRise / segments *
                     (1.0 + settings.randomVariation * 0.7 * unit(rng));
            }
            const double slope = 0.30 * settings.randomVariation * unit(rng);
            const Eigen::Vector2d center = longitudinal * along + Eigen::Vector2d(xShift, yShift);
            RuledSurfaceRuling r;
            r.a = Eigen::Vector3d(center.x() - 0.5 * width * across.x(),
                                  center.y() - 0.5 * width * across.y(), z - slope * width * 0.5);
            r.b = Eigen::Vector3d(center.x() + 0.5 * width * across.x(),
                                  center.y() + 0.5 * width * across.y(), z + slope * width * 0.5);
            rulings.push_back(r);
        }
        RuledSurface surface = makeRuledSurface(rulings);
        // Recency in XY avoids arbitrary family offsets while preserving shape.
        const auto bounds = ruledSurfaceBoundsXY(surface);
        const Eigen::Vector2d center = 0.5 * (bounds.min + bounds.max);
        for (auto& r : surface.rulings) {
            r.a.x() -= center.x(); r.a.y() -= center.y();
            r.b.x() -= center.x(); r.b.y() -= center.y();
        }
        result.push_back(makeRuledSurface(surface.rulings));
    }
    return result;
}

} // namespace alice2::stack

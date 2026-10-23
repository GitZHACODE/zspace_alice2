#include "RuledSurfaceStackSolver.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
#include <string>
#include <unordered_map>

namespace alice2::stack {
namespace {
constexpr double kNegInf = -1e300;
bool reachable(double value) { return value > kNegInf / 2.0; }

struct MinimumPlacement {
    std::vector<double> z;
    double height = 0.0;
};
struct StackConflict {
    bool valid = false;
    int i = -1, j = -1, intervalIndex = -1;
    double delta = 0.0, lo = 0.0, hi = 0.0, cheapScore = 0.0;
};
struct BranchProbe {
    bool feasible = false;
    MaxPlusClosure closure;
    MinimumPlacement placement;
};

const OrientedStackSurface* oriented(const SurfaceOrientationVariants& v, bool flip) {
    return flip && v.flipped ? &*v.flipped : (!flip ? &v.normal : nullptr);
}
const ForbiddenIntervalSet& forbidden(const std::vector<PairConstraintData>& pairs, int n,
                                      int i, int j, const std::vector<bool>& flips, bool hotWire) {
    const PairConstraintData& pair = pairs[pairIndex(i, j, n)];
    return hotWire ? pair.hotWire[flips[i]][flips[j]] : pair.finite[flips[i]][flips[j]];
}

MinimumPlacement computeMinimumPlacement(const MaxPlusClosure& closure,
                                         const std::vector<double>& minZ,
                                         const std::vector<double>& maxZ) {
    const int n = closure.size();
    MinimumPlacement out; out.z.assign(n, kNegInf);
    for (int j = 0; j < n; ++j) {
        double best = -minZ[j];
        for (int i = 0; i < n; ++i)
            if (reachable(closure.at(i, j))) best = std::max(best, -minZ[i] + closure.at(i, j));
        out.z[j] = best;
        out.height = std::max(out.height, best + maxZ[j]);
    }
    return out;
}

std::vector<StackConflict> findConflicts(const std::vector<PairConstraintData>& pairs, int n,
                                         const std::vector<bool>& flips, bool hotWire,
                                         const std::vector<double>& z, double eps,
                                         StackSolveStats& stats) {
    std::vector<StackConflict> out;
    for (int i = 0; i < n; ++i) for (int j = i + 1; j < n; ++j) {
        const auto& set = forbidden(pairs, n, i, j, flips, hotWire);
        const double delta = z[j] - z[i];
        ++stats.conflictsTested;
        const int index = set.findContainingInterval(delta, eps);
        if (index < 0) continue;
        const auto interval = set.intervals[index];
        const double left = delta - interval.lo;
        const double right = interval.hi - delta;
        out.push_back({true, i, j, index, delta, interval.lo, interval.hi,
                       std::min(left, right) + 0.1 * (interval.hi - interval.lo)});
    }
    return out;
}

BranchProbe probeEdge(const MaxPlusClosure& parent, int u, int v, double w, double eps,
                      const std::vector<double>& minZ, const std::vector<double>& maxZ,
                      StackSolveStats& stats) {
    BranchProbe probe; probe.closure = parent;
    if (!probe.closure.addEdge(u, v, w, eps)) { ++stats.cyclePrunes; return probe; }
    probe.feasible = true;
    probe.placement = computeMinimumPlacement(probe.closure, minZ, maxZ);
    return probe;
}

std::string flipKey(const std::vector<bool>& flips) {
    std::string key; key.reserve(flips.size());
    for (bool value : flips) key.push_back(value ? '1' : '0');
    return key;
}
} // namespace

MaxPlusClosure::MaxPlusClosure(int count) : m_D(Eigen::MatrixXd::Constant(count, count, kNegInf)) {
    for (int i = 0; i < count; ++i) m_D(i, i) = 0.0;
}
int MaxPlusClosure::size() const { return static_cast<int>(m_D.rows()); }
double MaxPlusClosure::at(int a, int b) const { return m_D(a, b); }
const Eigen::MatrixXd& MaxPlusClosure::matrix() const { return m_D; }
bool MaxPlusClosure::addEdge(int u, int v, double weight, double epsilon) {
    if (weight <= m_D(u, v) + epsilon) return true;
    if (reachable(m_D(v, u)) && m_D(v, u) + weight > epsilon) return false;
    const Eigen::MatrixXd old = m_D;
    for (int a = 0; a < size(); ++a) {
        if (!reachable(old(a, u))) continue;
        for (int b = 0; b < size(); ++b) {
            if (!reachable(old(v, b))) continue;
            m_D(a, b) = std::max(m_D(a, b), old(a, u) + weight + old(v, b));
        }
    }
    return true;
}

RuledSurfaceStackSolution solveFixedOrientationStackExact(
    const std::vector<SurfaceOrientationVariants>& surfaces,
    const std::vector<PairConstraintData>& pairs,
    const std::vector<bool>& flips,
    bool useHotWireCollision,
    const StackSolveSettings& settings,
    StackSolveStats* outputStats) {
    StackSolveStats stats;
    RuledSurfaceStackSolution best;
    const int n = static_cast<int>(surfaces.size());
    if (static_cast<int>(flips.size()) != n || pairs.size() != static_cast<size_t>(n) * (n - 1) / 2) {
        if (outputStats) *outputStats = stats;
        return best;
    }
    std::vector<double> minZ(n), maxZ(n);
    for (int i = 0; i < n; ++i) {
        const auto* surface = oriented(surfaces[i], flips[i]);
        if (!surface) { if (outputStats) *outputStats = stats; return best; }
        minZ[i] = surface->localMinZ; maxZ[i] = surface->localMaxZ;
    }
    if (n == 0) {
        best.feasible = best.exactForOrientationState = true;
        best.flippedBySurface = flips;
        if (outputStats) *outputStats = stats;
        return best;
    }

    auto record = [&](const MinimumPlacement& placement) {
        if (!best.feasible || placement.height < best.totalHeight - settings.numericalEpsilon) {
            best.feasible = true; best.exactForOrientationState = true;
            best.totalHeight = placement.height; best.zBySurface = placement.z; best.flippedBySurface = flips;
        }
    };

    // A deterministic greedy pass supplies a useful incumbent without inventing a stack order.
    MaxPlusClosure greedy(n);
    for (;;) {
        const MinimumPlacement placement = computeMinimumPlacement(greedy, minZ, maxZ);
        auto conflicts = findConflicts(pairs, n, flips, useHotWireCollision, placement.z,
                                      settings.numericalEpsilon, stats);
        if (conflicts.empty()) { record(placement); break; }
        const auto conflict = *std::max_element(conflicts.begin(), conflicts.end(),
            [](const StackConflict& a, const StackConflict& b) { return a.cheapScore < b.cheapScore; });
        BranchProbe left = probeEdge(greedy, conflict.j, conflict.i, -conflict.lo,
                                     settings.numericalEpsilon, minZ, maxZ, stats);
        BranchProbe right = probeEdge(greedy, conflict.i, conflict.j, conflict.hi,
                                      settings.numericalEpsilon, minZ, maxZ, stats);
        if (!left.feasible && !right.feasible) break;
        if (!right.feasible || (left.feasible && left.placement.height <= right.placement.height)) greedy = std::move(left.closure);
        else greedy = std::move(right.closure);
    }

    std::function<void(const MaxPlusClosure&)> search;
    search = [&](const MaxPlusClosure& closure) {
        if (settings.maxSearchNodes > 0 && stats.nodesVisited >= static_cast<std::uint64_t>(settings.maxSearchNodes)) {
            stats.searchLimitReached = true;
            return;
        }
        ++stats.nodesVisited;
        const MinimumPlacement placement = computeMinimumPlacement(closure, minZ, maxZ);
        if (best.feasible && placement.height >= best.totalHeight - settings.numericalEpsilon) {
            ++stats.boundPrunes; return;
        }
        auto conflicts = findConflicts(pairs, n, flips, useHotWireCollision, placement.z,
                                      settings.numericalEpsilon, stats);
        if (conflicts.empty()) { ++stats.feasibleLeaves; record(placement); return; }
        std::sort(conflicts.begin(), conflicts.end(), [](const StackConflict& a, const StackConflict& b) {
            return a.cheapScore > b.cheapScore;
        });
        const int candidates = settings.enableStrongBranching
            ? std::min<int>(std::max(1, settings.strongBranchCandidateCount), conflicts.size()) : 1;
        StackConflict selected = conflicts.front();
        BranchProbe selectedLeft, selectedRight;
        double bestScore = -std::numeric_limits<double>::infinity();
        for (int k = 0; k < candidates; ++k) {
            const auto& c = conflicts[k];
            BranchProbe left = probeEdge(closure, c.j, c.i, -c.lo, settings.numericalEpsilon, minZ, maxZ, stats);
            BranchProbe right = probeEdge(closure, c.i, c.j, c.hi, settings.numericalEpsilon, minZ, maxZ, stats);
            stats.strongBranchProbes += 2;
            const double score = std::min(left.feasible ? left.placement.height : std::numeric_limits<double>::infinity(),
                                          right.feasible ? right.placement.height : std::numeric_limits<double>::infinity());
            if (score > bestScore) { bestScore = score; selected = c; selectedLeft = std::move(left); selectedRight = std::move(right); }
        }
        // Strong-branch probes are complete children already; visit the better lower bound first.
        BranchProbe* first = &selectedLeft; BranchProbe* second = &selectedRight;
        if (!first->feasible || (second->feasible && second->placement.height < first->placement.height)) std::swap(first, second);
        if (first->feasible) search(first->closure);
        if (second->feasible) search(second->closure);
    };
    search(MaxPlusClosure(n));
    if (stats.searchLimitReached) best.exactForOrientationState = false;
    best.searchNodes = stats.nodesVisited; best.boundPrunes = stats.boundPrunes; best.positiveCyclePrunes = stats.cyclePrunes;
    if (outputStats) *outputStats = stats;
    return best;
}

RuledSurfaceStackSolution solveRuledSurfaceStackFast(
    const std::vector<SurfaceOrientationVariants>& surfaces,
    const std::vector<PairConstraintData>& pairs,
    const StackSolveSettings& settings,
    StackSolveStats* outputStats) {
    StackSolveStats totalStats;
    const int n = static_cast<int>(surfaces.size());
    std::unordered_map<std::string, RuledSurfaceStackSolution> cache;
    auto evaluate = [&](const std::vector<bool>& flips) -> const RuledSurfaceStackSolution& {
        const std::string key = flipKey(flips);
        if (auto it = cache.find(key); it != cache.end()) return it->second;
        StackSolveStats local;
        auto solution = solveFixedOrientationStackExact(surfaces, pairs, flips, settings.useHotWireCollision, settings, &local);
        totalStats.nodesVisited += local.nodesVisited; totalStats.boundPrunes += local.boundPrunes;
        totalStats.cyclePrunes += local.cyclePrunes; totalStats.feasibleLeaves += local.feasibleLeaves;
        totalStats.conflictsTested += local.conflictsTested; totalStats.strongBranchProbes += local.strongBranchProbes;
        totalStats.searchLimitReached = totalStats.searchLimitReached || local.searchLimitReached;
        return cache.emplace(key, std::move(solution)).first->second;
    };
    std::vector<bool> current(n, false);
    RuledSurfaceStackSolution best = evaluate(current);
    if (!settings.optimiseFlips) { if (outputStats) *outputStats = totalStats; return best; }

    auto improves = [&](const RuledSurfaceStackSolution& candidate) {
        return candidate.feasible && (!best.feasible || candidate.totalHeight < best.totalHeight - settings.numericalEpsilon);
    };
    bool changed = true;
    while (changed) {
        changed = false;
        std::vector<bool> bestFlips = current;
        for (int i = 0; i < n; ++i) {
            if (!surfaces[i].flipped) continue;
            auto trial = current; trial[i] = !trial[i];
            const auto& candidate = evaluate(trial);
            if (improves(candidate)) { best = candidate; bestFlips = std::move(trial); changed = true; }
        }
        if (changed) { current = std::move(bestFlips); continue; }
        // Pair neighbourhood: rank by cached interval width, a deterministic collision-pressure proxy.
        std::vector<double> pressure(n, 0.0);
        for (const auto& pair : pairs) {
            const auto& set = settings.useHotWireCollision ? pair.hotWire[current[pair.i]][current[pair.j]]
                                                           : pair.finite[current[pair.i]][current[pair.j]];
            for (const auto& interval : set.intervals) pressure[pair.i] += interval.hi - interval.lo, pressure[pair.j] += interval.hi - interval.lo;
        }
        std::vector<int> ranked(n); std::iota(ranked.begin(), ranked.end(), 0);
        std::sort(ranked.begin(), ranked.end(), [&](int a, int b) { return pressure[a] > pressure[b]; });
        ranked.resize(std::min<int>(settings.flipPairCandidateCount, ranked.size()));
        for (size_t a = 0; a < ranked.size(); ++a) for (size_t b = a + 1; b < ranked.size(); ++b) {
            const int i = ranked[a], j = ranked[b];
            if (!surfaces[i].flipped || !surfaces[j].flipped) continue;
            auto trial = current; trial[i] = !trial[i]; trial[j] = !trial[j];
            const auto& candidate = evaluate(trial);
            if (improves(candidate)) { best = candidate; bestFlips = std::move(trial); changed = true; }
        }
        if (changed) current = std::move(bestFlips);
    }
    if (outputStats) *outputStats = totalStats;
    return best;
}

} // namespace alice2::stack

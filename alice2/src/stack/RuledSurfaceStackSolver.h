#pragma once

#include "RuledSurfaceStackIntervals.h"

#include <Eigen/Core>

#include <cstdint>
#include <vector>

namespace alice2::stack {

class MaxPlusClosure {
public:
    explicit MaxPlusClosure(int count = 0);
    int size() const;
    double at(int a, int b) const;
    bool addEdge(int u, int v, double weight, double epsilon);
    const Eigen::MatrixXd& matrix() const;

private:
    Eigen::MatrixXd m_D;
};

struct StackSolveSettings {
    double numericalEpsilon = 1e-9;
    bool enableStrongBranching = true;
    int strongBranchCandidateCount = 8;
    bool optimiseFlips = false;
    bool useHotWireCollision = true;
    int flipPairCandidateCount = 12;
    // Zero keeps the exact search unbounded. A positive value returns the
    // best feasible placement found once this many nodes have been visited.
    int maxSearchNodes = 0;
};

struct StackSolveStats {
    std::uint64_t nodesVisited = 0;
    std::uint64_t boundPrunes = 0;
    std::uint64_t cyclePrunes = 0;
    std::uint64_t feasibleLeaves = 0;
    std::uint64_t conflictsTested = 0;
    std::uint64_t strongBranchProbes = 0;
    bool searchLimitReached = false;
};

struct RuledSurfaceStackSolution {
    std::vector<double> zBySurface;
    std::vector<bool> flippedBySurface;
    double totalHeight = 0.0;
    bool feasible = false;
    bool exactForOrientationState = false;
    std::uint64_t searchNodes = 0;
    std::uint64_t boundPrunes = 0;
    std::uint64_t positiveCyclePrunes = 0;
};

RuledSurfaceStackSolution solveFixedOrientationStackExact(
    const std::vector<SurfaceOrientationVariants>& surfaces,
    const std::vector<PairConstraintData>& pairs,
    const std::vector<bool>& flips,
    bool useHotWireCollision,
    const StackSolveSettings& settings,
    StackSolveStats* stats = nullptr);

RuledSurfaceStackSolution solveRuledSurfaceStackFast(
    const std::vector<SurfaceOrientationVariants>& surfaces,
    const std::vector<PairConstraintData>& pairs,
    const StackSolveSettings& settings,
    StackSolveStats* stats = nullptr);

} // namespace alice2::stack

#pragma once

#include "TnaSolver.h"
#include <Eigen/SparseCore>
#include <stop_token>

namespace alice2 {

struct TnaEquilibriumReport {
    bool finite{false};
    std::vector<float> loads;
    // p - A*q at every vertex. Constrained components are reactions with
    // opposite sign; only unconstrained components enter the norms below.
    std::vector<Vec3> residuals;
    std::vector<Vec3> reactions;
    double horizontalResidual{0.0};
    double verticalResidual{0.0};
    double residual{0.0};
    double rmsDesignDistance{0.0};
    double maximumDesignDistance{0.0};
};

// Common load and residual convention for both stages; compression q > 0.
TnaEquilibriumReport measureTnaEquilibrium(
    const MeshData& mesh, const MeshData& reference, const std::vector<TnaEdge>& edges,
    const std::vector<float>& densities, const std::vector<int>& fixedXYZ,
    const TnaVerticalSettings& loads,
    bool allowSlidingTensionSupports = true,
    double slidingSupportPenalty = 5.0);

std::vector<int> slidingSupports(const std::vector<int>& supports,
                                 const std::vector<TnaEdge>& edges,
                                 int count,
                                 bool enabled = true);

struct BestFitForceDensitySettings {
    TnaVerticalSettings loads;
    int maximumIterations{50};
    double designAttraction{1.0};
    double minimumDensityMagnitude{1e-4};
    double displacementTolerance{1e-5};
    std::vector<int> nearZeroEdges;
    bool allowSlidingTensionSupports{true};
    double slidingSupportPenalty{5.0};
    // Dense reference mode is retained for regression comparisons.
    bool sparseDensityFit{true};
};

enum class BestFitStatus { Idle, Running, Converged, Stagnated, IterationLimit, Failed, Cancelled };

struct BestFitForceDensityResult {
    // A valid, finite equilibrium candidate exists, including after stagnation.
    bool success{false};
    BestFitStatus status{BestFitStatus::Idle};
    std::string diagnostic;
    MeshData mesh;
    std::vector<TnaEdge> edges;
    std::vector<float> forceDensities;
    TnaEquilibriumReport report;
    int iteration{0};
    // Cumulative timings, excluding display/force-diagram construction.
    double densityFitSeconds{0.0};
    double equilibriumSeconds{0.0};
    int equilibriumTrials{0};
    int densityFallbacks{0};
};

class BestFitForceDensitySolver {
public:
    bool reset(const TnaFormDiagram& design, const BestFitForceDensitySettings& settings);
    // One accepted outer update (with bounded backtracking) per call.
    void step(std::stop_token stop = {});
    const BestFitForceDensityResult& result() const { return m_result; }
    const TnaFormDiagram& design() const { return m_design; }
    const BestFitForceDensitySettings& settings() const { return m_settings; }
private:
    TnaFormDiagram m_design;
    BestFitForceDensitySettings m_settings;
    BestFitForceDensityResult m_result;
    Eigen::SparseMatrix<double> m_referenceEquilibrium;
    std::vector<int> m_slidingSupports;
    std::vector<bool> m_nearZeroMask;
};

} // namespace alice2

#pragma once

#include "TnaSolver.h"

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
    const TnaVerticalSettings& loads);

struct BestFitForceDensitySettings {
    TnaVerticalSettings loads;
    int maximumIterations{50};
    double designAttraction{1.0};
    double minimumDensityMagnitude{1e-4};
    double displacementTolerance{1e-5};
};

enum class BestFitStatus { Idle, Running, Converged, Stagnated, IterationLimit, Failed };

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
};

class BestFitForceDensitySolver {
public:
    bool reset(const TnaFormDiagram& design, const BestFitForceDensitySettings& settings);
    // One accepted outer update (with bounded backtracking) per call.
    void step();
    const BestFitForceDensityResult& result() const { return m_result; }
    const TnaFormDiagram& design() const { return m_design; }
    const BestFitForceDensitySettings& settings() const { return m_settings; }
private:
    TnaFormDiagram m_design;
    BestFitForceDensitySettings m_settings;
    BestFitForceDensityResult m_result;
};

} // namespace alice2

#pragma once

#include <Eigen/Core>
#include <Eigen/SparseCore>
#include <string>
#include <vector>

namespace alice2::fdm {

enum class MemberType { Tension, Compression };
// Public density convention: compression > 0, tension < 0.
// Internal equilibrium uses -q: L(q) = -C^T diag(q) C and L(q) X = P.

struct Node {
    Eigen::Vector3d x0 = Eigen::Vector3d::Zero();
    Eigen::Vector3d load = Eigen::Vector3d::Zero();
    bool fixed[3] = {false, false, false};
};

struct Edge {
    int i = 0, j = 0;
    MemberType type = MemberType::Tension;
    double targetDensityMagnitude = 1.0;
};

struct StructuralGraph {
    std::vector<Node> nodes;
    std::vector<Edge> edges;
};

struct SolverOptions {
    int maxIterations = 100;
    int maxBacktracks = 12;
    double projection = 0.15;
    double minDensity = 1e-3;
    double maxDensity = 1e3;
    double regularization = 1e-3;
    double trustFraction = 0.25;
    double supportHorizontalWeight = 0; // 0 disables balancing; 1..10 sets shape allowance
    double forceTolerance = 1e-8;
    double shapeTolerance = 1e-7;
};

struct IterationRecord {
    double shapeError = 0, forceResidual = 0, projectedResidual = 0;
    double horizontalSupportForce = 0;
    double densityStep = 0;
    int rejectedTrials = 0;
};

struct SolverResult {
    Eigen::MatrixXd X;
    Eigen::VectorXd q;
    Eigen::MatrixXd residuals;
    std::vector<IterationRecord> history;
    std::string reason;
    bool equilibriumValidated = false;
    double fixedGeometryResidual = 0;
};

// Rows are ordered by node, then x/y/z, omitting fixed coordinates.
Eigen::SparseMatrix<double> equilibriumMatrix(const StructuralGraph&, const Eigen::MatrixXd& X);
Eigen::SparseMatrix<double> forceDensityMatrix(const StructuralGraph&, const Eigen::VectorXd& q);
Eigen::VectorXd freeLoads(const StructuralGraph&);
Eigen::VectorXd freeResidual(const StructuralGraph&, const Eigen::MatrixXd& X, const Eigen::VectorXd& q);
SolverResult solveProjectedMixedFDM(const StructuralGraph&, const SolverOptions& = {});
int runProjectedMixedFDMChecks();

} // namespace alice2::fdm

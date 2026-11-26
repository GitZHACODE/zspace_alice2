#pragma once

#include <geometry/MeshData.h>
#include <Eigen/Core>
#include <Eigen/SparseCholesky>
#include <Eigen/SparseLU>
#include <array>
#include <vector>

namespace alice2::differential {

struct HodgeResult {
    Eigen::VectorXd potential, exact, coexact, harmonic;
};

// The unrestricted cochain complex of an orientable triangle surface.
// Edges are oriented from the smaller vertex index to the larger one.
class DiscreteExteriorCalculus {
public:
    using Sparse = Eigen::SparseMatrix<double>;
    using Vector = Eigen::VectorXd;
    using Edge = std::array<int, 2>;

    void build(const MeshData& mesh);
    void updateMetric(const MeshData& mesh);

    const Sparse& D0() const { return d0_; }
    const Sparse& D1() const { return d1_; }
    const Sparse& H0() const { return h0_; }
    const Sparse& H1() const { return h1_; }
    const Sparse& H2() const { return h2_; }
    const Sparse& laplacian0() const { return stiffness_; }
    const std::vector<Edge>& edges() const { return edges_; }
    int componentCount() const { return static_cast<int>(vertexRoots_.size()); }
    int boundaryLoopCount() const { return boundaryLoops_; }
    int harmonicModeCount() const { return static_cast<int>(generators_.size()); }
    int totalGenus() const {
        const int openComponents = componentCount() - static_cast<int>(faces_.size()) + faceRowCount_;
        return (harmonicModeCount() + openComponents - boundaryLoops_) / 2;
    }

    Vector derivative0(const Vector& scalar) const;
    Vector derivative1(const Vector& form) const;
    Vector codifferential1(const Vector& form) const;
    Vector laplaceScalar(const Vector& scalar) const;
    Vector laplaceForm(const Vector& form);
    double edgeNorm(const Vector& form) const;

    // Unconstrained components receive a zero gauge at one vertex.
    Vector harmonicField(const std::vector<int>& fixedIds, const Vector& fixedValues) const;
    Vector integrate(const Vector& form);
    HodgeResult decompose(const Vector& form);
    // H1-orthonormal representatives. Compute only when explicitly requested.
    Eigen::MatrixXd harmonicBasis();

    Vector projectFaceField(const Eigen::MatrixXd& vectors);
    Eigen::MatrixXd reconstructFaceField(const Vector& form) const;

private:
    struct Face {
        std::array<int, 3> vertices, edges, signs;
    };
    struct DualLink {
        int child, edge;
    };

    void buildTopology(const MeshData& mesh);
    Vector solveScalar(const Vector& rhs, const std::vector<int>& fixedIds,
                       const Vector& fixedValues, bool reuseGauge) const;
    Vector solveMass(const Vector& rhs);
    Vector solveCoexact(const Vector& circulation);

    Sparse d0_, d1_, h0_, h1_, h2_, stiffness_;
    Vector vertexMass_, faceMass_;
    std::vector<Edge> edges_;
    std::vector<Face> faces_;
    std::vector<int> vertexComponents_, vertexRoots_, faceRows_, generators_;
    std::vector<DualLink> dualLinks_;
    std::vector<Eigen::Matrix3d> midpointBasis_;
    int boundaryLoops_ = 0;
    int faceRowCount_ = 0;
    mutable bool scalarReady_ = false;
    bool massReady_ = false, coexactReady_ = false;
    mutable Eigen::SimplicialLDLT<Sparse> scalarSolver_;
    Eigen::SimplicialLDLT<Sparse> massSolver_;
    Eigen::SparseLU<Sparse> coexactSolver_;
};

} // namespace alice2::differential

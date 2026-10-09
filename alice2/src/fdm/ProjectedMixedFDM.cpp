#include "ProjectedMixedFDM.h"

#include <Eigen/SparseLU>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <set>

namespace alice2::fdm {
namespace {
using Sparse = Eigen::SparseMatrix<double>;
using Triplet = Eigen::Triplet<double>;

int freeCount(const StructuralGraph& g) {
    int count = 0;
    for (const auto& n : g.nodes) for (int d = 0; d < 3; ++d) count += !n.fixed[d];
    return count;
}

Eigen::MatrixXi dofRows(const StructuralGraph& g) {
    Eigen::MatrixXi rows(g.nodes.size(), 3);
    int next = 0;
    for (int i = 0; i < rows.rows(); ++i)
        for (int d = 0; d < 3; ++d) rows(i, d) = g.nodes[i].fixed[d] ? -1 : next++;
    return rows;
}

Eigen::MatrixXi supportRows(const StructuralGraph& g) {
    Eigen::MatrixXi rows = Eigen::MatrixXi::Constant(g.nodes.size(), 3, -1);
    int next = 0;
    for (int i = 0; i < rows.rows(); ++i)
        for (int d = 0; d < 2; ++d) if (g.nodes[i].fixed[d]) rows(i, d) = next++;
    return rows;
}

Sparse horizontalSupportMatrix(const StructuralGraph& g, const Eigen::MatrixXd& X) {
    const auto rows = supportRows(g);
    std::vector<Triplet> terms;
    for (int e = 0; e < int(g.edges.size()); ++e) {
        const auto& edge = g.edges[e];
        for (int d = 0; d < 2; ++d) {
            const double delta = X(edge.j, d) - X(edge.i, d);
            if (rows(edge.i, d) >= 0) terms.emplace_back(rows(edge.i, d), e, delta);
            if (rows(edge.j, d) >= 0) terms.emplace_back(rows(edge.j, d), e, -delta);
        }
    }
    Sparse H((rows.array() >= 0).count(), g.edges.size());
    H.setFromTriplets(terms.begin(), terms.end());
    return H;
}

Eigen::VectorXd horizontalSupportLoads(const StructuralGraph& g) {
    int count = 0;
    for (const auto& n : g.nodes) for (int d = 0; d < 2; ++d) count += n.fixed[d];
    Eigen::VectorXd p(count); int row = 0;
    for (const auto& n : g.nodes) for (int d = 0; d < 2; ++d)
        if (n.fixed[d]) p[row++] = n.load[d];
    return p;
}

double horizontalSupportNorm(const StructuralGraph& g, const Eigen::MatrixXd& X,
                             const Eigen::VectorXd& q, const Eigen::VectorXd& p) {
    return (p - horizontalSupportMatrix(g, X) * q).norm();
}

Sparse stackedCorrectionMatrix(const Sparse& A, const Sparse& H, double weight) {
    std::vector<Triplet> terms;
    terms.reserve(A.nonZeros() + H.nonZeros());
    for (int c = 0; c < A.outerSize(); ++c)
        for (Sparse::InnerIterator it(A, c); it; ++it) terms.emplace_back(it.row(), it.col(), it.value());
    const double scale = std::sqrt(weight);
    for (int c = 0; c < H.outerSize(); ++c)
        for (Sparse::InnerIterator it(H, c); it; ++it)
            terms.emplace_back(A.rows() + it.row(), it.col(), scale * it.value());
    Sparse B(A.rows() + H.rows(), A.cols()); B.setFromTriplets(terms.begin(), terms.end());
    return B;
}

bool valid(const StructuralGraph& g, const SolverOptions& o, std::string& reason) {
    if (g.nodes.empty() || g.edges.empty() || !std::isfinite(o.minDensity) || !std::isfinite(o.maxDensity) ||
        !std::isfinite(o.projection) || !std::isfinite(o.regularization) ||
        !std::isfinite(o.trustFraction) || !std::isfinite(o.supportHorizontalWeight) ||
        !std::isfinite(o.forceTolerance) ||
        !std::isfinite(o.shapeTolerance) || o.minDensity <= 0 || o.maxDensity < o.minDensity ||
        o.projection <= 0 || o.projection > 1 || o.regularization <= 0 || o.trustFraction <= 0 ||
        o.forceTolerance <= 0 || o.shapeTolerance <= 0 || o.supportHorizontalWeight < 0 ||
        o.maxIterations < 0 || o.maxBacktracks < 1) { reason = "invalid options or empty graph"; return false; }
    for (const auto& n : g.nodes) if (!n.x0.allFinite() || !n.load.allFinite()) {
        reason = "nonfinite node"; return false;
    }
    std::set<std::pair<int, int>> unique;
    std::vector<std::vector<int>> adjacency(g.nodes.size());
    for (const auto& e : g.edges) {
        if (e.i < 0 || e.j < 0 || e.i >= int(g.nodes.size()) || e.j >= int(g.nodes.size()) ||
            e.i == e.j || !std::isfinite(e.targetDensityMagnitude) || e.targetDensityMagnitude <= 0) {
            reason = "invalid edge"; return false;
        }
        if (!unique.insert(std::minmax(e.i, e.j)).second ||
            (g.nodes[e.i].x0 - g.nodes[e.j].x0).norm() < 1e-10) {
            reason = "duplicate or collapsed input edge"; return false;
        }
        adjacency[e.i].push_back(e.j); adjacency[e.j].push_back(e.i);
    }
    std::vector<bool> seen(g.nodes.size(), false);
    for (int seed = 0; seed < int(g.nodes.size()); ++seed) {
        if (seen[seed]) continue;
        bool hasFixed[3] = {false, false, false};
        std::vector<int> stack{seed}; seen[seed] = true;
        while (!stack.empty()) {
            int i = stack.back(); stack.pop_back();
            for (int d = 0; d < 3; ++d) hasFixed[d] |= g.nodes[i].fixed[d];
            for (int j : adjacency[i]) if (!seen[j]) { seen[j] = true; stack.push_back(j); }
        }
        if (!hasFixed[0] || !hasFixed[1] || !hasFixed[2]) {
            reason = "component lacks a support in at least one coordinate"; return false;
        }
    }
    return true;
}

Eigen::MatrixXd inputPositions(const StructuralGraph& g) {
    Eigen::MatrixXd X(g.nodes.size(), 3);
    for (int i = 0; i < X.rows(); ++i) X.row(i) = g.nodes[i].x0.transpose();
    return X;
}

double shapeError(const Eigen::MatrixXd& X, const Eigen::MatrixXd& X0) {
    return (X - X0).squaredNorm();
}

bool recover(const StructuralGraph& g, const Eigen::VectorXd& q, Eigen::MatrixXd& X) {
    X = inputPositions(g);
    const int n = int(g.nodes.size());
    for (int d = 0; d < 3; ++d) {
        std::vector<int> row(n, -1);
        int count = 0;
        for (int i = 0; i < n; ++i) if (!g.nodes[i].fixed[d]) row[i] = count++;
        if (!count) continue;
        std::vector<Triplet> terms;
        Eigen::VectorXd rhs(count);
        for (int i = 0; i < n; ++i) if (row[i] >= 0) rhs[row[i]] = g.nodes[i].load[d];
        for (int e = 0; e < q.size(); ++e) {
            const int i = g.edges[e].i, j = g.edges[e].j;
            const double v = -q[e];
            if (row[i] >= 0) {
                terms.emplace_back(row[i], row[i], v);
                if (row[j] >= 0) terms.emplace_back(row[i], row[j], -v);
                else rhs[row[i]] += v * X(j, d);
            }
            if (row[j] >= 0) {
                terms.emplace_back(row[j], row[j], v);
                if (row[i] >= 0) terms.emplace_back(row[j], row[i], -v);
                else rhs[row[j]] += v * X(i, d);
            }
        }
        Sparse M(count, count); M.setFromTriplets(terms.begin(), terms.end()); M.makeCompressed();
        Eigen::SparseLU<Sparse> lu;
        lu.analyzePattern(M); lu.factorize(M);
        if (lu.info() != Eigen::Success) return false;
        Eigen::VectorXd solved = lu.solve(rhs);
        if (lu.info() != Eigen::Success || !solved.allFinite() ||
            (M * solved - rhs).norm() > 1e-9 * std::max(1.0, rhs.norm())) return false;
        for (int i = 0; i < n; ++i) if (row[i] >= 0) X(i, d) = solved[row[i]];
    }
    if (!X.allFinite()) return false;
    for (const auto& e : g.edges) if ((X.row(e.i) - X.row(e.j)).norm() < 1e-10) return false;
    return true;
}

Eigen::VectorXd boundedFit(const Sparse& A, const Eigen::VectorXd& p,
                           const StructuralGraph& g, const Eigen::VectorXd& center,
                           const SolverOptions& o, bool trust, bool anchor) {
    Eigen::VectorXd lo(center.size()), hi(center.size());
    for (int e = 0; e < center.size(); ++e) {
        const bool compression = g.edges[e].type == MemberType::Compression;
        lo[e] = compression ? o.minDensity : -o.maxDensity;
        hi[e] = compression ? o.maxDensity : -o.minDensity;
        if (trust) {
            double radius = o.trustFraction * std::max(std::abs(center[e]), o.minDensity);
            lo[e] = std::max(lo[e], center[e] - radius);
            hi[e] = std::min(hi[e], center[e] + radius);
        }
    }
    if (anchor) lo[0] = hi[0] = center[0]; // explicit zero-load force scale
    const double lambda = o.regularization;
    Eigen::VectorXd v = Eigen::VectorXd::Ones(center.size()); v.normalize();
    for (int k = 0; k < 20; ++k) {
        Eigen::VectorXd w = A.transpose() * (A * v) + lambda * v;
        if (w.norm() == 0) break;
        v = w.normalized();
    }
    const double L = std::max(lambda, (A * v).squaredNorm() + lambda) * 1.01;
    Eigen::VectorXd q = center.cwiseMax(lo).cwiseMin(hi);
    Eigen::VectorXd y = q;
    double t = 1;
    for (int k = 0; k < 300; ++k) {
        Eigen::VectorXd gradient = A.transpose() * (A * y - p) + lambda * (y - center);
        Eigen::VectorXd next = (y - gradient / L).cwiseMax(lo).cwiseMin(hi);
        if ((next - q).norm() < 1e-10 * std::max(1.0, q.norm())) { q = next; break; }
        double nextT = (1 + std::sqrt(1 + 4 * t * t)) / 2;
        y = next + ((t - 1) / nextT) * (next - q);
        q = next; t = nextT;
    }
    return q;
}
} // namespace

Eigen::SparseMatrix<double> equilibriumMatrix(const StructuralGraph& g, const Eigen::MatrixXd& X) {
    const auto rows = dofRows(g);
    std::vector<Triplet> terms;
    terms.reserve(g.edges.size() * 6);
    for (int e = 0; e < int(g.edges.size()); ++e) {
        const auto& edge = g.edges[e];
        for (int d = 0; d < 3; ++d) {
            double delta = X(edge.j, d) - X(edge.i, d);
            if (rows(edge.i, d) >= 0) terms.emplace_back(rows(edge.i, d), e, delta);
            if (rows(edge.j, d) >= 0) terms.emplace_back(rows(edge.j, d), e, -delta);
        }
    }
    Sparse A(freeCount(g), g.edges.size()); A.setFromTriplets(terms.begin(), terms.end()); return A;
}

Eigen::SparseMatrix<double> forceDensityMatrix(const StructuralGraph& g, const Eigen::VectorXd& q) {
    std::vector<Triplet> terms;
    terms.reserve(g.edges.size() * 4);
    for (int e = 0; e < int(g.edges.size()); ++e) {
        const auto& edge = g.edges[e]; const double v = -q[e];
        terms.emplace_back(edge.i, edge.i, v); terms.emplace_back(edge.j, edge.j, v);
        terms.emplace_back(edge.i, edge.j, -v); terms.emplace_back(edge.j, edge.i, -v);
    }
    Sparse L(g.nodes.size(), g.nodes.size()); L.setFromTriplets(terms.begin(), terms.end()); return L;
}

Eigen::VectorXd freeLoads(const StructuralGraph& g) {
    Eigen::VectorXd p(freeCount(g)); int row = 0;
    for (const auto& node : g.nodes) for (int d = 0; d < 3; ++d)
        if (!node.fixed[d]) p[row++] = node.load[d];
    return p;
}

Eigen::VectorXd freeResidual(const StructuralGraph& g, const Eigen::MatrixXd& X, const Eigen::VectorXd& q) {
    return freeLoads(g) - equilibriumMatrix(g, X) * q;
}

SolverResult solveProjectedMixedFDM(const StructuralGraph& g, const SolverOptions& o) {
    SolverResult out;
    if (!valid(g, o, out.reason)) return out;
    const Eigen::MatrixXd X0 = inputPositions(g);
    const Eigen::VectorXd p = freeLoads(g);
    const Eigen::VectorXd supportP = horizontalSupportLoads(g);
    Eigen::VectorXd q(g.edges.size());
    for (int e = 0; e < q.size(); ++e)
        q[e] = (g.edges[e].type == MemberType::Compression ? 1.0 : -1.0) *
               std::clamp(g.edges[e].targetDensityMagnitude, o.minDensity, o.maxDensity);
    const bool anchor = p.norm() < 1e-12;
    const Sparse A0 = equilibriumMatrix(g, X0);
    Eigen::VectorXd feasible = boundedFit(A0, p, g, q, o, false, anchor);
    out.fixedGeometryResidual = (A0 * feasible - p).norm();
    if (!recover(g, q, out.X)) { out.reason = "initial signed FDM is singular or invalid"; return out; }
    auto record = [&](double projected, double step, int rejected) {
        const Eigen::VectorXd r = freeResidual(g, out.X, q);
        const double supportForce = horizontalSupportNorm(g, out.X, q, supportP);
        out.history.push_back({shapeError(out.X, X0), r.norm(), projected, supportForce, step, rejected});
        out.equilibriumValidated = r.norm() <= o.forceTolerance * std::max({1.0, p.norm(), q.norm()});
    };
    record(out.fixedGeometryResidual, 0, 0);
    double previousMerit = out.history.back().shapeError +
        o.supportHorizontalWeight * std::pow(out.history.back().horizontalSupportForce, 2);
    int smallSteps = 0;
    for (int iteration = 0; iteration < o.maxIterations; ++iteration) {
        bool accepted = false;
        int rejected = 0;
        double projected = 0, step = 0;
        for (double alpha = o.projection; alpha >= o.projection / 16 && !accepted; alpha *= 0.5) {
            Eigen::MatrixXd Xp = out.X;
            for (int i = 0; i < Xp.rows(); ++i) for (int d = 0; d < 3; ++d)
                if (!g.nodes[i].fixed[d]) Xp(i, d) += alpha * (X0(i, d) - Xp(i, d));
            Sparse A = equilibriumMatrix(g, Xp);
            projected = (p - A * q).norm();
            Eigen::VectorXd target;
            if (o.supportHorizontalWeight > 0 && supportP.size() > 0) {
                Sparse H = horizontalSupportMatrix(g, Xp);
                Sparse B = stackedCorrectionMatrix(A, H, o.supportHorizontalWeight);
                Eigen::VectorXd targetP(p.size() + supportP.size());
                targetP << p, std::sqrt(o.supportHorizontalWeight) * supportP;
                target = boundedFit(B, targetP, g, q, o, true, anchor);
            } else target = boundedFit(A, p, g, q, o, true, anchor);
            for (int backtrack = 0; backtrack < o.maxBacktracks; ++backtrack) {
                const double beta = std::ldexp(1.0, -backtrack);
                Eigen::VectorXd trialQ = q + beta * (target - q);
                Eigen::MatrixXd trialX;
                if (recover(g, trialQ, trialX) &&
                    freeResidual(g, trialX, trialQ).norm() <= o.forceTolerance * std::max({1.0, p.norm(), trialQ.norm()}) &&
                    shapeError(trialX, X0) + o.supportHorizontalWeight *
                        std::pow(horizontalSupportNorm(g, trialX, trialQ, supportP), 2) <
                        previousMerit - 1e-12 * std::max(1.0, previousMerit)) {
                    step = (trialQ - q).norm() / std::max(1.0, q.norm());
                    q = trialQ; out.X = trialX; accepted = true; break;
                }
                ++rejected;
            }
        }
        if (!accepted) {
            out.reason = out.equilibriumValidated && previousMerit / std::max(1.0, X0.squaredNorm()) < o.shapeTolerance
                ? "converged" : "stagnated: no valid shape-improving signed FDM step";
            break;
        }
        record(projected, step, rejected);
        double merit = out.history.back().shapeError +
            o.supportHorizontalWeight * std::pow(out.history.back().horizontalSupportForce, 2);
        double improvement = (previousMerit - merit) / std::max(1.0, previousMerit);
        smallSteps = (improvement < o.shapeTolerance && step < o.shapeTolerance) ? smallSteps + 1 : 0;
        previousMerit = merit;
        if (smallSteps >= 2 && out.equilibriumValidated) { out.reason = "converged"; break; }
    }
    if (out.reason.empty()) out.reason = "max_iterations";
    out.q = q;
    out.residuals = Eigen::MatrixXd::Zero(g.nodes.size(), 3);
    Eigen::VectorXd r = freeResidual(g, out.X, q); int row = 0;
    for (int i = 0; i < out.residuals.rows(); ++i) for (int d = 0; d < 3; ++d)
        if (!g.nodes[i].fixed[d]) out.residuals(i, d) = r[row++];
    return out;
}
} // namespace alice2::fdm

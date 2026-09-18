#include "BestFitForceDensitySolver.h"

#include <Eigen/Dense>
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <limits>
#include <set>
#include <sstream>

namespace alice2 {

std::vector<int> slidingSupports(const std::vector<int>& supports,
                                 const std::vector<TnaEdge>& edges,
                                 int count,
                                 bool enabled) {
    if (!enabled) return {};
    std::vector<bool> hasTension(count, false);
    for (const auto& e : edges) {
        if (e.isTension) {
            if (e.vertexA >= 0 && e.vertexA < count) hasTension[e.vertexA] = true;
            if (e.vertexB >= 0 && e.vertexB < count) hasTension[e.vertexB] = true;
        }
    }
    std::vector<int> sliding;
    for (int v : supports) {
        if (v >= 0 && v < count && hasTension[v]) {
            sliding.push_back(v);
        }
    }
    return sliding;
}

namespace {
using Matrix = Eigen::MatrixXd;
using Vector = Eigen::VectorXd;

double coordinate(const Vec3& p, int axis) { return axis == 0 ? p.x : axis == 1 ? p.y : p.z; }
void coordinate(Vec3& p, int axis, double value) {
    if (axis == 0) p.x = static_cast<float>(value);
    else if (axis == 1) p.y = static_cast<float>(value);
    else p.z = static_cast<float>(value);
}
bool finite(const Vec3& p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
std::vector<std::array<bool, 3>> constraints(int count, const std::vector<int>& xyz,
                                              const std::vector<int>& z,
                                              const std::vector<int>& sliding = {}) {
    std::vector<std::array<bool, 3>> fixed(count, {false, false, false});
    for (int v : xyz) if (v >= 0 && v < count) fixed[v] = {true, true, true};
    for (int v : z) if (v >= 0 && v < count) fixed[v][2] = true;
    for (int v : sliding) if (v >= 0 && v < count) {
        fixed[v][0] = false;
        fixed[v][1] = false;
        fixed[v][2] = true;
    }
    return fixed;
}
std::vector<float> verticalLoads(const MeshData& mesh, const TnaVerticalSettings& settings) {
    if (settings.density == 0.0f)
        return settings.nodalLoads.empty() ? std::vector<float>(mesh.vertices.size(), settings.nodalLoad) : settings.nodalLoads;
    auto areas = TnaSolver::vertexAreas(mesh, settings.unloadedFaces);
    for (std::size_t i = 0; i < areas.size(); ++i) {
        const float base = settings.nodalLoads.empty() ? settings.nodalLoad : settings.nodalLoads[i];
        const float thickness = settings.thicknesses.empty() ? settings.thickness : settings.thicknesses[i];
        areas[i] = base + areas[i] * thickness * settings.density;
    }
    return areas;
}
struct Equations {
    Matrix A;
    Vector p;
    std::vector<std::pair<int, int>> rows;
};
Equations equations(const MeshData& mesh, const std::vector<TnaEdge>& edges,
                    const std::vector<int>& xyz, const TnaVerticalSettings& settings,
                    const std::vector<int>& sliding = {}) {
    const int n = static_cast<int>(mesh.vertices.size());
    const auto fixed = constraints(n, xyz, settings.additionalFixedVertices, sliding);
    std::vector<std::array<int, 3>> row(n, {-1, -1, -1});
    Equations e;
    for (int axis = 0; axis < 3; ++axis) for (int v = 0; v < n; ++v) {
        if (!fixed[v][axis]) {
            row[v][axis] = static_cast<int>(e.rows.size());
            e.rows.emplace_back(v, axis);
        }
    }
    e.A = Matrix::Zero(e.rows.size(), edges.size());
    e.p = Vector::Zero(e.rows.size());
    auto loads = verticalLoads(mesh, settings);
    for (int i = 0; i < static_cast<int>(e.rows.size()); ++i)
        if (e.rows[i].second == 2) e.p[i] = loads[e.rows[i].first];
    for (int i = 0; i < static_cast<int>(edges.size()); ++i) {
        const auto& edge = edges[i];
        for (int axis = 0; axis < 3; ++axis) {
            const double d = coordinate(mesh.vertices[edge.vertexA].position, axis) -
                             coordinate(mesh.vertices[edge.vertexB].position, axis);
            if (row[edge.vertexA][axis] >= 0) e.A(row[edge.vertexA][axis], i) = d;
            if (row[edge.vertexB][axis] >= 0) e.A(row[edge.vertexB][axis], i) = -d;
        }
    }
    return e;
}

// Lawson-Hanson active-set NNLS on signed columns, shifted by the lower
// magnitude bound. COD handles dependent columns without inverting A^T A.
bool boundedFit(const Matrix& A, const Vector& b, const std::vector<TnaEdge>& edges,
                double lower, Vector& q, const std::vector<bool>& nearZero = {},
                const Vector* seed = nullptr) {
    const int n = static_cast<int>(edges.size());
    Matrix B = A;
    Vector sign(n);
    for (int i = 0; i < n; ++i) { sign[i] = edges[i].isTension ? -1.0 : 1.0; B.col(i) *= sign[i]; }
    const Vector rhs = b - B * Vector::Constant(n, lower);
    Vector x = Vector::Zero(n);
    std::vector<bool> passive(n, false);
    bool refitSeed = false;
    if (seed && seed->size() == n && seed->allFinite()) {
        for (int i = 0; i < n; ++i) {
            if (!nearZero.empty() && nearZero[i]) continue;
            x[i] = std::max(0.0, sign[i] * (*seed)[i] - lower);
            passive[i] = x[i] > 1e-12;
            if (!passive[i]) x[i] = 0.0;
            refitSeed = refitSeed || passive[i];
        }
    }
    const double tolerance = 1e-10 * std::max(1.0, (B.transpose() * rhs).lpNorm<Eigen::Infinity>());
    int budget = std::max(100, 10 * n);
    while (--budget > 0) {
        // Re-solve the seeded passive set for the NEW geometry/RHS before
        // checking inactive gradients; the old solution is only feasible.
        if (!refitSeed) {
            Vector gradient = B.transpose() * (rhs - B * x);
            int entering = -1;
            double maximum = tolerance;
            for (int i = 0; i < n; ++i) {
                if (!nearZero.empty() && nearZero[i]) continue;
                if (!passive[i] && gradient[i] > maximum) { maximum = gradient[i]; entering = i; }
            }
            if (entering < 0) {
                q = sign.cwiseProduct(x.array().max(0.0).matrix() + Vector::Constant(n, lower));
                return q.allFinite();
            }
            passive[entering] = true;
        }
        refitSeed = false;
        while (--budget > 0) {
            std::vector<int> ids;
            for (int i = 0; i < n; ++i) if (passive[i]) ids.push_back(i);
            if (ids.empty()) { x.setZero(); break; }
            Matrix selected(B.rows(), ids.size());
            for (int i = 0; i < static_cast<int>(ids.size()); ++i) selected.col(i) = B.col(ids[i]);
            Vector solved = selected.completeOrthogonalDecomposition().solve(rhs);
            if (!solved.allFinite()) return false;
            Vector z = Vector::Zero(n);
            for (int i = 0; i < static_cast<int>(ids.size()); ++i) z[ids[i]] = solved[i];
            double alpha = 1.0;
            bool positive = true;
            for (int i : ids) if (z[i] <= 0.0) {
                positive = false;
                if (x[i] > z[i]) alpha = std::min(alpha, x[i] / (x[i] - z[i]));
                else alpha = 0.0;
            }
            if (positive) { x = z; break; }
            x += alpha * (z - x);
            for (int i : ids) if (x[i] <= 1e-12) { x[i] = 0.0; passive[i] = false; }
        }
    }
    return false;
}

std::vector<float> floats(const Vector& q) {
    std::vector<float> values(q.size());
    for (int i = 0; i < q.size(); ++i) values[i] = static_cast<float>(q[i]);
    return values;
}
Vector vector(const std::vector<float>& q) {
    Vector result(q.size());
    for (int i = 0; i < result.size(); ++i) result[i] = q[i];
    return result;
}

// Full XYZ FDM with separate free-coordinate sets, including red Z supports and sliding tension supports.
bool equilibriumShape(const TnaFormDiagram& design, const MeshData& start,
                      const Vector& q, const BestFitForceDensitySettings& settings,
                      MeshData& candidate, TnaEquilibriumReport& report, std::string& failure,
                      const std::vector<int>& sliding = {}) {
    const int n = static_cast<int>(start.vertices.size());
    const auto fixed = constraints(n, design.supportVertices, settings.loads.additionalFixedVertices, sliding);
    std::vector<bool> isSliding(n, false);
    for (int v : sliding) if (v >= 0 && v < n) isSliding[v] = true;

    Matrix D = Matrix::Zero(n, n);
    for (int i = 0; i < q.size(); ++i) {
        const auto& e = design.activeFormEdges[i];
        D(e.vertexA, e.vertexA) += q[i]; D(e.vertexB, e.vertexB) += q[i];
        D(e.vertexA, e.vertexB) -= q[i]; D(e.vertexB, e.vertexA) -= q[i];
    }
    std::array<std::vector<int>, 3> free;
    std::array<Eigen::FullPivLU<Matrix>, 3> factors;
    std::array<Vector, 3> supportRhs;
    const double penalty = settings.slidingSupportPenalty;
    for (int axis = 0; axis < 3; ++axis) {
        for (int v = 0; v < n; ++v) if (!fixed[v][axis]) free[axis].push_back(v);
        const int count = static_cast<int>(free[axis].size());
        if (count == 0) continue;
        // X and Y always have identical free sets and spring diagonals.
        Matrix reduced;
        if (axis != 1) reduced.resize(count, count);
        supportRhs[axis] = Vector::Zero(count);
        for (int r = 0; r < count; ++r) {
            const int vr = free[axis][r];
            if (axis != 1)
                for (int c = 0; c < count; ++c) reduced(r, c) = D(vr, free[axis][c]);
            for (int v = 0; v < n; ++v) if (fixed[v][axis])
                supportRhs[axis][r] -= D(vr, v) * coordinate(design.mesh.vertices[v].position, axis);

            if (axis < 2 && isSliding[vr] && penalty > 0.0) {
                if (axis != 1) reduced(r, r) += penalty;
                supportRhs[axis][r] += penalty * coordinate(design.mesh.vertices[vr].position, axis);
            }
        }
        if (axis != 1) {
            factors[axis].compute(reduced);
            if (!factors[axis].isInvertible()) { failure = "singular signed stiffness"; return false; }
        }
    }
    candidate = start;
    double scale = 1.0;
    for (const auto& v : design.mesh.vertices)
        scale = std::max(scale, double((v.position - design.mesh.vertices.front().position).length()));
    auto densities = floats(q);
    auto loads = verticalLoads(candidate, settings.loads);
    for (int iteration = 0; iteration < std::max(1, settings.loads.maximumIterations); ++iteration) {
        for (int axis = 0; axis < 3; ++axis) {
            if (free[axis].empty() || (iteration > 0 && axis < 2)) continue;
            Vector rhs = supportRhs[axis];
            if (axis == 2) for (int r = 0; r < rhs.size(); ++r) rhs[r] += loads[free[axis][r]];
            Vector solved = factors[axis == 1 ? 0 : axis].solve(rhs);
            if (!solved.allFinite()) { failure = "nonfinite FDM solve"; return false; }
            for (int r = 0; r < solved.size(); ++r) coordinate(candidate.vertices[free[axis][r]].position, axis, solved[r]);
        }
        for (int v = 0; v < n; ++v) {
            for (int axis = 0; axis < 3; ++axis) if (fixed[v][axis])
                coordinate(candidate.vertices[v].position, axis, coordinate(design.mesh.vertices[v].position, axis));
            if (!finite(candidate.vertices[v].position) ||
                (candidate.vertices[v].position - design.mesh.vertices[v].position).length() > 100.0 * scale) {
                failure = "unbounded FDM displacement"; return false;
            }
        }
        report = measureTnaEquilibrium(candidate, design.mesh, design.activeFormEdges,
                                      densities, design.supportVertices, settings.loads,
                                      settings.allowSlidingTensionSupports, settings.slidingSupportPenalty);
        if (!report.finite) { failure = "nonfinite equilibrium residual"; return false; }
        if (report.residual <= settings.loads.residualTolerance) {
            candidate.calculateNormals(); candidate.triangulationDirty = true;
            return true;
        }
        // The residual check already calculated loads on the updated shape.
        loads = std::move(report.loads);
    }
    failure = "self-weight residual " + std::to_string(report.residual);
    return false;
}
} // namespace

TnaEquilibriumReport measureTnaEquilibrium(
    const MeshData& mesh, const MeshData& reference, const std::vector<TnaEdge>& edges,
    const std::vector<float>& densities, const std::vector<int>& fixedXYZ,
    const TnaVerticalSettings& settings,
    bool allowSlidingTensionSupports,
    double slidingSupportPenalty) {
    TnaEquilibriumReport result;
    const int n = static_cast<int>(mesh.vertices.size());
    if (n == 0 || mesh.vertices.size() != reference.vertices.size() || edges.size() != densities.size() ||
        (!settings.nodalLoads.empty() && settings.nodalLoads.size() != mesh.vertices.size()) ||
        (!settings.thicknesses.empty() && settings.thicknesses.size() != mesh.vertices.size())) return result;
    for (const auto& v : mesh.vertices) if (!finite(v.position)) return result;
    result.loads = verticalLoads(mesh, settings);
    result.residuals.assign(n, Vec3(0, 0, 0)); result.reactions.assign(n, Vec3(0, 0, 0));
    for (int v = 0; v < n; ++v) result.residuals[v].z = result.loads[v];
    for (std::size_t i = 0; i < edges.size(); ++i) {
        const auto& e = edges[i];
        if (e.vertexA < 0 || e.vertexB < 0 || e.vertexA >= n || e.vertexB >= n || !std::isfinite(densities[i])) return result;
        const auto force = (mesh.vertices[e.vertexA].position - mesh.vertices[e.vertexB].position) * densities[i];
        result.residuals[e.vertexA] -= force; result.residuals[e.vertexB] += force;
    }
    const auto sliding = slidingSupports(fixedXYZ, edges, n, allowSlidingTensionSupports);
    auto fixed = constraints(n, fixedXYZ, settings.additionalFixedVertices, sliding);
    std::vector<bool> isSliding(n, false);
    for (int v : sliding) if (v >= 0 && v < n) isSliding[v] = true;

    double distanceSquared = 0.0, horizontalSquared = 0.0, verticalSquared = 0.0;
    for (int v = 0; v < n; ++v) {
        if (!finite(result.residuals[v])) return result;
        for (int axis = 0; axis < 3; ++axis) {
            const double r = coordinate(result.residuals[v], axis);
            if (fixed[v][axis]) {
                coordinate(result.reactions[v], axis, -r);
            } else if (isSliding[v] && axis < 2) {
                // Sliding support in XY: the ground reaction is vertical only (reactions[v].x = 0, reactions[v].y = 0).
                // During equilibriumShape with penalty spring, r = penalty * (x - x0).
                // Net unbalanced force including penalty constraint reaction:
                const double d = coordinate(mesh.vertices[v].position, axis) -
                                 coordinate(reference.vertices[v].position, axis);
                const double rPenalty = -slidingSupportPenalty * d;
                const double netUnbalanced = r + rPenalty;
                horizontalSquared += netUnbalanced * netUnbalanced;
            } else if (axis == 2) {
                verticalSquared += r * r;
            } else {
                horizontalSquared += r * r;
            }
        }
        const double d = isSliding[v]
            ? std::abs(coordinate(mesh.vertices[v].position, 2) - coordinate(reference.vertices[v].position, 2))
            : (mesh.vertices[v].position - reference.vertices[v].position).length();
        if (!std::isfinite(d)) return result;
        distanceSquared += d * d; result.maximumDesignDistance = std::max(result.maximumDesignDistance, d);
    }
    result.rmsDesignDistance = std::sqrt(distanceSquared / n);
    result.horizontalResidual = std::sqrt(horizontalSquared); result.verticalResidual = std::sqrt(verticalSquared);
    result.residual = std::sqrt(horizontalSquared + verticalSquared); result.finite = true;
    return result;
}

bool BestFitForceDensitySolver::reset(const TnaFormDiagram& design, const BestFitForceDensitySettings& settings) {
    m_design = design; m_settings = settings; m_result = {};
    m_result.mesh = design.mesh; m_result.edges = design.activeFormEdges;
    const int n = static_cast<int>(design.mesh.vertices.size());
    auto fail = [&](const char* message) { m_result.status = BestFitStatus::Failed; m_result.diagnostic = message; return false; };
    if (!design.success || n == 0 || design.activeFormEdges.empty() || design.supportVertices.empty())
        return fail("Shape fit needs a valid mesh, active members and black supports");
    if (settings.maximumIterations < 1 || !std::isfinite(settings.designAttraction) || settings.designAttraction < 0 ||
        !std::isfinite(settings.minimumDensityMagnitude) || settings.minimumDensityMagnitude <= 0 ||
        !std::isfinite(settings.displacementTolerance) || settings.displacementTolerance <= 0 ||
        !std::isfinite(settings.slidingSupportPenalty) || settings.slidingSupportPenalty < 0 ||
        !std::isfinite(settings.loads.residualTolerance) || settings.loads.residualTolerance <= 0 ||
        settings.loads.maximumIterations < 1 || !std::isfinite(settings.loads.nodalLoad) ||
        !std::isfinite(settings.loads.density) || settings.loads.density < 0 ||
        !std::isfinite(settings.loads.thickness) || settings.loads.thickness < 0)
        return fail("Shape fit settings must be finite with positive tolerances and iteration limits");
    for (const auto* values : {&settings.loads.nodalLoads, &settings.loads.thicknesses}) {
        if (!values->empty() && values->size() != design.mesh.vertices.size()) return fail("Load arrays must match vertex count");
        for (float value : *values) if (!std::isfinite(value)) return fail("Load arrays contain nonfinite values");
    }
    for (float thickness : settings.loads.thicknesses) if (thickness < 0) return fail("Thickness must be nonnegative");
    for (int face : settings.loads.unloadedFaces)
        if (face < 0 || face >= static_cast<int>(design.mesh.faces.size())) return fail("Invalid unloaded face index");
    for (int v : design.supportVertices) if (v < 0 || v >= n) return fail("Invalid black support index");
    for (int v : settings.loads.additionalFixedVertices) if (v < 0 || v >= n) return fail("Invalid red support index");
    for (int e : settings.nearZeroEdges)
        if (e < 0 || e >= static_cast<int>(design.activeFormEdges.size())) return fail("Invalid near-zero edge index");
    for (const auto& v : design.mesh.vertices) if (!finite(v.position)) return fail("Nonfinite input coordinates");
    for (const auto& face : design.mesh.faces) for (int v : face.vertices)
        if (v < 0 || v >= n) return fail("Invalid face vertex index");
    std::vector<std::vector<int>> neighbours(n);
    std::set<std::pair<int, int>> unique;
    for (const auto& e : design.activeFormEdges) {
        if (e.vertexA < 0 || e.vertexB < 0 || e.vertexA >= n || e.vertexB >= n || e.vertexA == e.vertexB ||
            (design.mesh.vertices[e.vertexA].position - design.mesh.vertices[e.vertexB].position).length() < 1e-6f)
            return fail("Invalid or zero-length active member");
        if (!unique.emplace(std::min(e.vertexA, e.vertexB), std::max(e.vertexA, e.vertexB)).second)
            return fail("Duplicate active member");
        neighbours[e.vertexA].push_back(e.vertexB); neighbours[e.vertexB].push_back(e.vertexA);
    }
    std::vector<bool> reached(n, false);
    std::vector<int> pending = design.supportVertices;
    for (int v : pending) reached[v] = true;
    while (!pending.empty()) { const int v = pending.back(); pending.pop_back();
        for (int u : neighbours[v]) if (!reached[u]) { reached[u] = true; pending.push_back(u); } }
    if (std::find(reached.begin(), reached.end(), false) != reached.end()) return fail("Every active component needs a black support");
    m_slidingSupports = slidingSupports(design.supportVertices, design.activeFormEdges, n,
                                        settings.allowSlidingTensionSupports);
    m_nearZeroMask.assign(design.activeFormEdges.size(), false);
    for (int edge : settings.nearZeroEdges) m_nearZeroMask[edge] = true;
    m_referenceEquilibrium = equations(design.mesh, design.activeFormEdges, design.supportVertices,
                                       settings.loads, m_slidingSupports).A;
    m_result.status = BestFitStatus::Running; m_result.diagnostic = "Step 1: fitting signed densities on original design";
    return true;
}

void BestFitForceDensitySolver::step() {
    if (m_result.status != BestFitStatus::Running) return;
    const auto& edges = m_design.activeFormEdges;
    ++m_result.iteration;
    const auto& nearZeroMask = m_nearZeroMask;
    const auto& sliding = m_slidingSupports;
    const auto e = equations(m_result.mesh, edges, m_design.supportVertices, m_settings.loads, sliding);
    const auto fitDensities = [&](const Vector& rhs, Vector& output, const Vector* seed = nullptr) {
        const auto begin = std::chrono::steady_clock::now();
        bool success = boundedFit(e.A, rhs, edges, m_settings.minimumDensityMagnitude, output, nearZeroMask, seed);
        // Degenerate warm starts can cycle. Preserve the original cold solver
        // as a bounded fallback, including its rank-revealing decomposition.
        if (!success && seed)
            success = boundedFit(e.A, rhs, edges, m_settings.minimumDensityMagnitude, output, nearZeroMask);
        m_result.densityFitSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
        return success;
    };
    Vector q;
    if (!m_result.success) {
        // Bhooshan et al., IASS 2015, section 3.2 step 1, extended to signed bounds.
        if (!fitDensities(e.p, q)) {
            m_result.status = BestFitStatus::Failed; m_result.diagnostic = "Initial bounded density fit did not converge"; return;
        }
    } else {
        q = vector(m_result.forceDensities);
        // Paper steps 3-7: A*dq = residual + target force. Convert attraction
        // displacement to force with the current stiffness, using the sign
        // D*dx + A*dq = p - A*q. No CCF distance-from-plane projection.
        const Vector residual = e.p - e.A * q;
        Vector targetForce = -m_settings.designAttraction * (m_referenceEquilibrium - e.A) * q;
        std::vector<bool> isSliding(m_design.mesh.vertices.size(), false);
        for (int v : sliding) if (v >= 0 && v < static_cast<int>(isSliding.size())) isSliding[v] = true;
        for (int r = 0; r < static_cast<int>(e.rows.size()); ++r) {
            const int v = e.rows[r].first;
            const int axis = e.rows[r].second;
            if (isSliding[v] && axis < 2) {
                targetForce[r] = 0.0;
            }
        }
        Vector proposed;
        if (!fitDensities(e.A * q + residual + targetForce, proposed, &q)) {
            m_result.status = BestFitStatus::Stagnated;
            m_result.diagnostic = "Step 1 stopped: bounded update stalled; retained best equilibrium shape"; return;
        }
        q = proposed;
    }
    bool accepted = false;
    double movement = 0.0;
    MeshData candidate;
    TnaEquilibriumReport report;
    std::string failure;
    const bool initial = !m_result.success;
    const MeshData start = m_result.mesh;
    const Vector previous = initial ? q : vector(m_result.forceDensities);
    const double commonMagnitude = std::max(m_settings.minimumDensityMagnitude, q.cwiseAbs().mean());
    Vector common(q.size());
    for (int i = 0; i < q.size(); ++i) {
        if (nearZeroMask[i]) common[i] = (edges[i].isTension ? -1.0 : 1.0) * m_settings.minimumDensityMagnitude;
        else common[i] = edges[i].isTension ? -commonMagnitude : commonMagnitude;
    }
    // A residual-minimising q can lie near a signed stiffness mechanism.
    // Continue towards a connected, uniformly sized signed seed and test
    // force scales under self-weight. Keep the closest valid candidate;
    // never present a singular/unstable least-squares fit as a solved shape.
    constexpr double blends[] = {0.0, .001, .01, .05, .1, .25, .5, 1.0};
    for (int trial = 0; trial < (initial ? 64 : 12); ++trial) {
        const double blend = initial ? blends[trial / 8] : 0.0;
        Vector trialQ = initial
            ? Vector(((1.0 - blend) * q + blend * common) * std::pow(2.0, trial % 8))
            : Vector(previous + std::pow(0.5, trial) * (q - previous));
        for (int i = 0; i < q.size(); ++i) {
            if (nearZeroMask[i]) {
                trialQ[i] = (edges[i].isTension ? -1.0 : 1.0) * m_settings.minimumDensityMagnitude;
            }
        }
        const auto trialBegin = std::chrono::steady_clock::now();
        const bool valid = equilibriumShape(m_design, start, trialQ, m_settings, candidate, report, failure, sliding);
        m_result.equilibriumSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - trialBegin).count();
        ++m_result.equilibriumTrials;
        if (!valid) continue;
        if (m_result.success && report.rmsDesignDistance >= m_result.report.rmsDesignDistance - 1e-9) continue;
        for (std::size_t i = 0; i < candidate.vertices.size(); ++i)
            movement = std::max(movement, double((candidate.vertices[i].position - m_result.mesh.vertices[i].position).length()));
        m_result.mesh = std::move(candidate); m_result.forceDensities = floats(trialQ);
        m_result.report = std::move(report); m_result.success = true; accepted = true;
        if (!initial || m_result.report.maximumDesignDistance <= m_settings.displacementTolerance) break;
    }
    if (!accepted) {
        m_result.status = m_result.success ? BestFitStatus::Stagnated : BestFitStatus::Failed;
        m_result.diagnostic = m_result.success ? "Step 1 stagnated; best equilibrium shape retained" :
            "Step 1 failed: " + failure + "; check supports/tension/load";
        return;
    }
    if (movement <= m_settings.displacementTolerance || m_result.report.maximumDesignDistance <= m_settings.displacementTolerance)
        m_result.status = BestFitStatus::Converged;
    else if (m_result.iteration >= m_settings.maximumIterations) m_result.status = BestFitStatus::IterationLimit;
    std::ostringstream message;
    message << "Step 1 " << (m_result.status == BestFitStatus::Running ? "running" :
               m_result.status == BestFitStatus::Converged ? "converged" : "iteration limit")
            << ": " << m_result.iteration << "/" << m_settings.maximumIterations
            << ", residual " << m_result.report.residual << ", design RMS " << m_result.report.rmsDesignDistance;
    m_result.diagnostic = message.str();
}
} // namespace alice2

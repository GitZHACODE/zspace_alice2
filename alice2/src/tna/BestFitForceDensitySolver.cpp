#include "BestFitForceDensitySolver.h"

#include <Eigen/Dense>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <sstream>

namespace alice2 {
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
                                          const std::vector<int>& z) {
    std::vector<std::array<bool, 3>> fixed(count, {false, false, false});
    for (int v : xyz) if (v >= 0 && v < count) fixed[v] = {true, true, true};
    for (int v : z) if (v >= 0 && v < count) fixed[v][2] = true;
    return fixed;
}
std::vector<float> verticalLoads(const MeshData& mesh, const TnaVerticalSettings& settings) {
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
                    const std::vector<int>& xyz, const TnaVerticalSettings& settings) {
    const int n = static_cast<int>(mesh.vertices.size());
    const auto fixed = constraints(n, xyz, settings.additionalFixedVertices);
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
                double lower, Vector& q) {
    const int n = static_cast<int>(edges.size());
    Matrix B = A;
    Vector sign(n);
    for (int i = 0; i < n; ++i) { sign[i] = edges[i].isTension ? -1.0 : 1.0; B.col(i) *= sign[i]; }
    const Vector rhs = b - B * Vector::Constant(n, lower);
    Vector x = Vector::Zero(n);
    std::vector<bool> passive(n, false);
    const double tolerance = 1e-10 * std::max(1.0, (B.transpose() * rhs).lpNorm<Eigen::Infinity>());
    int budget = std::max(100, 10 * n);
    while (--budget > 0) {
        Vector gradient = B.transpose() * (rhs - B * x);
        int entering = -1;
        double maximum = tolerance;
        for (int i = 0; i < n; ++i)
            if (!passive[i] && gradient[i] > maximum) { maximum = gradient[i]; entering = i; }
        if (entering < 0) {
            q = sign.cwiseProduct(x.array().max(0.0).matrix() + Vector::Constant(n, lower));
            return q.allFinite();
        }
        passive[entering] = true;
        while (--budget > 0) {
            std::vector<int> ids;
            for (int i = 0; i < n; ++i) if (passive[i]) ids.push_back(i);
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

// Full XYZ FDM with separate free-coordinate sets, including red Z supports.
bool equilibriumShape(const TnaFormDiagram& design, const MeshData& start,
                      const Vector& q, const BestFitForceDensitySettings& settings,
                      MeshData& candidate, TnaEquilibriumReport& report, std::string& failure) {
    const int n = static_cast<int>(start.vertices.size());
    const auto fixed = constraints(n, design.supportVertices, settings.loads.additionalFixedVertices);
    Matrix D = Matrix::Zero(n, n);
    for (int i = 0; i < q.size(); ++i) {
        const auto& e = design.activeFormEdges[i];
        D(e.vertexA, e.vertexA) += q[i]; D(e.vertexB, e.vertexB) += q[i];
        D(e.vertexA, e.vertexB) -= q[i]; D(e.vertexB, e.vertexA) -= q[i];
    }
    std::array<std::vector<int>, 3> free;
    std::array<Eigen::FullPivLU<Matrix>, 3> factors;
    std::array<Vector, 3> supportRhs;
    for (int axis = 0; axis < 3; ++axis) {
        for (int v = 0; v < n; ++v) if (!fixed[v][axis]) free[axis].push_back(v);
        const int count = static_cast<int>(free[axis].size());
        if (count == 0) continue;
        Matrix reduced(count, count);
        supportRhs[axis] = Vector::Zero(count);
        for (int r = 0; r < count; ++r) {
            for (int c = 0; c < count; ++c) reduced(r, c) = D(free[axis][r], free[axis][c]);
            for (int v = 0; v < n; ++v) if (fixed[v][axis])
                supportRhs[axis][r] -= D(free[axis][r], v) * coordinate(design.mesh.vertices[v].position, axis);
        }
        factors[axis].compute(reduced);
        if (!factors[axis].isInvertible()) { failure = "singular signed stiffness"; return false; }
    }
    candidate = start;
    double scale = 1.0;
    for (const auto& v : design.mesh.vertices)
        scale = std::max(scale, double((v.position - design.mesh.vertices.front().position).length()));
    auto densities = floats(q);
    for (int iteration = 0; iteration < std::max(1, settings.loads.maximumIterations); ++iteration) {
        const auto loads = verticalLoads(candidate, settings.loads);
        for (int axis = 0; axis < 3; ++axis) {
            if (free[axis].empty()) continue;
            Vector rhs = supportRhs[axis];
            if (axis == 2) for (int r = 0; r < rhs.size(); ++r) rhs[r] += loads[free[axis][r]];
            Vector solved = factors[axis].solve(rhs);
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
                                      densities, design.supportVertices, settings.loads);
        if (!report.finite) { failure = "nonfinite equilibrium residual"; return false; }
        if (report.residual <= settings.loads.residualTolerance) {
            candidate.calculateNormals(); candidate.triangulationDirty = true;
            return true;
        }
    }
    failure = "self-weight residual " + std::to_string(report.residual);
    return false;
}
} // namespace

TnaEquilibriumReport measureTnaEquilibrium(
    const MeshData& mesh, const MeshData& reference, const std::vector<TnaEdge>& edges,
    const std::vector<float>& densities, const std::vector<int>& fixedXYZ,
    const TnaVerticalSettings& settings) {
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
    auto fixed = constraints(n, fixedXYZ, settings.additionalFixedVertices);
    double distanceSquared = 0.0, horizontalSquared = 0.0, verticalSquared = 0.0;
    for (int v = 0; v < n; ++v) {
        if (!finite(result.residuals[v])) return result;
        for (int axis = 0; axis < 3; ++axis) {
            const double r = coordinate(result.residuals[v], axis);
            if (fixed[v][axis]) coordinate(result.reactions[v], axis, -r);
            else if (axis == 2) verticalSquared += r * r;
            else horizontalSquared += r * r;
        }
        const double d = (mesh.vertices[v].position - reference.vertices[v].position).length();
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
    m_result.status = BestFitStatus::Running; m_result.diagnostic = "Step 1: fitting signed densities on original design";
    return true;
}

void BestFitForceDensitySolver::step() {
    if (m_result.status != BestFitStatus::Running) return;
    const auto& edges = m_design.activeFormEdges;
    ++m_result.iteration;
    const auto e = equations(m_result.mesh, edges, m_design.supportVertices, m_settings.loads);
    Vector q;
    if (!m_result.success) {
        // Bhooshan et al., IASS 2015, section 3.2 step 1, extended to signed bounds.
        if (!boundedFit(e.A, e.p, edges, m_settings.minimumDensityMagnitude, q)) {
            m_result.status = BestFitStatus::Failed; m_result.diagnostic = "Initial bounded density fit did not converge"; return;
        }
    } else {
        q = vector(m_result.forceDensities);
        // Paper steps 3-7: A*dq = residual + target force. Convert attraction
        // displacement to force with the current stiffness, using the sign
        // D*dx + A*dq = p - A*q. No CCF distance-from-plane projection.
        const auto target = equations(m_design.mesh, edges, m_design.supportVertices, m_settings.loads);
        const Vector residual = e.p - e.A * q;
        const Vector targetForce = -m_settings.designAttraction * (target.A - e.A) * q;
        Vector proposed;
        if (!boundedFit(e.A, e.A * q + residual + targetForce, edges,
                        m_settings.minimumDensityMagnitude, proposed)) {
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
    for (int i = 0; i < q.size(); ++i) common[i] = edges[i].isTension ? -commonMagnitude : commonMagnitude;
    // A residual-minimising q can lie near a signed stiffness mechanism.
    // Continue towards a connected, uniformly sized signed seed and test
    // force scales under self-weight. Keep the closest valid candidate;
    // never present a singular/unstable least-squares fit as a solved shape.
    constexpr double blends[] = {0.0, .001, .01, .05, .1, .25, .5, 1.0};
    for (int trial = 0; trial < (initial ? 64 : 12); ++trial) {
        const double blend = initial ? blends[trial / 8] : 0.0;
        const Vector trialQ = initial
            ? Vector(((1.0 - blend) * q + blend * common) * std::pow(2.0, trial % 8))
            : Vector(previous + std::pow(0.5, trial) * (q - previous));
        if (!equilibriumShape(m_design, start, trialQ, m_settings, candidate, report, failure)) continue;
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

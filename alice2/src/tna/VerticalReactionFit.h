#pragma once
#include "BestFitForceDensitySolver.h"
#include <Eigen/SparseQR>
#include <array>
#include <sstream>

namespace alice2 {
// Joint geometry/density least squares with increasing equilibrium weight.
// Positional preferences never enter the physical force balance or reactions.
class VerticalReactionFit {
public:
    bool reset(const TnaFormDiagram& form, const BestFitForceDensitySettings& settings,
               double slidingMovementPenalty, const std::vector<int>& yellowSupports = {},
               double yellowMovementPenalty = 50) {
        BestFitForceDensitySolver validation;
        m_form = form; m_settings = settings; m_penalty = slidingMovementPenalty;
        m_result = {}; m_result.mesh = form.mesh; m_result.edges = form.activeFormEdges;
        // Validation needs graph anchors, but this coupled solve also supports
        // networks whose supports all slide: positional objectives fix the gauge.
        auto validationForm = form;
        if (validationForm.supportVertices.empty()) validationForm.supportVertices = settings.loads.additionalFixedVertices;
        if (!validation.reset(validationForm, settings) || !std::isfinite(m_penalty) || m_penalty < 0 ||
            !std::isfinite(yellowMovementPenalty) || yellowMovementPenalty < 0) {
            m_result.status = BestFitStatus::Failed;
            m_result.diagnostic = "Invalid vertical-reaction fit: " + validation.result().diagnostic;
            return false;
        }
        const int n = int(form.mesh.vertices.size());
        std::vector<bool> fixed(n, false), fixedZ(n, false), sliding(n, false), yellow(n, false);
        for (int v : yellowSupports) {
            if (v < 0 || v >= n) {
                m_result.status = BestFitStatus::Failed; m_result.diagnostic = "Invalid yellow support index"; return false;
            }
            yellow[v] = true;
        }
        for (int v : form.supportVertices) fixed[v] = fixedZ[v] = true;
        for (int v : settings.loads.additionalFixedVertices) fixedZ[v] = sliding[v] = true;
        m_coordinates.clear(); m_rows.clear(); m_preferences.clear();
        m_scale = 1;
        for (const auto& v : form.mesh.vertices)
            m_scale = std::max(m_scale, double((v.position - form.mesh.vertices.front().position).length()));
        for (int v = 0; v < n; ++v) for (int axis = 0; axis < 3; ++axis) {
            // XY equilibrium at EVERY vertex, including all fixed anchors.
            if (axis < 2 || !fixedZ[v]) m_rows.emplace_back(v, axis);
            if (!fixed[v] && (axis < 2 || !fixedZ[v])) {
                m_coordinates.emplace_back(v, axis);
                m_preferences.push_back(sliding[v] && axis < 2
                    ? (yellow[v] ? yellowMovementPenalty : m_penalty) : settings.designAttraction);
            }
        }
        const int e = int(form.activeFormEdges.size());
        m_x = Eigen::VectorXd::Zero(e + m_coordinates.size());
        for (int i = 0; i < int(m_coordinates.size()); ++i) {
            auto [v,a] = m_coordinates[i];
            m_x[e+i] = component(form.mesh.vertices[v].position, a) / m_scale;
        }
        m_reference = m_x;
        m_weight = 100; m_damping = 1e-4;
        m_forceScale = 1;
        auto initial = evaluate(m_x);
        for (float load : initial.report.loads) m_forceScale = std::max(m_forceScale, std::abs(double(load)));
        m_result = std::move(initial); m_result.status = BestFitStatus::Running;
        m_result.diagnostic = "Fitting geometry and densities for vertical-only reactions";
        return true;
    }
    const BestFitForceDensityResult& result() const { return m_result; }
    void step(std::stop_token stop = {}) {
        if (m_result.status != BestFitStatus::Running) return;
        if (stop.stop_requested()) { finish(BestFitStatus::Cancelled); return; }
        const int e = int(m_form.activeFormEdges.size()), k = int(m_coordinates.size());
        const int r = int(m_rows.size()), variables = int(m_x.size());
        if (m_result.iteration == 0) {
            // Start from the existing free-node equilibrium when it also
            // improves the full residual. Seed work remains on the worker.
            BestFitForceDensitySolver seed;
            if (seed.reset(m_form, m_settings)) {
                seed.step(stop);
                if (seed.result().success) {
                    Eigen::VectorXd trial = m_x;
                    for (int i = 0; i < e; ++i)
                        trial[i] = std::log(std::max(1e-10, std::abs(double(seed.result().forceDensities[i])) -
                            m_settings.minimumDensityMagnitude));
                    for (int i = 0; i < k; ++i) {
                        auto [v,a] = m_coordinates[i];
                        trial[e+i] = component(seed.result().mesh.vertices[v].position,a) / m_scale;
                    }
                    const auto seeded = evaluate(trial);
                    if (seeded.report.finite && objective(trial, seeded) < objective(m_x, m_result)) m_x = trial;
                }
            }
            if (stop.stop_requested()) { finish(BestFitStatus::Cancelled); return; }
        }
        auto current = evaluate(m_x);
        const Eigen::VectorXd base = forces(current);
        const double w = std::sqrt(m_weight) / m_forceScale;
        std::vector<Eigen::Triplet<double>> entries;
        // Geometry derivatives include changing tributary self-weight.
        for (int c = 0; c < variables; ++c) {
            if (stop.stop_requested()) { finish(BestFitStatus::Cancelled); return; }
            Eigen::VectorXd perturbed = m_x;
            const double h = 1e-4 * std::max(1., std::abs(m_x[c]));
            perturbed[c] += h;
            const Eigen::VectorXd derivative = (forces(evaluate(perturbed)) - base) / h;
            for (int row = 0; row < r; ++row)
                if (std::abs(derivative[row]) > 1e-12) entries.emplace_back(row, c, w * derivative[row]);
        }
        Eigen::VectorXd rhs = Eigen::VectorXd::Zero(r + k + variables);
        rhs.head(r) = -w * base;
        for (int i = 0; i < k; ++i) {
            const double preference = std::sqrt(m_preferences[i]);
            entries.emplace_back(r+i, e+i, preference);
            rhs[r+i] = -preference * (m_x[e+i] - m_reference[e+i]);
        }
        for (int i = 0; i < variables; ++i)
            entries.emplace_back(r+k+i, i, std::sqrt(m_damping));
        Eigen::SparseMatrix<double> jacobian(rhs.size(), variables);
        jacobian.setFromTriplets(entries.begin(), entries.end());
        Eigen::SparseQR<Eigen::SparseMatrix<double>, Eigen::COLAMDOrdering<int>> qr;
        qr.compute(jacobian);
        if (qr.info() != Eigen::Success) { finish(BestFitStatus::Failed); return; }
        Eigen::VectorXd delta = qr.solve(rhs);
        if (!delta.allFinite()) { finish(BestFitStatus::Failed); return; }
        // Log magnitudes preserve signs; cap each trial to a moderate density/geometry change.
        double divisor = std::max(1., delta.head(e).cwiseAbs().maxCoeff());
        if (k) divisor = std::max(divisor, delta.tail(k).cwiseAbs().maxCoeff() / .2);
        delta /= divisor;
        bool accepted = false;
        const double before = objective(m_x, current);
        for (int trial = 0; trial < 14; ++trial) {
            if (stop.stop_requested()) { finish(BestFitStatus::Cancelled); return; }
            const Eigen::VectorXd candidate = m_x + std::ldexp(1., -trial) * delta;
            if (candidate.head(e).cwiseAbs().maxCoeff() > 25) continue;
            auto result = evaluate(candidate);
            if (!result.report.finite || objective(candidate, result) >= before) continue;
            m_x = candidate; current = std::move(result); accepted = true; break;
        }
        const int iteration = m_result.iteration + 1;
        m_result = std::move(current); m_result.iteration = iteration;
        m_result.status = BestFitStatus::Running;
        m_damping = accepted ? std::max(1e-8, m_damping * .5) : std::min(1e6, m_damping * 10);
        if ((!accepted || iteration % 8 == 0) && !m_result.success)
            m_weight = std::min(1e12, m_weight * 10);
        if (m_result.success && (!accepted || delta.norm() < 1e-5)) finish(BestFitStatus::Converged);
        else if (iteration >= m_settings.maximumIterations) finish(BestFitStatus::IterationLimit);
        else if (!accepted && m_damping >= 1e6 && m_weight >= 1e12) finish(BestFitStatus::Stagnated);
        else describe("Running");
    }
private:
    static double component(const Vec3& p, int a) { return a == 0 ? p.x : a == 1 ? p.y : p.z; }
    BestFitForceDensityResult evaluate(const Eigen::VectorXd& x) const {
        BestFitForceDensityResult result;
        result.mesh = m_form.mesh; result.edges = m_form.activeFormEdges;
        const int e = int(result.edges.size());
        for (int i = 0; i < int(m_coordinates.size()); ++i) {
            auto [v,a] = m_coordinates[i];
            auto& p = result.mesh.vertices[v].position;
            const float value = float(x[e+i] * m_scale);
            if (a == 0) p.x = value; else if (a == 1) p.y = value; else p.z = value;
        }
        for (int i = 0; i < e; ++i) result.forceDensities.push_back(float(
            (result.edges[i].isTension ? -1 : 1) * (m_settings.minimumDensityMagnitude + std::exp(x[i]))));
        result.report = measureTnaEquilibrium(result.mesh, m_form.mesh, result.edges,
            result.forceDensities, m_form.supportVertices, m_settings.loads, false, 0);
        if (!result.report.finite) return result;
        double horizontal = 0;
        for (const auto& residual : result.report.residuals)
            horizontal += double(residual.x)*residual.x + double(residual.y)*residual.y;
        result.report.horizontalResidual = std::sqrt(horizontal);
        result.report.residual = std::hypot(result.report.horizontalResidual, result.report.verticalResidual);
        result.success = result.report.residual <= m_settings.loads.residualTolerance;
        for (const auto& edge : result.edges)
            if ((result.mesh.vertices[edge.vertexA].position-result.mesh.vertices[edge.vertexB].position).length() < 1e-6f)
                result.report.finite = result.success = false;
        return result;
    }
    Eigen::VectorXd forces(const BestFitForceDensityResult& result) const {
        Eigen::VectorXd values(m_rows.size());
        for (int i = 0; i < int(m_rows.size()); ++i) {
            auto [v,a] = m_rows[i]; values[i] = component(result.report.residuals[v], a);
        }
        return values;
    }
    double objective(const Eigen::VectorXd& x, const BestFitForceDensityResult& result) const {
        double objective = m_weight * forces(result).squaredNorm() / (m_forceScale*m_forceScale);
        const int e = int(m_form.activeFormEdges.size());
        for (int i = 0; i < int(m_coordinates.size()); ++i)
            objective += m_preferences[i] * std::pow(x[e+i]-m_reference[e+i], 2);
        return objective;
    }
    void describe(const char* state) {
        std::ostringstream message;
        message << state << " " << m_result.iteration << "/" << m_settings.maximumIterations
                << " | all-node H " << m_result.report.horizontalResidual
                << " | free Z " << m_result.report.verticalResidual
                << (m_result.success ? " | vertical reactions verified" : " | NOT equilibrated");
        m_result.diagnostic = message.str();
    }
    void finish(BestFitStatus status) {
        m_result.status = status;
        describe(status == BestFitStatus::Cancelled ? "Cancelled" : status == BestFitStatus::Converged ? "Converged" :
            status == BestFitStatus::IterationLimit ? "Iteration limit" : status == BestFitStatus::Failed ? "Failed" : "Stagnated");
    }
    TnaFormDiagram m_form;
    BestFitForceDensitySettings m_settings;
    BestFitForceDensityResult m_result;
    std::vector<std::pair<int,int>> m_coordinates, m_rows;
    std::vector<double> m_preferences;
    Eigen::VectorXd m_x, m_reference;
    double m_penalty{50}, m_scale{1}, m_forceScale{1}, m_weight{100}, m_damping{1e-4};
};
}

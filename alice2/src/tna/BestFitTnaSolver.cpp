#include "BestFitTnaSolver.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace alice2 {
bool BestFitTnaSolver::reset(const TnaFormDiagram& design, const BestFitForceDensitySettings& settings) {
    m_tna = {}; m_force = {}; m_report = {}; m_densities.clear(); m_lockedPlan = {};
    m_mesh = design.mesh; m_edges = design.activeFormEdges; m_verticalIterations = 0;
    const bool valid = m_fit.reset(design, settings);
    m_stage = valid ? BestFitTnaStage::Ready : BestFitTnaStage::Failed;
    m_diagnostic = valid ? "1: fit shape in XYZ | 2: TNA with fitted XY fixed" : m_fit.result().diagnostic;
    return valid;
}
bool BestFitTnaSolver::startFit() {
    const auto design = m_fit.design(); const auto settings = m_fit.settings();
    if (!reset(design, settings)) return false;
    m_stage = BestFitTnaStage::Fitting; return true;
}
bool BestFitTnaSolver::running() const {
    return m_stage == BestFitTnaStage::Fitting || m_stage == BestFitTnaStage::Horizontal || m_stage == BestFitTnaStage::Vertical;
}
void BestFitTnaSolver::restoreFit() {
    if (!m_fit.result().success) return;
    m_tna = {}; m_mesh = m_fit.result().mesh; m_edges = m_fit.result().edges;
    m_densities = m_fit.result().forceDensities; m_report = m_fit.result().report;
    auto planar = m_fit.design(); planar.mesh = m_mesh;
    for (auto& v : planar.mesh.vertices) v.position.z = 0;
    m_force = m_tna.makeForceDiagram(planar, m_densities);
    m_stage = BestFitTnaStage::Fitted;
    m_diagnostic = "TNA controls changed; fitted shape retained. Press 2 to solve again";
}
bool BestFitTnaSolver::exportable() const {
    return (m_stage == BestFitTnaStage::Fitted || m_stage == BestFitTnaStage::Complete) &&
           m_report.finite && m_report.residual <= m_fit.settings().loads.residualTolerance;
}
void BestFitTnaSolver::fail(const std::string& message) { m_stage = BestFitTnaStage::Failed; m_diagnostic = message; }
void BestFitTnaSolver::measure() {
    m_report = measureTnaEquilibrium(m_mesh, m_fit.design().mesh, m_edges, m_densities,
                                    m_fit.design().supportVertices, m_fit.settings().loads);
}

bool BestFitTnaSolver::startTna(int iterations, float angleTolerance, float tensionDistance) {
    if (running() || !m_fit.result().success) {
        m_diagnostic = "Finish a valid step-1 shape before starting TNA"; return false;
    }
    if (iterations < 1 || !std::isfinite(angleTolerance) || angleTolerance < 0 ||
        !std::isfinite(tensionDistance)) { m_diagnostic = "Invalid TNA settings"; return false; }
    m_tna = {}; m_verticalIterations = 0;
    m_mesh = m_fit.result().mesh; m_lockedPlan = m_mesh;
    m_densities = m_fit.result().forceDensities; m_edges = m_fit.result().edges;
    TnaFormDiagram planar = m_fit.design(); planar.mesh = m_mesh;
    for (auto& v : planar.mesh.vertices) v.position.z = 0.0f;
    m_force = m_tna.makeForceDiagram(planar, m_densities);
    if (!m_force.success) { fail(m_force.diagnostic); return false; }
    // Active-form and dual edge order currently coincide. Explicit endpoint
    // lookup below preserves density ownership if dual ordering changes later.
    std::vector<float> mapped;
    for (const auto& e : m_force.reciprocalFormEdges) {
        auto found = std::find_if(m_edges.begin(), m_edges.end(), [&](const TnaEdge& a) {
            return (a.vertexA == e.vertexA && a.vertexB == e.vertexB) ||
                   (a.vertexA == e.vertexB && a.vertexB == e.vertexA);
        });
        if (found == m_edges.end()) { fail("Density transfer lost an active member"); return false; }
        mapped.push_back(m_densities[std::distance(m_edges.begin(), found)]);
    }
    m_edges = m_force.reciprocalFormEdges; m_densities = std::move(mapped);
    if (tensionDistance != 0.0f) {
        std::vector<int> valence(m_force.mesh.vertices.size(), 0);
        std::vector<bool> tension(m_force.mesh.vertices.size(), true);
        for (std::size_t i = 0; i < m_force.mesh.edges.size(); ++i) {
            const auto& e = m_force.mesh.edges[i];
            for (int v : {e.vertexA, e.vertexB}) { ++valence[v]; tension[v] = tension[v] && m_edges[i].isTension; }
        }
        Vec3 centre(0, 0, 0);
        for (int v = 0; v < static_cast<int>(valence.size()); ++v) if (valence[v] && tension[v]) {
            m_force.fixedForceVertices.push_back(v); centre += m_force.mesh.vertices[v].position;
        }
        if (!m_force.fixedForceVertices.empty()) centre /= static_cast<float>(m_force.fixedForceVertices.size());
        for (int v : m_force.fixedForceVertices) {
            auto& position = m_force.mesh.vertices[v].position;
            position += (position - centre).normalized() * tensionDistance;
        }
    }
    measure();
    const bool reciprocal = std::all_of(m_force.edgeDirectionDeviationsDegrees.begin(),
        m_force.edgeDirectionDeviationsDegrees.end(), [&](float angle) { return std::isfinite(angle) && angle <= angleTolerance; });
    if (tensionDistance == 0.0f && reciprocal && m_report.finite &&
        m_report.residual <= m_fit.settings().loads.residualTolerance) {
        m_stage = BestFitTnaStage::Complete;
        m_diagnostic = "Step 2 verified: fitted shape already in equilibrium; geometry and densities preserved";
        return true;
    }
    if (!m_tna.resetHorizontalEquilibrium(planar.mesh, m_force, planar.supportVertices)) {
        fail(m_tna.horizontalEquilibrium().diagnostic); return false;
    }
    m_horizontal = {}; m_horizontal.formWeight = 1.0f;
    m_horizontal.maximumIterations = iterations; m_horizontal.angleToleranceDegrees = angleTolerance;
    for (std::size_t i = 0; i < m_force.mesh.edges.size(); ++i) {
        const auto& edge = m_force.mesh.edges[i];
        const float length = (m_force.mesh.vertices[edge.vertexA].position - m_force.mesh.vertices[edge.vertexB].position).length();
        TnaHorizontalSettings::EdgeConstraint constraint;
        constraint.isTension = m_edges[i].isTension;
        constraint.forceLengthMinimum = std::max(1e-6f, length * 0.1f);
        constraint.forceLengthMaximum = std::max(1e-5f, length * 5.0f);
        m_horizontal.edgeConstraints.push_back(constraint);
    }
    m_vertical = m_fit.settings().loads;
    m_vertical.forceScale = 1.0f; m_vertical.supportHeights.clear(); m_vertical.initialHeights.clear();
    for (const auto& v : m_fit.design().mesh.vertices) m_vertical.supportHeights.push_back(v.position.z);
    for (const auto& v : m_mesh.vertices) m_vertical.initialHeights.push_back(v.position.z);
    m_stage = BestFitTnaStage::Horizontal;
    m_diagnostic = "Step 2: resolving reciprocal forces with step-1 XY locked";
    return true;
}

void BestFitTnaSolver::step() {
    if (m_stage == BestFitTnaStage::Fitting) {
        m_fit.step();
        const auto& fit = m_fit.result(); m_diagnostic = fit.diagnostic;
        if (fit.success) {
            m_mesh = fit.mesh; m_edges = fit.edges; m_densities = fit.forceDensities; m_report = fit.report;
        }
        if (fit.status != BestFitStatus::Running) {
            m_stage = fit.success ? BestFitTnaStage::Fitted : BestFitTnaStage::Failed;
            if (fit.success) {
                auto planar = m_fit.design(); planar.mesh = fit.mesh;
                for (auto& v : planar.mesh.vertices) v.position.z = 0;
                m_force = m_tna.makeForceDiagram(planar, fit.forceDensities);
            }
        }
    } else if (m_stage == BestFitTnaStage::Horizontal) {
        m_tna.stepHorizontalEquilibrium(m_horizontal);
        const auto& h = m_tna.horizontalEquilibrium();
        m_force.mesh = h.forceDiagram; m_force.edgeAnglesDegrees = h.edgeAnglesDegrees;
        m_force.edgeDirectionDeviationsDegrees = h.edgeDirectionDeviationsDegrees;
        m_densities = h.forceDensities; measure();
        m_diagnostic = h.diagnostic;
        if (!h.converged) return;
        if (!h.satisfiesAngleTarget || !m_report.finite ||
            m_report.horizontalResidual > m_fit.settings().loads.residualTolerance) {
            std::ostringstream message;
            message << "Step 2 not equilibrated: fixed-XY horizontal residual " << m_report.horizontalResidual
                    << ", angle " << h.maximumDirectionDeviation << "; increase H iterations or revise constraints";
            fail(message.str()); return;
        }
        m_stage = BestFitTnaStage::Vertical;
    } else if (m_stage == BestFitTnaStage::Vertical) {
        // One self-weight update per frame; seed the next call from its result.
        auto settings = m_vertical; settings.maximumIterations = 1;
        m_tna.solveVerticalEquilibrium(settings);
        const auto& vertical = m_tna.verticalEquilibrium();
        if (vertical.iteration == 0) { fail(vertical.diagnostic); return; }
        ++m_verticalIterations; m_mesh = vertical.formDiagram; m_densities = vertical.forceDensities;
        for (std::size_t i = 0; i < m_mesh.vertices.size(); ++i) {
            m_mesh.vertices[i].position.x = m_lockedPlan.vertices[i].position.x;
            m_mesh.vertices[i].position.y = m_lockedPlan.vertices[i].position.y;
            m_vertical.initialHeights[i] = m_mesh.vertices[i].position.z;
        }
        measure();
        std::ostringstream message;
        message << "Step 2 " << (m_report.finite && m_report.residual <= m_vertical.residualTolerance ? "equilibrated" : "vertical")
                << ": residual " << m_report.residual << ", design RMS " << m_report.rmsDesignDistance
                << ", iteration " << m_verticalIterations;
        m_diagnostic = message.str();
        if (!m_report.finite) fail("Step 2 produced nonfinite residuals");
        else if (m_report.residual <= m_vertical.residualTolerance) m_stage = BestFitTnaStage::Complete;
        else if (m_verticalIterations >= m_vertical.maximumIterations) fail("Step 2 iteration limit: " + m_diagnostic);
    }
}
} // namespace alice2

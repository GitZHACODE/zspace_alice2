#include "TnaOffsetSolver.h"
#include <algorithm>
#include <map>
#include <sstream>

namespace alice2 {
bool TnaOffsetSolver::running() const {
    return m_stage == TnaOffsetStage::CompressionFitting || m_stage == TnaOffsetStage::Horizontal ||
        m_stage == TnaOffsetStage::Vertical || m_stage == TnaOffsetStage::CableFitting;
}
void TnaOffsetSolver::fail(const std::string& message) {
    m_stage = TnaOffsetStage::Failed; m_result.success = false;
    m_result.status = BestFitStatus::Failed; m_result.diagnostic = message;
}
bool TnaOffsetSolver::reset(const MeshData& input, const TnaOffsetSettings& settings) {
    m_settings = settings; m_input = input; m_compression = {}; m_compressionEdges.clear();
    m_compressionQ.clear(); m_compressionReport = {}; m_tna = {}; m_form = {}; m_force = {};
    m_fittedCompression = {}; m_tnaVerified = m_offsetBuiltFromTna = false;
    m_horizontalSteps = m_verticalSteps = 0; m_result = {}; m_network = {};
    m_stage = TnaOffsetStage::Ready;
    if (!std::isfinite(settings.offset) || settings.offset >= -1e-5f ||
        settings.horizontalIterations < 1 || settings.verticalIterations < 1 || settings.fitIterations < 1 ||
        !std::isfinite(settings.formWeight) || settings.formWeight < 0 || settings.formWeight > 1 ||
        !std::isfinite(settings.forceScale) || settings.forceScale <= 0 ||
        !std::isfinite(settings.tensionSeedRatio) || settings.tensionSeedRatio <= 0 ||
        !std::isfinite(settings.residualTolerance) || settings.residualTolerance <= 0 ||
        !std::isfinite(settings.nodalLoad) || !std::isfinite(settings.density) || settings.density < 0 ||
        !std::isfinite(settings.angleTolerance) || settings.angleTolerance < 0) {
        fail("Invalid three-stage settings"); return false;
    }
    try { m_network = makeOffsetCableNetwork(input, settings.offset, true); }
    catch (const std::exception& e) { fail(e.what()); return false; }
    m_fitSettings = offsetCableSettings(m_network, settings.nodalLoad, settings.density);
    m_fitSettings.maximumIterations = settings.fitIterations; m_fitSettings.designAttraction = settings.attraction;
    m_fitSettings.loads.residualTolerance = settings.residualTolerance;
    if (!m_fit.reset(m_network.form, m_fitSettings, settings.redPenalty, m_network.yellowSupports, settings.yellowPenalty)) {
        fail(m_fit.result().diagnostic); return false;
    }
    m_compression = input;
    m_result.mesh = input; m_result.status = BestFitStatus::Idle;
    m_result.diagnostic = "Ready: q compression fit | t compression TNA | c offset cable fit";
    return true;
}
bool TnaOffsetSolver::startCompressionFit() {
    if (running()) { m_result.diagnostic = "Cancel the current stage with Esc before restarting"; return false; }
    const auto input = m_input; const auto settings = m_settings;
    if (!reset(input,settings)) return false;
    // Without cables, red/yellow anchors must temporarily carry horizontal thrust.
    m_tnaSupports = m_network.form.supportVertices;
    m_tnaSupports.insert(m_tnaSupports.end(),m_network.sliding.begin(),m_network.sliding.end());
    m_tnaSupports.insert(m_tnaSupports.end(),m_network.yellowSupports.begin(),m_network.yellowSupports.end());
    std::sort(m_tnaSupports.begin(),m_tnaSupports.end());
    m_tnaSupports.erase(std::unique(m_tnaSupports.begin(),m_tnaSupports.end()),m_tnaSupports.end());
    // Keep reciprocal-compatible topology for the next stage. Outside faces
    // contribute neither physical compression members nor self-weight.
    m_form = m_tna.makeFormDiagram(input,m_tnaSupports);
    if (!m_form.success) { fail("Compression topology: " + m_form.diagnostic); return false; }
    auto design = m_form; design.supportVertices = m_tnaSupports;
    m_compressionSettings = {}; m_compressionSettings.allowSlidingTensionSupports = false;
    m_compressionSettings.maximumIterations = settings.fitIterations;
    m_compressionSettings.designAttraction = settings.attraction;
    m_compressionSettings.loads.nodalLoad = settings.nodalLoad;
    m_compressionSettings.loads.density = settings.density;
    m_compressionSettings.loads.residualTolerance = settings.residualTolerance;
    m_compressionSettings.loads.unloadedFaces = m_form.exteriorFormFaces;
    if (!m_compressionFit.reset(design,m_compressionSettings)) {
        fail(m_compressionFit.result().diagnostic); return false;
    }
    m_compression = design.mesh; m_compressionEdges = design.activeFormEdges; m_compressionQ.clear();
    m_result = m_compressionFit.result(); m_stage = TnaOffsetStage::CompressionFitting;
    m_result.diagnostic = "1: compression best fit and positive densities";
    return true;
}
void TnaOffsetSolver::configure(const TnaOffsetSettings& settings) {
    if (running()) throw std::logic_error("Stop the active stage before changing settings");
    if (settings.nodalLoad != m_settings.nodalLoad || settings.density != m_settings.density ||
        settings.attraction != m_settings.attraction) {
        const auto input = m_input; reset(input,settings); return;
    }
    const bool tnaChanged = settings.formWeight != m_settings.formWeight || settings.forceScale != m_settings.forceScale ||
        settings.horizontalIterations != m_settings.horizontalIterations || settings.angleTolerance != m_settings.angleTolerance;
    m_settings = settings;
    if (tnaChanged && m_fittedCompression.success) {
        m_tnaVerified = m_offsetBuiltFromTna = false; m_force = {};
        m_compression = m_fittedCompression.mesh; m_compressionQ = m_fittedCompression.forceDensities;
        m_compressionEdges = m_fittedCompression.edges; m_compressionReport = m_fittedCompression.report;
        m_stage = TnaOffsetStage::CompressionFitted; m_result = m_fittedCompression;
        m_result.diagnostic = "TNA settings changed; compression fit retained | press t";
    } else if (m_tnaVerified) {
        m_stage = TnaOffsetStage::TnaComplete; m_offsetBuiltFromTna = false; publishCompression();
        m_result.status = BestFitStatus::Converged; m_result.success = true;
        m_result.diagnostic = "Cable-fit settings changed; compression TNA retained | press c";
    }
}
bool TnaOffsetSolver::startTna() {
    if (running() || !m_fittedCompression.success) {
        m_result.diagnostic = "Finish a valid compression fit with q before pressing t"; return false;
    }
    m_tna = {}; m_force = {}; m_horizontalSteps = m_verticalSteps = 0;
    m_tnaVerified = m_offsetBuiltFromTna = false;
    m_compression = m_fittedCompression.mesh; m_compressionEdges = m_form.activeFormEdges;
    m_compressionQ = m_fittedCompression.forceDensities;
    for (auto& q : m_compressionQ) q *= m_settings.forceScale;
    auto planar = m_form; planar.mesh = m_compression;
    for (auto& v : planar.mesh.vertices) v.position.z = 0;
    // Preserve the fitted density field; do not return to centroid force seeds.
    m_force = m_tna.makeForceDiagram(planar,m_compressionQ);
    if (!m_force.success) { fail("Fitted compression reciprocal: " + m_force.diagnostic); return false; }
    for (const auto& e : m_force.reciprocalFormEdges)
        if ((planar.mesh.vertices[e.vertexA].position-planar.mesh.vertices[e.vertexB].position).length() < 1e-6f) {
            fail("Compression TNA requires nonzero XY edge lengths"); return false;
        }
    if (!m_tna.resetHorizontalEquilibrium(planar.mesh,m_force,m_tnaSupports)) {
        fail(m_tna.horizontalEquilibrium().diagnostic); return false;
    }
    m_horizontal = {}; m_horizontal.formWeight = m_settings.formWeight;
    m_horizontal.maximumIterations = m_settings.horizontalIterations;
    m_horizontal.angleToleranceDegrees = m_settings.angleTolerance;
    // Scale is already applied to the seed, so do not apply it twice.
    for (const auto& e : m_force.mesh.edges) {
        const float length = (m_force.mesh.vertices[e.vertexA].position-m_force.mesh.vertices[e.vertexB].position).length();
        TnaHorizontalSettings::EdgeConstraint c;
        c.forceLengthMinimum = std::max(1e-6f,length*.01f);
        c.forceLengthMaximum = std::max(1e-5f,length*100.f);
        m_horizontal.edgeConstraints.push_back(c);
    }
    m_vertical = m_compressionSettings.loads; m_vertical.maximumIterations = m_settings.verticalIterations;
    m_vertical.supportHeights.clear(); m_vertical.initialHeights.clear();
    for (const auto& v : m_input.vertices) m_vertical.supportHeights.push_back(v.position.z);
    for (const auto& v : m_compression.vertices) m_vertical.initialHeights.push_back(v.position.z);
    m_stage = TnaOffsetStage::Horizontal; m_result.success = false; m_result.status = BestFitStatus::Running;
    m_result.diagnostic = "2a: TNA horizontal equilibrium from fitted compression densities";
    measureCompression(); publishCompression();
    return true;
}
void TnaOffsetSolver::measureCompression() {
    m_compressionReport = measureTnaEquilibrium(m_compression,m_input,m_compressionEdges,
        m_compressionQ,m_tnaSupports,m_compressionSettings.loads,false,0);
}
void TnaOffsetSolver::publishCompression() {
    m_result.mesh = m_compression; m_result.edges = m_compressionEdges;
    m_result.forceDensities = m_compressionQ; m_result.report = m_compressionReport;
}
bool TnaOffsetSolver::startCableFit() {
    if (running() || !m_tnaVerified) {
        m_result.diagnostic = "Finish verified compression TNA with t before pressing c"; return false;
    }
    auto physical = m_compression; physical.faces = m_input.faces; physical.edges.clear();
    try { m_network = makeOffsetCableNetwork(physical,m_settings.offset,true); }
    catch (const std::exception& e) { fail("Offset after TNA: " + std::string(e.what())); return false; }
    m_fitSettings = offsetCableSettings(m_network,m_settings.nodalLoad,m_settings.density);
    m_fitSettings.maximumIterations = m_settings.fitIterations; m_fitSettings.designAttraction = m_settings.attraction;
    m_fitSettings.loads.residualTolerance = m_settings.residualTolerance;
    std::map<std::pair<int,int>,float> qByEdge;
    for (size_t i = 0; i < m_compressionEdges.size(); ++i) {
        const auto& e = m_compressionEdges[i]; qByEdge[std::minmax(e.vertexA,e.vertexB)] = m_compressionQ[i];
    }
    auto sorted = m_compressionQ; std::sort(sorted.begin(),sorted.end());
    if (sorted.empty()) { fail("Compression TNA returned no densities"); return false; }
    const float lower = float(m_fitSettings.minimumDensityMagnitude*1.01);
    const float median = std::max(lower,sorted[sorted.size()/2]);
    const float tension = -std::max(lower,median*m_settings.tensionSeedRatio);
    std::vector<float> seed;
    for (const auto& e : m_network.form.activeFormEdges) {
        if (e.isTension) { seed.push_back(tension); continue; }
        const auto found = qByEdge.find(std::minmax(e.vertexA,e.vertexB));
        seed.push_back(found == qByEdge.end() ? median : std::max(lower,found->second));
    }
    if (!m_fit.reset(m_network.form,m_fitSettings,m_settings.redPenalty,m_network.yellowSupports,m_settings.yellowPenalty,seed)) {
        fail(m_fit.result().diagnostic); return false;
    }
    m_result = m_fit.result(); m_stage = TnaOffsetStage::CableFitting; m_offsetBuiltFromTna = true;
    m_result.diagnostic = "3: offset cables + compression, fitting vertical-only support reactions";
    return true;
}
void TnaOffsetSolver::step(std::stop_token stop) {
    if (!running()) return;
    if (stop.stop_requested()) {
        m_stage = TnaOffsetStage::Cancelled; m_result.status = BestFitStatus::Cancelled;
        m_result.diagnostic = "Cancelled current stage; q restarts, t repeats from fitted compression"; return;
    }
    if (m_stage == TnaOffsetStage::CompressionFitting) {
        m_compressionFit.step(stop); m_result = m_compressionFit.result();
        m_result.diagnostic = "1: " + m_result.diagnostic;
        if (m_result.success) {
            m_compression = m_result.mesh; m_compressionEdges = m_result.edges;
            m_compressionQ = m_result.forceDensities; m_compressionReport = m_result.report;
        }
        if (m_result.status != BestFitStatus::Running) {
            m_fittedCompression = m_result;
            m_stage = m_result.status == BestFitStatus::Cancelled ? TnaOffsetStage::Cancelled :
                m_result.success ? TnaOffsetStage::CompressionFitted : TnaOffsetStage::Failed;
            if (m_result.success) m_result.diagnostic += " | press t for TNA";
        }
    } else if (m_stage == TnaOffsetStage::Horizontal) {
        m_tna.stepHorizontalEquilibrium(m_horizontal); ++m_horizontalSteps;
        const auto& h = m_tna.horizontalEquilibrium();
        if (!h.success) { fail(h.diagnostic); return; }
        m_result.diagnostic = "2a: " + h.diagnostic;
        if (m_horizontalSteps%20 == 0 || h.converged) {
            m_force.mesh = h.forceDiagram; m_force.edgeAnglesDegrees = h.edgeAnglesDegrees;
            m_force.edgeDirectionDeviationsDegrees = h.edgeDirectionDeviationsDegrees;
            m_compression = h.formDiagram;
            // Horizontal solve must not display the flattening as a height change.
            for (size_t i = 0; i < m_compression.vertices.size(); ++i)
                m_compression.vertices[i].position.z = m_fittedCompression.mesh.vertices[i].position.z;
            m_compressionEdges = h.reciprocalFormEdges; m_compressionQ = h.forceDensities;
            measureCompression(); publishCompression();
        }
        if (!h.converged) return;
        if (!h.satisfiesAngleTarget || !m_compressionReport.finite ||
            m_compressionReport.horizontalResidual > m_settings.residualTolerance) {
            std::ostringstream s; s << "2: TNA horizontal not equilibrated: H=" << m_compressionReport.horizontalResidual
                << ", angle=" << h.maximumDirectionDeviation;
            fail(s.str()); return;
        }
        m_stage = TnaOffsetStage::Vertical; m_result.diagnostic = "2b: compression TNA vertical equilibrium";
    } else if (m_stage == TnaOffsetStage::Vertical) {
        auto settings = m_vertical; settings.maximumIterations = 1;
        m_tna.solveVerticalEquilibrium(settings);
        const auto& v = m_tna.verticalEquilibrium();
        if (!v.iteration) { fail("2: compression vertical TNA: " + v.diagnostic); return; }
        ++m_verticalSteps; m_compression = v.formDiagram; m_compressionQ = v.forceDensities;
        for (size_t i = 0; i < m_compression.vertices.size(); ++i)
            m_vertical.initialHeights[i] = m_compression.vertices[i].position.z;
        measureCompression(); publishCompression();
        m_result.diagnostic = "2b: " + v.diagnostic;
        if (!m_compressionReport.finite) { fail("Nonfinite compression TNA equilibrium"); return; }
        if (m_compressionReport.residual <= m_settings.residualTolerance) {
            m_tnaVerified = true; m_stage = TnaOffsetStage::TnaComplete;
            m_result.success = true; m_result.status = BestFitStatus::Converged;
            m_result.diagnostic = "2: compression TNA verified | press c to build and fit cables";
        } else if (m_verticalSteps >= m_settings.verticalIterations)
            fail("2: compression vertical TNA iteration limit: residual=" + std::to_string(m_compressionReport.residual));
    } else {
        m_fit.step(stop); m_result = m_fit.result(); m_result.diagnostic = "3: " + m_result.diagnostic;
        if (m_result.status != BestFitStatus::Running)
            m_stage = m_result.status == BestFitStatus::Cancelled ? TnaOffsetStage::Cancelled :
                m_result.success ? TnaOffsetStage::Complete : TnaOffsetStage::Failed;
    }
}
} // namespace alice2

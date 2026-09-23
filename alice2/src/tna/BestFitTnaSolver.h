#pragma once
#include "BestFitForceDensitySolver.h"

namespace alice2 {
enum class BestFitTnaStage { Ready, Fitting, Fitted, Horizontal, Vertical, Complete, Failed };

// Owns the stage boundary. In particular, the step-2 plan is never regenerated
// from the original design and display offsets never enter solver coordinates.
class BestFitTnaSolver {
public:
    bool reset(const TnaFormDiagram& design, const BestFitForceDensitySettings& settings);
    bool startFit();
    void restoreFit();
    bool startTna(int horizontalIterations = 500, float angleTolerance = 3.0f,
                  float tensionVertexDistance = 0.0f);
    void step(std::stop_token stop = {});
    BestFitTnaStage stage() const { return m_stage; }
    bool running() const;
    bool exportable() const;
    const std::string& diagnostic() const { return m_diagnostic; }
    const MeshData& mesh() const { return m_mesh; }
    const TnaForceDiagram& forceDiagram() const { return m_force; }
    const std::vector<TnaEdge>& edges() const { return m_edges; }
    const std::vector<float>& densities() const { return m_densities; }
    const TnaEquilibriumReport& report() const { return m_report; }
    const BestFitForceDensityResult& fitResult() const { return m_fit.result(); }
private:
    void measure();
    void fail(const std::string& message);
    BestFitForceDensitySolver m_fit;
    TnaSolver m_tna;
    BestFitTnaStage m_stage{BestFitTnaStage::Ready};
    TnaHorizontalSettings m_horizontal;
    TnaVerticalSettings m_vertical;
    MeshData m_mesh;
    MeshData m_lockedPlan;
    TnaForceDiagram m_force;
    std::vector<TnaEdge> m_edges;
    std::vector<float> m_densities;
    TnaEquilibriumReport m_report;
    std::string m_diagnostic;
    int m_verticalIterations{0};
};
} // namespace alice2

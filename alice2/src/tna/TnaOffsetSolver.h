#pragma once
#include "OffsetCableNetwork.h"
#include "VerticalReactionFit.h"

namespace alice2 {
enum class TnaOffsetStage { Ready, CompressionFitting, CompressionFitted, Horizontal, Vertical, TnaComplete, CableFitting, Complete, Failed, Cancelled };
struct TnaOffsetSettings {
    float offset{-.05f}, nodalLoad{.001f}, density{1};
    int horizontalIterations{10000}, verticalIterations{100}, fitIterations{50};
    float formWeight{1}, forceScale{1}, angleTolerance{3}, residualTolerance{1e-3f};
    double attraction{1}, redPenalty{0}, yellowPenalty{50};
    // New tension members have no TNA density; initialise relative to compression.
    float tensionSeedRatio{.1f};
};

// Three manually triggered stages: compression best fit, reciprocal TNA,
// then the spatial mixed-sign cable fit. No stage automatically starts the next.
class TnaOffsetSolver {
public:
    bool reset(const MeshData& weldedInput, const TnaOffsetSettings& settings = {});
    void configure(const TnaOffsetSettings& settings);
    bool startCompressionFit();
    bool startTna();
    bool startCableFit();
    void step(std::stop_token stop = {});
    bool running() const;
    TnaOffsetStage stage() const { return m_stage; }
    const std::string& diagnostic() const { return m_result.diagnostic; }
    const BestFitForceDensityResult& result() const { return m_result; }
    const OffsetCableNetwork& network() const { return m_network; }
    const BestFitForceDensitySettings& fitSettings() const { return m_fitSettings; }
    const MeshData& input() const { return m_input; }
    const MeshData& compressionReference() const { return m_compression; }
    const std::vector<TnaEdge>& compressionEdges() const { return m_compressionEdges; }
    const std::vector<float>& compressionDensities() const { return m_compressionQ; }
    const TnaEquilibriumReport& compressionReport() const { return m_compressionReport; }
    const TnaForceDiagram& forceDiagram() const { return m_force; }
    const BestFitForceDensityResult& compressionFitResult() const { return m_fittedCompression; }
    bool tnaVerified() const { return m_tnaVerified; }
    int horizontalSteps() const { return m_horizontalSteps; }
    int verticalSteps() const { return m_verticalSteps; }
    bool offsetBuiltFromTna() const { return m_offsetBuiltFromTna; }
private:
    void fail(const std::string& message);
    void measureCompression();
    void publishCompression();
    TnaOffsetSettings m_settings;
    TnaOffsetStage m_stage{TnaOffsetStage::Ready};
    MeshData m_input, m_compression;
    OffsetCableNetwork m_network;
    TnaSolver m_tna;
    TnaFormDiagram m_form;
    TnaHorizontalSettings m_horizontal;
    TnaVerticalSettings m_vertical;
    BestFitForceDensitySettings m_fitSettings;
    BestFitForceDensitySettings m_compressionSettings;
    BestFitForceDensitySolver m_compressionFit;
    BestFitForceDensityResult m_fittedCompression;
    TnaForceDiagram m_force;
    VerticalReactionFit m_fit;
    BestFitForceDensityResult m_result;
    std::vector<int> m_tnaSupports;
    std::vector<TnaEdge> m_compressionEdges;
    std::vector<float> m_compressionQ;
    TnaEquilibriumReport m_compressionReport;
    int m_horizontalSteps{0}, m_verticalSteps{0};
    bool m_offsetBuiltFromTna{false};
    bool m_tnaVerified{false};
};
int runTnaOffsetChecks(const char* objPath = nullptr, int fitIterations = 50, float formWeight = 1, int horizontalIterations = 10000);
} // namespace alice2

#pragma once

#include <alice2.h>

#include <string>
#include <utility>
#include <vector>

#include "ML/Autoencoder.h"
#include "ML/DCT.h"
#include "ML/GridField.h"

// ====================== Configurations ======================
struct WaveLatentConfig {
    int Kx = 32;
    int Ky = 32;
    int keepK = 512;
    int latentDim = 16;
    unsigned seed = 1234;
    std::vector<int> encoderHidden{16};
    std::vector<int> decoderHidden{16};
};

struct WaveLatentTrainingParams {
    int epochs = 200;
    float learningRate = 5e-3f;
    float weightDecay = 1e-6f;
    float latentReg = 1e-3f;
    int printEvery = 50;
};

struct WaveLatentDataset {
    int gridResX = 0;
    int gridResY = 0;
    float xMin = 0.f;
    float xMax = 0.f;
    float yMin = 0.f;
    float yMax = 0.f;
    std::vector<std::vector<float>> fields;

    bool empty() const { return fields.empty(); }
    size_t sampleCount() const { return fields.size(); }
    size_t fieldSize() const { return fields.empty() ? 0u : fields.front().size(); }
};

struct WaveLatentDatasetOptions {
    int gridResX = 256;
    int gridResY = 256;
    float xMin = -1.2f;
    float xMax = 1.2f;
    float yMin = -1.2f;
    float yMax = 1.2f;
};

// ====================== WaveLatent Core ======================
class WaveLatent {
public:
    WaveLatent();

    void clear();

    bool setFields(int height, int width, const std::vector<std::vector<float>>& fields);
    bool setFields(const WaveLatentDataset& dataset);
    bool initialize(const WaveLatentConfig& config);

    static bool loadDatasetFromJson(const std::string& filePath,
                                    WaveLatentDataset& dataset,
                                    const WaveLatentDatasetOptions& options = WaveLatentDatasetOptions{});

    bool ready() const;

    void train(const WaveLatentTrainingParams& params);

    bool encodeSample(int index, std::vector<float>& z) const;
    void decodeLatentToGrid(const std::vector<float>& z, std::vector<float>& gridOut) const;

    bool reconstructBasisOnly(int index, std::vector<float>& gridOut) const;
    bool reconstructAutoencoder(int index, std::vector<float>& gridOut) const;
    bool blendSamples(int indexA, int indexB, float t, std::vector<float>& gridOut) const;

    void getAllLatents(std::vector<std::vector<float>>& latentsOut) const;
    void printDiagnostics() const;
    void printCrossID() const;

    bool saveModel(const std::string& filePath) const;
    bool loadModel(const std::string& filePath);

    // Accessors
    int gridWidth() const { return W_; }
    int gridHeight() const { return H_; }
    int sampleCount() const { return static_cast<int>(coeffs_.size()); }
    int basisRank() const { return static_cast<int>(selectedModes_.size()); }
    int fullBasisRank() const { return basisFull_.Kfull(); }
    int latentDim() const { return config_.latentDim; }
    const WaveLatentConfig& config() const { return config_; }
    float domainXMin() const { return domainXMin_; }
    float domainXMax() const { return domainXMax_; }
    float domainYMin() const { return domainYMin_; }
    float domainYMax() const { return domainYMax_; }
    const WaveLatentTrainingParams& trainingParams() const { return trainingParams_; }
    const std::vector<std::pair<int, int>>& modes() const { return selectedModes_; }
    const std::vector<std::vector<float>>& basis() const { return selectedBasis_; }
    const std::vector<std::vector<float>>& fields() const { return fields_; }
    const std::vector<std::vector<float>>& normalizedCoefficients() const { return coeffs_; }
    const std::vector<float>& coeffMean() const { return coeffMean_; }
    const std::vector<float>& coeffStd() const { return coeffStd_; }
    const std::vector<std::vector<float>>& storedLatents() const { return storedLatents_; }

private:
    void projectFieldsOntoBasis(std::vector<std::vector<float>>& coeffsFull) const;
    void selectTopKBasis(const std::vector<std::vector<float>>& coeffsFull);
    void normaliseCoefficients();
    void rebuildSelectedBasis();
    void initialiseAutoencoder(unsigned seed);

    bool reconstructInternal(int index, bool useAE, std::vector<float>& gridOut) const;
    void latentToCoeffsDenorm(const std::vector<float>& z, std::vector<float>& coeffsDenorm) const;
    void coeffsToGrid(const std::vector<float>& coeffsDenorm, std::vector<float>& gridOut) const;

    int H_ = 0;
    int W_ = 0;
    std::vector<std::vector<float>> fields_;

    WaveLatentConfig config_{};
    WaveLatentTrainingParams trainingParams_{};

    DCT2Basis2D basisFull_;
    std::vector<std::pair<int, int>> selectedModes_;
    std::vector<std::vector<float>> selectedBasis_;

    std::vector<std::vector<float>> coeffs_;
    std::vector<float> coeffMean_;
    std::vector<float> coeffStd_;
    std::vector<std::vector<float>> storedLatents_;

    float domainXMin_ = -1.0f;
    float domainXMax_ = 1.0f;
    float domainYMin_ = -1.0f;
    float domainYMax_ = 1.0f;

    ml::Autoencoder ae_;
};

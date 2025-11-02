#pragma once

#include <cstddef>
#include <random>
#include <string>
#include <vector>

#include "genericMLP.h"

namespace alice2 {

struct AutoDecoderSample {
    int shapeIndex = 0;
    std::vector<float> coordinate;
    float sdf = 0.0f;
};

struct AutoDecoderTrainingConfig {
    int epochs = 1;
    float learningRateWeights = 1e-3f;
    float learningRateLatent = 1e-3f;
    float latentRegularization = 1e-4f;
    float latentInitStd = 0.01f;
    unsigned int shuffleSeed = 1337u;
};

struct AutoDecoderTrainingStats {
    int epochsCompleted = 0;
    float lastLoss = 0.0f;
    float averageLoss = 0.0f;
    std::size_t totalSamples = 0;
};

class AutoDecoderTrainer {
public:
    explicit AutoDecoderTrainer(MLP& decoder);

    void initialize(int numShapes, int latentDim, int coordinateDim);
    void setSamples(const std::vector<AutoDecoderSample>& samples);

    const std::vector<std::vector<float>>& getLatentCodes() const { return latentCodes; }
    void setLatentCodes(const std::vector<std::vector<float>>& z) { latentCodes = z; latentsInitialized = true; }
    void setLatentCodes(std::vector<std::vector<float>>&& z) { latentCodes = std::move(z); latentsInitialized = true; }
    int getLatentDim() const { return latentDim; }
    int getCoordinateDim() const { return coordDim; }

    const MLP& getDecoder() const { return model; }
    MLP&       getDecoder()       { return model; }

    AutoDecoderTrainingStats train(const AutoDecoderTrainingConfig& config);

    bool saveToJson(const std::string& filePath) const;

private:
    MLP& model;
    int coordDim = 0;
    int latentDim = 0;
    bool latentsInitialized = false;

    std::vector<AutoDecoderSample> dataset;
    std::vector<std::vector<float>> latentCodes;
    std::mt19937 rng;

    float lastBatchLoss = 0.0f;
    float runningAverageLoss = 0.0f;
    int epochsRun = 0;
    float lastLatentRegularization = 0.0f;

    void ensureLatentInit(float stdDev);
    std::vector<float> buildInput(int shapeIndex, const std::vector<float>& coordinate) const;
};

} // namespace alice2

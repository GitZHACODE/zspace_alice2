#include "AutoDecoderTrainer.h"

#include <algorithm>
#include <fstream>
#include <numeric>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace alice2 {

using json = nlohmann::json;

AutoDecoderTrainer::AutoDecoderTrainer(MLP& decoder)
    : model(decoder), rng(1337u)
{
}

void AutoDecoderTrainer::initialize(int numShapes, int latentDimIn, int coordinateDim)
{
    if (numShapes <= 0)
        throw std::runtime_error("AutoDecoderTrainer::initialize requires a positive number of shapes.");
    if (latentDimIn <= 0)
        throw std::runtime_error("AutoDecoderTrainer::initialize requires latentDim > 0.");
    if (coordinateDim <= 0)
        throw std::runtime_error("AutoDecoderTrainer::initialize requires coordinateDim > 0.");

    latentDim = latentDimIn;
    coordDim = coordinateDim;
    latentCodes.assign(static_cast<std::size_t>(numShapes), std::vector<float>(latentDim, 0.0f));
    latentsInitialized = false;
}

void AutoDecoderTrainer::setSamples(const std::vector<AutoDecoderSample>& samples)
{
    if (coordDim == 0)
        throw std::runtime_error("AutoDecoderTrainer::setSamples called before initialize.");

    dataset = samples;
    for (const auto& sample : dataset)
    {
        if (sample.coordinate.size() != static_cast<std::size_t>(coordDim))
            throw std::runtime_error("AutoDecoderTrainer sample coordinate dimension mismatch.");
        if (sample.shapeIndex < 0 || sample.shapeIndex >= static_cast<int>(latentCodes.size()))
            throw std::runtime_error("AutoDecoderTrainer sample shape index out of range.");
    }
}

void AutoDecoderTrainer::ensureLatentInit(float stdDev)
{
    if (latentsInitialized)
        return;

    if (stdDev <= 0.0f)
        stdDev = 0.01f;

    std::normal_distribution<float> dist(0.0f, stdDev);
    for (auto& code : latentCodes)
        for (float& value : code)
            value = dist(rng);

    latentsInitialized = true;
}

std::vector<float> AutoDecoderTrainer::buildInput(int shapeIndex, const std::vector<float>& coordinate) const
{
    if (shapeIndex < 0 || shapeIndex >= static_cast<int>(latentCodes.size()))
        throw std::runtime_error("AutoDecoderTrainer::buildInput shape index out of range.");
    if (coordinate.size() != static_cast<std::size_t>(coordDim))
        throw std::runtime_error("AutoDecoderTrainer::buildInput coordinate dimension mismatch.");

    std::vector<float> input;
    input.reserve(static_cast<std::size_t>(latentDim + coordDim));
    const auto& latent = latentCodes[static_cast<std::size_t>(shapeIndex)];
    input.insert(input.end(), latent.begin(), latent.end());
    input.insert(input.end(), coordinate.begin(), coordinate.end());
    return input;
}

AutoDecoderTrainingStats AutoDecoderTrainer::train(const AutoDecoderTrainingConfig& config)
{
    AutoDecoderTrainingStats stats;

    if (config.epochs <= 0)
        return stats;
    if (dataset.empty())
        throw std::runtime_error("AutoDecoderTrainer::train requires a non-empty dataset.");
    if (latentCodes.empty())
        throw std::runtime_error("AutoDecoderTrainer::train requires initialize to be called before training.");
    if (model.inputDim != latentDim + coordDim)
        throw std::runtime_error("AutoDecoderTrainer::train decoder input dimension does not match latent + coordinate dimensions.");
    if (model.outputDim != 1)
        throw std::runtime_error("AutoDecoderTrainer::train expects decoder output dimension to be 1 for SDF values.");

    if (epochsRun == 0)
        rng.seed(config.shuffleSeed);

    ensureLatentInit(config.latentInitStd);

    std::vector<std::size_t> order(dataset.size());
    std::iota(order.begin(), order.end(), 0);

    for (int epoch = 0; epoch < config.epochs; ++epoch)
    {
        std::shuffle(order.begin(), order.end(), rng);

        float epochLoss = 0.0f;

        for (std::size_t idx : order)
        {
            const AutoDecoderSample& sample = dataset[idx];
            std::vector<float> input = buildInput(sample.shapeIndex, sample.coordinate);

            std::vector<float> prediction = model.forward(input);
            if (prediction.empty())
                throw std::runtime_error("AutoDecoderTrainer::train decoder forward produced empty output.");

            float diff = prediction[0] - sample.sdf;
            float sampleLoss = diff * diff;

            std::vector<float> gradOut(1, 2.0f * diff);
            std::vector<float> inputGrad = model.backwardWithInputGrad(gradOut, config.learningRateWeights);

            if (inputGrad.size() != input.size())
                throw std::runtime_error("AutoDecoderTrainer::train received mismatched input gradient size.");

            std::vector<float>& latent = latentCodes[static_cast<std::size_t>(sample.shapeIndex)];

            float latentPenalty = 0.0f;
            if (config.latentRegularization > 0.0f)
            {
                for (float value : latent)
                    latentPenalty += value * value;
                latentPenalty *= config.latentRegularization;
            }

            epochLoss += sampleLoss + latentPenalty;
            lastBatchLoss = sampleLoss + latentPenalty;

            for (int k = 0; k < latentDim; ++k)
            {
                float grad = inputGrad[static_cast<std::size_t>(k)];
                if (config.latentRegularization > 0.0f)
                    grad += 2.0f * config.latentRegularization * latent[static_cast<std::size_t>(k)];
                latent[static_cast<std::size_t>(k)] -= config.learningRateLatent * grad;
            }
        }

        runningAverageLoss = epochLoss / static_cast<float>(dataset.size());
        stats.averageLoss = runningAverageLoss;
        stats.lastLoss = lastBatchLoss;
        stats.epochsCompleted = epoch + 1;
    }

    epochsRun += config.epochs;
    lastLatentRegularization = config.latentRegularization;
    stats.totalSamples = dataset.size() * static_cast<std::size_t>(config.epochs);

    return stats;
}

bool AutoDecoderTrainer::saveToJson(const std::string& filePath) const
{
    if (latentCodes.empty())
        return false;

    json root;

    json decoder;
    decoder["input_dim"] = model.inputDim;
    decoder["output_dim"] = model.outputDim;
    decoder["hidden_dims"] = model.hiddenDims;
    decoder["weights"] = model.W;
    decoder["biases"] = model.b;
    root["decoder"] = decoder;

    root["latent_codes"] = latentCodes;

    json training;
    training["epochs_completed"] = epochsRun;
    training["last_loss"] = lastBatchLoss;
    training["average_loss"] = runningAverageLoss;
    training["latent_regularization"] = lastLatentRegularization;
    training["samples_per_epoch"] = dataset.size();
    root["training"] = training;

    json metadata;
    metadata["num_shapes"] = latentCodes.size();
    metadata["latent_dim"] = latentDim;
    metadata["coordinate_dim"] = coordDim;
    metadata["model_input_dim"] = model.inputDim;
    metadata["model_output_dim"] = model.outputDim;
    root["metadata"] = metadata;

    std::ofstream file(filePath);
    if (!file.is_open())
        return false;

    file << root.dump(4);
    return true;
}

} // namespace alice2

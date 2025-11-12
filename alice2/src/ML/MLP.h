#pragma once

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace ml {

struct HiddenLayer {
    int inDim = 0;
    int outDim = 0;
    bool useReLU = false;
    std::vector<float> weights;
    std::vector<float> bias;
    mutable std::vector<float> preActivation;
    mutable std::vector<float> activation;
    std::vector<float> gradWeights;
    std::vector<float> gradBias;

    void initialize(int in, int out, bool relu, unsigned seed) {
        inDim = in;
        outDim = out;
        useReLU = relu;
        const size_t weightCount = static_cast<size_t>(outDim) * static_cast<size_t>(inDim);
        weights.resize(weightCount);
        gradWeights.assign(weightCount, 0.f);
        bias.assign(outDim, 0.f);
        gradBias.assign(outDim, 0.f);
        preActivation.assign(outDim, 0.f);
        activation.assign(outDim, 0.f);

        std::mt19937 rng(seed);
        const float scale = useReLU ? std::sqrt(2.0f / std::max(1, inDim))
                                    : std::sqrt(1.0f / std::max(1, inDim));
        std::normal_distribution<float> dist(0.f, scale);
        for (auto& w : weights) {
            w = dist(rng);
        }
        const float biasInit = useReLU ? 0.01f : 0.0f;
        bias.assign(outDim, biasInit);
    }

    void forward(const std::vector<float>& input) const {
        if (static_cast<int>(input.size()) != inDim) {
            return;
        }
        for (int o = 0; o < outDim; ++o) {
            float s = bias[o];
            const float* w = &weights[o * inDim];
            for (int i = 0; i < inDim; ++i) {
                s += w[i] * input[i];
            }
            preActivation[o] = s;
            activation[o] = useReLU ? ((s > 0.f) ? s : 0.f) : s;
        }
    }

    void backward(const std::vector<float>& input,
                  const std::vector<float>& gradOutput,
                  std::vector<float>& gradInput,
                  float weightDecay) {
        gradInput.assign(inDim, 0.f);
        std::fill(gradWeights.begin(), gradWeights.end(), 0.f);
        std::fill(gradBias.begin(), gradBias.end(), 0.f);

        for (int o = 0; o < outDim; ++o) {
            float g = gradOutput[o];
            if (useReLU && preActivation[o] <= 0.f) {
                g = 0.f;
            }
            gradBias[o] += g;
            float* gWRow = &gradWeights[o * inDim];
            const float* wRow = &weights[o * inDim];
            for (int i = 0; i < inDim; ++i) {
                gWRow[i] += g * input[i] + weightDecay * wRow[i];
                gradInput[i] += g * wRow[i];
            }
        }
    }

    void sgd(float lr) {
        for (size_t i = 0; i < weights.size(); ++i) {
            weights[i] -= lr * gradWeights[i];
        }
        for (size_t i = 0; i < bias.size(); ++i) {
            bias[i] -= lr * gradBias[i];
        }
    }

    void setParams(const std::vector<float>& newWeights, const std::vector<float>& newBias) {
        if (newWeights.size() == weights.size()) {
            weights = newWeights;
        }
        if (newBias.size() == bias.size()) {
            bias = newBias;
        }
    }
};

} // namespace ml

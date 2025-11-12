#pragma once

#include "ML/MLP.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace ml {

struct LayerDesc {
    int inDim = 0;
    int outDim = 0;
    bool useReLU = false;
    std::vector<float> weights;
    std::vector<float> bias;
};

class Autoencoder {
public:
    void initialize(int inputDim, int latentDim,
                    const std::vector<int>& encoderHidden,
                    const std::vector<int>& decoderHidden,
                    unsigned seed = 1234) {
        clear();
        if (inputDim <= 0 || latentDim <= 0) {
            return;
        }

        buildLayers(inputDim, latentDim, encoderHidden, encoderLayers_, seed);
        buildLayers(latentDim, inputDim, decoderHidden, decoderLayers_, seed ^ 0x9e3779b9u);
        encoderInputs_.assign(encoderLayers_.size() + 1u, {});
        decoderInputs_.assign(decoderLayers_.size() + 1u, {});
        latent_.assign(latentDim, 0.f);
        recon_.assign(inputDim, 0.f);
    }

    void clear() {
        encoderLayers_.clear();
        decoderLayers_.clear();
        encoderInputs_.clear();
        decoderInputs_.clear();
        latent_.clear();
        recon_.clear();
    }

    bool ready() const { return !encoderLayers_.empty() && !decoderLayers_.empty(); }

    int inDim() const { return encoderLayers_.empty() ? 0 : encoderLayers_.front().inDim; }
    int latentDim() const { return encoderLayers_.empty() ? 0 : encoderLayers_.back().outDim; }

    void encode(const std::vector<float>& x, std::vector<float>& z) const {
        runNetwork(x, encoderLayers_, z);
    }

    void decode(const std::vector<float>& z, std::vector<float>& xhat) const {
        runNetwork(z, decoderLayers_, xhat);
    }

    float forward(const std::vector<float>& x) {
        if (!ready()) {
            return 0.f;
        }
        propagateAndStore(x);
        const int outDim = std::max(1, decoderLayers_.back().outDim);
        float L = 0.f;
        const int dim = std::max(1, outDim);
        for (int i = 0; i < outDim; ++i) {
            const float d = recon_[i] - x[i];
            L += 0.5f * d * d;
        }
        return L / float(dim);
    }

    void backward(const std::vector<float>& x, float weightDecay = 0.f, float zReg = 1e-3f) {
        if (!ready()) {
            return;
        }
        const int outDim = decoderLayers_.back().outDim;
        std::vector<float> gradCurrent(outDim, 0.f);
        const float invScale = 1.0f / std::sqrt(float(std::max(1, outDim)));
        for (int i = 0; i < outDim; ++i) {
            gradCurrent[i] = (recon_[i] - x[i]) * invScale;
        }

        std::vector<float> gradInput;
        for (int layer = static_cast<int>(decoderLayers_.size()) - 1; layer >= 0; --layer) {
            decoderLayers_[layer].backward(decoderInputs_[static_cast<size_t>(layer)],
                                           gradCurrent, gradInput, weightDecay);
            gradCurrent.swap(gradInput);
        }

        for (size_t i = 0; i < gradCurrent.size(); ++i) {
            gradCurrent[i] += zReg * latent_[i];
        }

        for (int layer = static_cast<int>(encoderLayers_.size()) - 1; layer >= 0; --layer) {
            encoderLayers_[layer].backward(encoderInputs_[static_cast<size_t>(layer)],
                                           gradCurrent, gradInput, weightDecay);
            gradCurrent.swap(gradInput);
        }
    }

    void sgd(float lr) {
        for (auto& layer : encoderLayers_) {
            layer.sgd(lr);
        }
        for (auto& layer : decoderLayers_) {
            layer.sgd(lr);
        }
    }

    void encoderDesc(std::vector<LayerDesc>& out) const {
        snapshotLayers(encoderLayers_, out);
    }

    void decoderDesc(std::vector<LayerDesc>& out) const {
        snapshotLayers(decoderLayers_, out);
    }

    bool rebuildFromDesc(const std::vector<LayerDesc>& enc,
                         const std::vector<LayerDesc>& dec) {
        encoderLayers_.clear();
        decoderLayers_.clear();
        for (const auto& layer : enc) {
            if (!appendLayerFromDesc(layer, encoderLayers_)) {
                clear();
                return false;
            }
        }
        for (const auto& layer : dec) {
            if (!appendLayerFromDesc(layer, decoderLayers_)) {
                clear();
                return false;
            }
        }
        encoderInputs_.assign(encoderLayers_.size() + 1u, {});
        decoderInputs_.assign(decoderLayers_.size() + 1u, {});
        latent_.assign(latentDim(), 0.f);
        recon_.assign(inDim(), 0.f);
        return ready();
    }

private:
    void buildLayers(int inputDim, int outputDim, const std::vector<int>& hidden,
                     std::vector<HiddenLayer>& layers, unsigned seed) {
        std::vector<int> dims;
        dims.push_back(inputDim);
        for (int h : hidden) {
            if (h > 0) {
                dims.push_back(h);
            }
        }
        dims.push_back(outputDim);
        layers.clear();
        unsigned localSeed = seed;
        for (size_t i = 1; i < dims.size(); ++i) {
            HiddenLayer layer;
            const bool relu = (i + 1 < dims.size());
            layer.initialize(dims[i - 1], dims[i], relu, localSeed);
            layers.push_back(std::move(layer));
            localSeed += 1337u;
        }
    }

    void propagateAndStore(const std::vector<float>& x) {
        encoderInputs_.resize(encoderLayers_.size() + 1u);
        encoderInputs_[0] = x;
        std::vector<float> current = x;
        for (size_t i = 0; i < encoderLayers_.size(); ++i) {
            encoderLayers_[i].forward(current);
            current.assign(encoderLayers_[i].activation.begin(), encoderLayers_[i].activation.end());
            encoderInputs_[i + 1] = current;
        }
        latent_ = current;

        decoderInputs_.resize(decoderLayers_.size() + 1u);
        decoderInputs_[0] = latent_;
        current = latent_;
        for (size_t i = 0; i < decoderLayers_.size(); ++i) {
            decoderLayers_[i].forward(current);
            current.assign(decoderLayers_[i].activation.begin(), decoderLayers_[i].activation.end());
            decoderInputs_[i + 1] = current;
        }
        recon_ = current;
    }

    static void runNetwork(const std::vector<float>& input,
                           const std::vector<HiddenLayer>& layers,
                           std::vector<float>& output) {
        if (layers.empty()) {
            output = input;
            return;
        }
        std::vector<float> current = input;
        for (const auto& layer : layers) {
            layer.forward(current);
            current.assign(layer.activation.begin(), layer.activation.end());
        }
        output = current;
    }

    static void snapshotLayers(const std::vector<HiddenLayer>& layers,
                               std::vector<LayerDesc>& out) {
        out.clear();
        out.reserve(layers.size());
        for (const auto& layer : layers) {
            LayerDesc desc;
            desc.inDim = layer.inDim;
            desc.outDim = layer.outDim;
            desc.useReLU = layer.useReLU;
            desc.weights = layer.weights;
            desc.bias = layer.bias;
            out.push_back(std::move(desc));
        }
    }

    static bool appendLayerFromDesc(const LayerDesc& desc, std::vector<HiddenLayer>& target) {
        if (desc.inDim <= 0 || desc.outDim <= 0) {
            return false;
        }
        HiddenLayer layer;
        layer.initialize(desc.inDim, desc.outDim, desc.useReLU, 1234u);
        layer.setParams(desc.weights, desc.bias);
        target.push_back(std::move(layer));
        return true;
    }

private:
    std::vector<HiddenLayer> encoderLayers_;
    std::vector<HiddenLayer> decoderLayers_;
    std::vector<std::vector<float>> encoderInputs_;
    std::vector<std::vector<float>> decoderInputs_;
    std::vector<float> latent_;
    std::vector<float> recon_;
};

} // namespace ml

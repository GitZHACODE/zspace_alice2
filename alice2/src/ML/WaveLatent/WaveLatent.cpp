#include "WaveLatent.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <numeric>
#include <random>

#include <computeGeom/scalarField.h>
#include <objects/GraphObject.h>

using nlohmann::json;

namespace {
constexpr float kCoeffEps = 1e-12f;

int modeIndex(int Kx, int kx, int ky) {
    return ky * Kx + kx;
}
} // namespace

// ====================== GridField ======================
void GridField::configure(int resX, int resY, float xMin, float xMax, float yMin, float yMax) {
    resX_ = resX;
    resY_ = resY;
    xMin_ = xMin;
    xMax_ = xMax;
    yMin_ = yMin;
    yMax_ = yMax;
    samples_.assign(static_cast<size_t>(std::max(0, resX_)) * static_cast<size_t>(std::max(0, resY_)), 0.f);
    segments_.clear();
}

void GridField::updateValues(const std::vector<float>& samples) {
    const size_t expected = static_cast<size_t>(std::max(0, resX_)) * static_cast<size_t>(std::max(0, resY_));
    if (samples.size() != expected || expected == 0) {
        return;
    }
    samples_ = samples;
    rebuildSegments();
}

void GridField::draw(alice2::Renderer& renderer, float left, float top, float cellW, float cellH,
                     const alice2::Color& color, float thickness) const {
    if (segments_.empty()) {
        return;
    }
    const float sxMax = float(std::max(1, resX_ - 1));
    const float syMax = float(std::max(1, resY_ - 1));
    const float invX = 1.0f / std::max(1e-6f, xMax_ - xMin_);
    const float invY = 1.0f / std::max(1e-6f, yMax_ - yMin_);

    for (const auto& seg : segments_) {
        auto mapPoint = [&](const alice2::Vec3& p) -> alice2::Vec2 {
            const float gx = (p.x - xMin_) * invX * sxMax;
            const float gy = (p.y - yMin_) * invY * syMax;
            const float px = left + gx * cellW;
            const float py = top + (syMax - gy) * cellH;
            return alice2::Vec2(px, py);
        };
        const alice2::Vec2 a = mapPoint(seg.a);
        const alice2::Vec2 b = mapPoint(seg.b);
        renderer.draw2dLine(a, b, color, thickness);
    }
}

void GridField::rebuildSegments() {
    segments_.clear();
    if (resX_ <= 1 || resY_ <= 1 || samples_.empty()) {
        return;
    }

    try {
        ScalarField2D field(alice2::Vec3(xMin_, yMin_, 0.f), alice2::Vec3(xMax_, yMax_, 0.f), resX_, resY_);
        field.set_values(samples_);
        const GraphObject contours = field.get_contours(0.0f);
        const auto data = contours.getGraphData();
        if (!data) {
            return;
        }
        segments_.reserve(data->edges.size());
        for (const auto& edge : data->edges) {
            if (edge.vertexA < 0 || edge.vertexB < 0 ||
                edge.vertexA >= static_cast<int>(data->vertices.size()) ||
                edge.vertexB >= static_cast<int>(data->vertices.size())) {
                continue;
            }
            const alice2::Vec3& a = data->vertices[edge.vertexA].position;
            const alice2::Vec3& b = data->vertices[edge.vertexB].position;
            segments_.push_back(Segment{a, b});
        }
    } catch (...) {
        segments_.clear();
    }
}

// ====================== DCT-II Orthonormal Basis ======================
void DCT2Basis2D::setup(int gridX, int gridY, int KxIn, int KyIn) {
    resX = gridX;
    resY = gridY;
    Kx = std::max(1, KxIn);
    Ky = std::max(1, KyIn);

    modes.clear();
    modes.reserve(Kx * Ky);
    for (int ky = 0; ky < Ky; ++ky) {
        for (int kx = 0; kx < Kx; ++kx) {
            modes.emplace_back(kx, ky);
        }
    }

    const size_t P = static_cast<size_t>(resX) * static_cast<size_t>(resY);
    B.assign(modes.size(), std::vector<float>(P, 0.f));

    auto alphaX = [&](int k) { return (k == 0) ? std::sqrt(1.0f / float(resX)) : std::sqrt(2.0f / float(resX)); };
    auto alphaY = [&](int k) { return (k == 0) ? std::sqrt(1.0f / float(resY)) : std::sqrt(2.0f / float(resY)); };

    std::vector<std::vector<float>> cx(Kx, std::vector<float>(resX));
    std::vector<std::vector<float>> cy(Ky, std::vector<float>(resY));
    for (int kx = 0; kx < Kx; ++kx) {
        for (int x = 0; x < resX; ++x) {
            cx[kx][x] = std::cos((alice2::PI / float(resX)) * (float(x) + 0.5f) * float(kx));
        }
    }
    for (int ky = 0; ky < Ky; ++ky) {
        for (int y = 0; y < resY; ++y) {
            cy[ky][y] = std::cos((alice2::PI / float(resY)) * (float(y) + 0.5f) * float(ky));
        }
    }

    for (size_t k = 0; k < modes.size(); ++k) {
        const int kx = modes[k].first;
        const int ky = modes[k].second;
        const float s = alphaX(kx) * alphaY(ky);
        auto& basis = B[k];
        for (int y = 0; y < resY; ++y) {
            for (int x = 0; x < resX; ++x) {
                const size_t p = static_cast<size_t>(y) * static_cast<size_t>(resX) + static_cast<size_t>(x);
                basis[p] = s * cx[kx][x] * cy[ky][y];
            }
        }
    }
}

// ====================== Linear AE ======================
void LinearAE::initialize(int inputDim, int zDim, unsigned seed) {
    inDim = inputDim;
    latentDim = zDim;
    std::mt19937 rng(seed);
    std::normal_distribution<float> N(0.f, 0.02f);

    We.resize(latentDim * inDim);
    for (auto& v : We) v = N(rng);
    Wd.resize(inDim * latentDim);
    for (auto& v : Wd) v = N(rng);
    be.assign(latentDim, 0.f);
    bd.assign(inDim, 0.f);

    z.assign(latentDim, 0.f);
    xhat.assign(inDim, 0.f);
    g_xhat.assign(inDim, 0.f);
    g_z.assign(latentDim, 0.f);
    g_We.assign(latentDim * inDim, 0.f);
    g_Wd.assign(inDim * latentDim, 0.f);
    g_be.assign(latentDim, 0.f);
    g_bd.assign(inDim, 0.f);
}

void LinearAE::encode(const std::vector<float>& x, std::vector<float>& out_z) const {
    out_z.assign(latentDim, 0.f);
    for (int i = 0; i < latentDim; ++i) {
        float s = be[i];
        const float* w = &We[i * inDim];
        for (int j = 0; j < inDim; ++j) {
            s += w[j] * x[j];
        }
        out_z[i] = s;
    }
}

void LinearAE::decode(const std::vector<float>& in_z, std::vector<float>& out_xhat) const {
    out_xhat.assign(inDim, 0.f);
    for (int i = 0; i < inDim; ++i) {
        float s = bd[i];
        const float* w = &Wd[i * latentDim];
        for (int j = 0; j < latentDim; ++j) {
            s += w[j] * in_z[j];
        }
        out_xhat[i] = s;
    }
}

float LinearAE::forward(const std::vector<float>& x) {
    encode(x, z);
    decode(z, xhat);
    float L = 0.f;
    for (int i = 0; i < inDim; ++i) {
        const float d = xhat[i] - x[i];
        L += 0.5f * d * d;
    }
    return L / float(std::max(1, inDim));
}

void LinearAE::backward(const std::vector<float>& x, float weightDecay, float zReg) {
    const float invDim = 1.f / float(std::max(1, inDim));
    for (int i = 0; i < inDim; ++i) {
        g_xhat[i] = (xhat[i] - x[i]) * invDim;
    }

    std::fill(g_Wd.begin(), g_Wd.end(), 0.f);
    std::fill(g_bd.begin(), g_bd.end(), 0.f);
    std::fill(g_z.begin(), g_z.end(), 0.f);

    for (int i = 0; i < inDim; ++i) {
        g_bd[i] += g_xhat[i];
        const float* WdRow = &Wd[i * latentDim];
        float* gWdRow = &g_Wd[i * latentDim];
        for (int j = 0; j < latentDim; ++j) {
            gWdRow[j] += g_xhat[i] * z[j] + weightDecay * WdRow[j];
            g_z[j] += g_xhat[i] * WdRow[j];
        }
    }

    for (int j = 0; j < latentDim; ++j) {
        g_be[j] += g_z[j] + zReg * z[j];
        float* gWeRow = &g_We[j * inDim];
        const float* xRow = x.data();
        for (int i = 0; i < inDim; ++i) {
            gWeRow[i] += g_z[j] * xRow[i] + weightDecay * We[j * inDim + i];
        }
    }
}

void LinearAE::sgd(float lr) {
    for (int i = 0; i < static_cast<int>(We.size()); ++i) We[i] -= lr * g_We[i];
    for (int i = 0; i < static_cast<int>(Wd.size()); ++i) Wd[i] -= lr * g_Wd[i];
    for (int i = 0; i < static_cast<int>(be.size()); ++i) be[i] -= lr * g_be[i];
    for (int i = 0; i < static_cast<int>(bd.size()); ++i) bd[i] -= lr * g_bd[i];
}

// ====================== WaveLatent ======================
WaveLatent::WaveLatent() = default;

void WaveLatent::clear() {
    H_ = 0;
    W_ = 0;
    fields_.clear();
    config_ = {};
    trainingParams_ = {};
    basisFull_ = {};
    selectedModes_.clear();
    selectedBasis_.clear();
    coeffs_.clear();
    coeffMean_.clear();
    coeffStd_.clear();
    storedLatents_.clear();
    domainXMin_ = -1.0f;
    domainXMax_ = 1.0f;
    domainYMin_ = -1.0f;
    domainYMax_ = 1.0f;
    ae_ = {};
}

bool WaveLatent::setFields(int height, int width, const std::vector<std::vector<float>>& fields) {
    H_ = height;
    W_ = width;
    fields_ = fields;
    if (H_ <= 0 || W_ <= 0 || fields_.empty()) {
        return false;
    }
    const size_t P = static_cast<size_t>(H_) * static_cast<size_t>(W_);
    for (const auto& f : fields_) {
        if (f.size() != P) {
            return false;
        }
    }
    return true;
}

bool WaveLatent::setFields(const WaveLatentDataset& dataset) {
    if (dataset.empty()) {
        return false;
    }
    domainXMin_ = dataset.xMin;
    domainXMax_ = dataset.xMax;
    domainYMin_ = dataset.yMin;
    domainYMax_ = dataset.yMax;
    return setFields(dataset.gridResY, dataset.gridResX, dataset.fields);
}

bool WaveLatent::initialize(const WaveLatentConfig& config) {
    if (H_ <= 0 || W_ <= 0 || fields_.empty()) {
        std::printf("[WaveLatent] initialize called without fields.\n");
        return false;
    }

    config_ = config;
    basisFull_.setup(W_, H_, config_.Kx, config_.Ky);

    std::vector<std::vector<float>> coeffsFull;
    projectFieldsOntoBasis(coeffsFull);
    selectTopKBasis(coeffsFull);
    normaliseCoefficients();
    initialiseAutoencoder(config_.seed);
    return true;
}

bool WaveLatent::ready() const {
    return !selectedBasis_.empty() && ae_.latentDim == config_.latentDim && ae_.inDim == static_cast<int>(coeffMean_.size());
}

void WaveLatent::train(const WaveLatentTrainingParams& params) {
    if (coeffs_.empty() || ae_.latentDim <= 0) {
        return;
    }
    trainingParams_ = params;
    for (int epoch = 0; epoch < params.epochs; ++epoch) {
        double loss = 0.0;
        for (const auto& c : coeffs_) {
            loss += ae_.forward(c);
            ae_.backward(c, params.weightDecay, params.latentReg);
            ae_.sgd(params.learningRate);
        }
        if (params.printEvery > 0 && ((epoch + 1) % params.printEvery) == 0) {
            const float avgLoss = static_cast<float>(loss / std::max<size_t>(1, coeffs_.size()));
            std::printf("[WaveLatent] epoch %d  avgLoss=%.6f\n", epoch + 1, avgLoss);
        }
    }
}

bool WaveLatent::encodeSample(int index, std::vector<float>& z) const {
    if (ae_.latentDim <= 0 || coeffs_.empty()) {
        z.assign(config_.latentDim, 0.f);
        return false;
    }
    if (index < 0 || index >= static_cast<int>(coeffs_.size())) {
        z.assign(ae_.latentDim, 0.f);
        return false;
    }
    ae_.encode(coeffs_[index], z);
    return true;
}

void WaveLatent::decodeLatentToGrid(const std::vector<float>& z, std::vector<float>& gridOut) const {
    std::vector<float> coeffDenorm;
    latentToCoeffsDenorm(z, coeffDenorm);
    coeffsToGrid(coeffDenorm, gridOut);
}

bool WaveLatent::reconstructBasisOnly(int index, std::vector<float>& gridOut) const {
    return reconstructInternal(index, false, gridOut);
}

bool WaveLatent::reconstructAutoencoder(int index, std::vector<float>& gridOut) const {
    return reconstructInternal(index, true, gridOut);
}

bool WaveLatent::blendSamples(int indexA, int indexB, float t, std::vector<float>& gridOut) const {
    if (!ready()) {
        return false;
    }
    if (indexA < 0 || indexB < 0 || indexA >= static_cast<int>(coeffs_.size()) || indexB >= static_cast<int>(coeffs_.size())) {
        return false;
    }

    std::vector<float> zA;
    std::vector<float> zB;
    if (!encodeSample(indexA, zA) || !encodeSample(indexB, zB)) {
        return false;
    }

    std::vector<float> zBlend(zA.size(), 0.f);
    for (size_t d = 0; d < zBlend.size(); ++d) {
        zBlend[d] = (1.f - t) * zA[d] + t * zB[d];
    }

    decodeLatentToGrid(zBlend, gridOut);
    return true;
}

void WaveLatent::getAllLatents(std::vector<std::vector<float>>& latentsOut) const {
    if (coeffs_.empty() || ae_.latentDim <= 0) {
        latentsOut.clear();
        return;
    }
    latentsOut.assign(coeffs_.size(), std::vector<float>(ae_.latentDim, 0.f));
    for (size_t i = 0; i < coeffs_.size(); ++i) {
        ae_.encode(coeffs_[i], latentsOut[i]);
    }
}

void WaveLatent::printDiagnostics() const {
    if (fields_.empty() || selectedBasis_.empty()) {
        std::printf("[WaveLatent] Diagnostics unavailable (missing fields or basis).\n");
        return;
    }
    const size_t P = static_cast<size_t>(H_) * static_cast<size_t>(W_);
    auto mse = [&](const std::vector<float>& a, const std::vector<float>& b) {
        double e = 0.0;
        for (size_t p = 0; p < P; ++p) {
            const double d = double(a[p]) - double(b[p]);
            e += d * d;
        }
        return float(e / std::max<size_t>(1, P));
    };

    std::printf("[WaveLatent] Diagnostics per sample:\n");
    for (size_t i = 0; i < fields_.size(); ++i) {
        std::vector<float> basisOnly;
        std::vector<float> autoencoder;
        reconstructBasisOnly(static_cast<int>(i), basisOnly);
        reconstructAutoencoder(static_cast<int>(i), autoencoder);
        const float mseBasis = mse(basisOnly, fields_[i]);
        const float mseAE = mse(autoencoder, fields_[i]);

        std::vector<float> z, chat;
        encodeSample(static_cast<int>(i), z);
        ae_.decode(z, chat);
        double coeffErr = 0.0;
        for (size_t k = 0; k < chat.size(); ++k) {
            const double d = double(chat[k]) - double(coeffs_[i][k]);
            coeffErr += d * d;
        }
        coeffErr /= std::max<size_t>(1, chat.size());

        std::printf("  #%zu  MSE[basis]=%.6g  MSE[AE]=%.6g  MSE[coeff-norm]=%.6g\n",
                    i, mseBasis, mseAE, float(coeffErr));
    }
}

void WaveLatent::printCrossID() const {
    if (fields_.empty() || selectedBasis_.empty()) {
        return;
    }
    const size_t P = static_cast<size_t>(H_) * static_cast<size_t>(W_);
    auto mse = [&](const std::vector<float>& a, const std::vector<float>& b) {
        double e = 0.0;
        for (size_t p = 0; p < P; ++p) {
            const double d = double(a[p]) - double(b[p]);
            e += d * d;
        }
        return float(e / std::max<size_t>(1, P));
    };

    std::vector<std::vector<float>> latents;
    getAllLatents(latents);
    if (latents.empty()) {
        return;
    }

    std::printf("[WaveLatent] Cross-ID (argmin MSE over GT):\n");
    for (size_t i = 0; i < latents.size(); ++i) {
        std::vector<float> recon;
        decodeLatentToGrid(latents[i], recon);
        int bestIdx = 0;
        float bestErr = std::numeric_limits<float>::max();
        for (size_t j = 0; j < fields_.size(); ++j) {
            const float err = mse(recon, fields_[j]);
            if (err < bestErr) {
                bestErr = err;
                bestIdx = static_cast<int>(j);
            }
        }
        std::printf("  z_%zu -> GT #%d   MSE=%.6g\n", i, bestIdx, bestErr);
    }
}

bool WaveLatent::saveModel(const std::string& filePath) const {
    if (!ready()) {
        std::printf("[WaveLatent] saveModel aborted: model not initialised.\n");
        return false;
    }

    json j;
    j["version"] = 1;
    j["grid"] = {{"H", H_}, {"W", W_}};
    j["config"] = {
        {"Kx", config_.Kx},
        {"Ky", config_.Ky},
        {"keepK", basisRank()},
        {"latentDim", config_.latentDim},
        {"seed", config_.seed}
    };
    j["domain"] = {
        {"xMin", domainXMin_},
        {"xMax", domainXMax_},
        {"yMin", domainYMin_},
        {"yMax", domainYMax_}
    };

    json modesJson = json::array();
    for (const auto& m : selectedModes_) {
        modesJson.push_back({m.first, m.second});
    }
    j["modes"] = modesJson;

    j["coeff_stats"] = {
        {"mean", coeffMean_},
        {"std", coeffStd_}
    };

    j["training"] = {
        {"learningRate", trainingParams_.learningRate},
        {"weightDecay", trainingParams_.weightDecay},
        {"latentReg", trainingParams_.latentReg},
        {"printEvery", trainingParams_.printEvery}
    };

    json aeJson;
    aeJson["inDim"] = ae_.inDim;
    aeJson["latentDim"] = ae_.latentDim;
    aeJson["We"] = ae_.We;
    aeJson["Wd"] = ae_.Wd;
    aeJson["be"] = ae_.be;
    aeJson["bd"] = ae_.bd;
    j["ae"] = aeJson;

    std::vector<std::vector<float>> latents;
    getAllLatents(latents);
    if (!latents.empty()) {
        j["latents"] = latents;
    }

    std::ofstream out(filePath);
    if (!out.is_open()) {
        std::printf("[WaveLatent] Failed to write model file '%s'\n", filePath.c_str());
        return false;
    }
    out << j.dump(2);
    return true;
}

bool WaveLatent::loadModel(const std::string& filePath) {
    std::ifstream in(filePath);
    if (!in.is_open()) {
        std::printf("[WaveLatent] Failed to open model file '%s'\n", filePath.c_str());
        return false;
    }

    json j;
    in >> j;
    if (!j.is_object()) {
        std::printf("[WaveLatent] Model file '%s' is invalid.\n", filePath.c_str());
        return false;
    }

    if (auto gridIt = j.find("grid"); gridIt != j.end()) {
        H_ = gridIt->value("H", H_);
        W_ = gridIt->value("W", W_);
    }

    if (auto cfgIt = j.find("config"); cfgIt != j.end()) {
        config_.Kx = cfgIt->value("Kx", config_.Kx);
        config_.Ky = cfgIt->value("Ky", config_.Ky);
        config_.keepK = cfgIt->value("keepK", config_.keepK);
        config_.latentDim = cfgIt->value("latentDim", config_.latentDim);
        config_.seed = cfgIt->value("seed", config_.seed);
    }

    if (auto domainIt = j.find("domain"); domainIt != j.end()) {
        domainXMin_ = domainIt->value("xMin", domainXMin_);
        domainXMax_ = domainIt->value("xMax", domainXMax_);
        domainYMin_ = domainIt->value("yMin", domainYMin_);
        domainYMax_ = domainIt->value("yMax", domainYMax_);
    } else {
        domainXMin_ = -1.0f;
        domainXMax_ = 1.0f;
        domainYMin_ = -1.0f;
        domainYMax_ = 1.0f;
    }

    selectedModes_.clear();
    if (auto modesIt = j.find("modes"); modesIt != j.end() && modesIt->is_array()) {
        for (const auto& m : *modesIt) {
            if (m.is_array() && m.size() == 2) {
                selectedModes_.emplace_back(m[0].get<int>(), m[1].get<int>());
            }
        }
    }

    coeffMean_.clear();
    coeffStd_.clear();
    if (auto statsIt = j.find("coeff_stats"); statsIt != j.end()) {
        coeffMean_ = statsIt->value("mean", coeffMean_);
        coeffStd_ = statsIt->value("std", coeffStd_);
    }

    if (auto trainIt = j.find("training"); trainIt != j.end()) {
        trainingParams_.learningRate = trainIt->value("learningRate", trainingParams_.learningRate);
        trainingParams_.weightDecay = trainIt->value("weightDecay", trainingParams_.weightDecay);
        trainingParams_.latentReg = trainIt->value("latentReg", trainingParams_.latentReg);
        trainingParams_.printEvery = trainIt->value("printEvery", trainingParams_.printEvery);
    }

    if (H_ <= 0 || W_ <= 0) {
        std::printf("[WaveLatent] loadModel failed: grid size missing.\n");
        return false;
    }

    basisFull_.setup(W_, H_, config_.Kx, config_.Ky);
    rebuildSelectedBasis();

    const int inDim = static_cast<int>(coeffMean_.size());
    if (inDim > 0) {
        ae_.initialize(inDim, config_.latentDim, config_.seed);
    } else {
        ae_ = {};
    }

    if (auto aeIt = j.find("ae"); aeIt != j.end()) {
        const auto latDim = aeIt->value("latentDim", ae_.latentDim);
        const auto inDimFile = aeIt->value("inDim", ae_.inDim);
        if (inDim == inDimFile && latDim == ae_.latentDim) {
            ae_.We = aeIt->value("We", ae_.We);
            ae_.Wd = aeIt->value("Wd", ae_.Wd);
            ae_.be = aeIt->value("be", ae_.be);
            ae_.bd = aeIt->value("bd", ae_.bd);
        } else {
            std::printf("[WaveLatent] loadModel warning: AE dimensions mismatch; keeping freshly initialised weights.\n");
        }
    }

    storedLatents_.clear();
    if (auto latIt = j.find("latents"); latIt != j.end() && latIt->is_array()) {
        storedLatents_.reserve(latIt->size());
        for (const auto& entry : *latIt) {
            if (!entry.is_array()) {
                continue;
            }
            std::vector<float> latentRow;
            latentRow.reserve(entry.size());
            for (const auto& v : entry) {
                latentRow.push_back(v.get<float>());
            }
            storedLatents_.push_back(std::move(latentRow));
        }
    }

    return ready();
}

// ====================== Private Helpers ======================
void WaveLatent::projectFieldsOntoBasis(std::vector<std::vector<float>>& coeffsFull) const {
    const size_t sampleCount = fields_.size();
    const int Kfull = basisFull_.Kfull();
    coeffsFull.assign(sampleCount, std::vector<float>(Kfull, 0.f));
    const size_t P = static_cast<size_t>(H_) * static_cast<size_t>(W_);

    for (size_t s = 0; s < sampleCount; ++s) {
        const auto& field = fields_[s];
        for (int k = 0; k < Kfull; ++k) {
            const auto& basis = basisFull_.B[k];
            double acc = 0.0;
            for (size_t p = 0; p < P; ++p) {
                acc += double(field[p]) * double(basis[p]);
            }
            coeffsFull[s][k] = static_cast<float>(acc);
        }
    }
}

void WaveLatent::selectTopKBasis(const std::vector<std::vector<float>>& coeffsFull) {
    const int Kfull = basisFull_.Kfull();
    if (Kfull == 0) {
        selectedModes_.clear();
        selectedBasis_.clear();
        coeffs_.clear();
        return;
    }

    const int Ksel = std::max(1, std::min(config_.keepK, Kfull));
    std::vector<double> power(Kfull, 0.0);
    for (int k = 0; k < Kfull; ++k) {
        double acc = 0.0;
        for (const auto& sample : coeffsFull) {
            const double v = double(sample[k]);
            acc += v * v;
        }
        power[k] = acc / std::max<size_t>(1, coeffsFull.size());
    }

    std::vector<int> indices(Kfull);
    std::iota(indices.begin(), indices.end(), 0);
    std::partial_sort(indices.begin(), indices.begin() + Ksel, indices.end(),
                      [&](int a, int b) { return power[a] > power[b]; });
    indices.resize(Ksel);

    selectedModes_.clear();
    selectedModes_.reserve(Ksel);
    for (int idx : indices) {
        selectedModes_.push_back(basisFull_.modes[idx]);
    }

    coeffs_.assign(coeffsFull.size(), std::vector<float>(Ksel, 0.f));
    for (size_t s = 0; s < coeffsFull.size(); ++s) {
        for (int r = 0; r < Ksel; ++r) {
            coeffs_[s][r] = coeffsFull[s][indices[r]];
        }
    }

    rebuildSelectedBasis();
}

void WaveLatent::normaliseCoefficients() {
    const size_t K = selectedModes_.size();
    if (K == 0) {
        coeffMean_.clear();
        coeffStd_.clear();
        return;
    }

    coeffMean_.assign(K, 0.f);
    coeffStd_.assign(K, 1.f);
    if (coeffs_.empty()) {
        return;
    }

    const float invSamples = 1.f / float(std::max<size_t>(1, coeffs_.size()));
    for (size_t k = 0; k < K; ++k) {
        double mean = 0.0;
        for (const auto& sample : coeffs_) {
            mean += sample[k];
        }
        mean *= invSamples;

        double variance = 0.0;
        for (const auto& sample : coeffs_) {
            const double diff = sample[k] - mean;
            variance += diff * diff;
        }
        variance *= invSamples;

        coeffMean_[k] = static_cast<float>(mean);
        coeffStd_[k] = static_cast<float>(std::sqrt(variance + kCoeffEps));

        for (auto& sample : coeffs_) {
            sample[k] = (sample[k] - coeffMean_[k]) / coeffStd_[k];
        }
    }
}

void WaveLatent::rebuildSelectedBasis() {
    selectedBasis_.clear();
    if (selectedModes_.empty()) {
        return;
    }
    selectedBasis_.reserve(selectedModes_.size());
    for (const auto& mode : selectedModes_) {
        const int idx = modeIndex(basisFull_.Kx, mode.first, mode.second);
        if (idx >= 0 && idx < static_cast<int>(basisFull_.B.size())) {
            selectedBasis_.push_back(basisFull_.B[idx]);
        }
    }
}

void WaveLatent::initialiseAutoencoder(unsigned seed) {
    const int inDim = static_cast<int>(coeffMean_.size());
    if (inDim <= 0) {
        ae_ = {};
        return;
    }
    ae_.initialize(inDim, config_.latentDim, seed);
}

bool WaveLatent::reconstructInternal(int index, bool useAE, std::vector<float>& gridOut) const {
    if (!ready()) {
        return false;
    }
    if (index < 0 || index >= static_cast<int>(coeffs_.size())) {
        return false;
    }

    std::vector<float> coeffDenorm(coeffs_[index].size(), 0.f);
    if (useAE) {
        std::vector<float> z;
        if (!encodeSample(index, z)) {
            return false;
        }
        latentToCoeffsDenorm(z, coeffDenorm);
    } else {
        for (size_t k = 0; k < coeffDenorm.size(); ++k) {
            coeffDenorm[k] = coeffs_[index][k] * coeffStd_[k] + coeffMean_[k];
        }
    }
    coeffsToGrid(coeffDenorm, gridOut);
    return true;
}

void WaveLatent::latentToCoeffsDenorm(const std::vector<float>& z, std::vector<float>& coeffsDenorm) const {
    std::vector<float> coeffNorm;
    ae_.decode(z, coeffNorm);
    coeffsDenorm.resize(coeffNorm.size());
    for (size_t k = 0; k < coeffNorm.size(); ++k) {
        const float mean = (k < coeffMean_.size()) ? coeffMean_[k] : 0.f;
        const float stdv = (k < coeffStd_.size()) ? coeffStd_[k] : 1.f;
        coeffsDenorm[k] = coeffNorm[k] * stdv + mean;
    }
}

void WaveLatent::coeffsToGrid(const std::vector<float>& coeffsDenorm, std::vector<float>& gridOut) const {
    const size_t P = static_cast<size_t>(H_) * static_cast<size_t>(W_);
    gridOut.assign(P, 0.f);
    for (size_t k = 0; k < selectedBasis_.size(); ++k) {
        const auto& basis = selectedBasis_[k];
        const float ck = (k < coeffsDenorm.size()) ? coeffsDenorm[k] : 0.f;
        for (size_t p = 0; p < P; ++p) {
            gridOut[p] += ck * basis[p];
        }
    }
}

bool WaveLatent::loadDatasetFromJson(const std::string& filePath,
                                     WaveLatentDataset& dataset,
                                     const WaveLatentDatasetOptions& options) {
    dataset = {};

    std::ifstream file(filePath);
    if (!file.is_open()) {
        std::printf("[WaveLatent] Failed to open dataset '%s'\n", filePath.c_str());
        return false;
    }

    json j;
    try {
        file >> j;
    } catch (const std::exception& e) {
        std::printf("[WaveLatent] Failed to parse dataset JSON: %s\n", e.what());
        return false;
    }

    if (!j.contains("shapes") || !j["shapes"].is_array()) {
        std::printf("[WaveLatent] Dataset missing 'shapes' array.\n");
        return false;
    }

    alice2::Vec3 minBB(options.xMin, options.yMin, 0.0f);
    alice2::Vec3 maxBB(options.xMax, options.yMax, 0.0f);
    if (const auto bboxIt = j.find("bbox"); bboxIt != j.end() && bboxIt->is_object()) {
        const auto& bbox = *bboxIt;
        if (bbox.contains("minbb") && bbox["minbb"].is_array() && bbox["minbb"].size() >= 2) {
            minBB.x = bbox["minbb"][0].get<float>();
            minBB.y = bbox["minbb"][1].get<float>();
        }
        if (bbox.contains("maxbb") && bbox["maxbb"].is_array() && bbox["maxbb"].size() >= 2) {
            maxBB.x = bbox["maxbb"][0].get<float>();
            maxBB.y = bbox["maxbb"][1].get<float>();
        }
    }

    const int gridResX = std::max(1, options.gridResX);
    const int gridResY = std::max(1, options.gridResY);
    const size_t expectedSize = static_cast<size_t>(gridResX) * static_cast<size_t>(gridResY);

    dataset.fields.clear();
    dataset.fields.reserve(j["shapes"].size());

    auto applyBranchPolys = [](const json& branchNode, ScalarField2D& target) -> bool {
        const json* polyArray = nullptr;
        if (branchNode.is_object()) {
            if (const auto it = branchNode.find("polys"); it != branchNode.end() && it->is_array()) {
                polyArray = &(*it);
            }
        } else if (branchNode.is_array()) {
            polyArray = &branchNode;
        }
        if (!polyArray) {
            return false;
        }

        bool applied = false;
        for (const auto& poly : *polyArray) {
            if (!poly.is_array() || poly.empty()) {
                continue;
            }

            std::vector<alice2::Vec3> pts;
            pts.reserve(poly.size());
            for (const auto& p : poly) {
                if (!p.is_array() || p.size() < 2) {
                    continue;
                }
                const float px = p[0].get<float>();
                const float py = p[1].get<float>();
                const float pz = (p.size() > 2) ? p[2].get<float>() : 0.0f;
                pts.emplace_back(px, py, pz);
            }
            if (!pts.empty()) {
                target.apply_scalar_polygon(pts);
                applied = true;
            }
        }
        return applied;
    };

    const json* cutoutRoot = nullptr;
    if (const auto cutoutIt = j.find("cutout"); cutoutIt != j.end()) {
        cutoutRoot = &(*cutoutIt);
    }

    const auto& shapesArray = j["shapes"];
    for (size_t branchIdx = 0; branchIdx < shapesArray.size(); ++branchIdx) {
        const auto& branch = shapesArray[branchIdx];
        ScalarField2D field(minBB, maxBB, gridResX, gridResY);

        applyBranchPolys(branch, field);

        bool hasCutout = false;
        ScalarField2D cutoutField(minBB, maxBB, gridResX, gridResY);
        if (cutoutRoot) {
            if (cutoutRoot->is_array()) {
                if (branchIdx < cutoutRoot->size()) {
                    hasCutout = applyBranchPolys((*cutoutRoot)[branchIdx], cutoutField) || hasCutout;
                }
            } else if (cutoutRoot->is_object()) {
                hasCutout = applyBranchPolys(*cutoutRoot, cutoutField) || hasCutout;
            }
        }

        if (branch.is_object()) {
            if (const auto cutoutIt = branch.find("cutout"); cutoutIt != branch.end()) {
                if (cutoutIt->is_array()) {
                    for (const auto& cutoutBranch : *cutoutIt) {
                        hasCutout = applyBranchPolys(cutoutBranch, cutoutField) || hasCutout;
                    }
                } else if (cutoutIt->is_object()) {
                    hasCutout = applyBranchPolys(*cutoutIt, cutoutField) || hasCutout;
                }
            }
        }

        if (hasCutout) {
            field.boolean_subtract(cutoutField);
        }

        field.normalise();
        const auto& values = field.get_values();
        if (values.size() != expectedSize) {
            continue;
        }
        dataset.fields.emplace_back(values.begin(), values.end());
    }

    if (dataset.fields.empty()) {
        std::printf("[WaveLatent] Dataset contained no valid fields.\n");
        return false;
    }

    dataset.gridResX = gridResX;
    dataset.gridResY = gridResY;
    dataset.xMin = minBB.x;
    dataset.xMax = maxBB.x;
    dataset.yMin = minBB.y;
    dataset.yMax = maxBB.y;

    std::printf("[WaveLatent] Loaded %zu fields from '%s'\n",
                dataset.fields.size(), filePath.c_str());
    return true;
}

// #define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>

#include <algorithm>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#include "ML/WaveLatent/WaveLatent.h"

using namespace alice2;

namespace {
// Editable globals for quick experiments.
int gDisplayFieldCount = 10;
int gShapeIdA = 0;
int gShapeIdB = 5;

constexpr float kMargin = 20.0f;
constexpr float kTileSize = 170.0f;
constexpr float kTileGap = 18.0f;
constexpr float kRowGap = 40.0f;
constexpr float kFrameThickness = 1.25f;
const Color kIsoColor(0.12f, 0.28f, 0.78f, 1.0f);
const Color kFrameColor(0.12f, 0.12f, 0.12f, 1.0f);
const Color kLabelColor(0.22f, 0.22f, 0.25f, 1.0f);

constexpr float kCoeffStdEps = 1e-6f;

std::string formatLayerChain(int first, const std::vector<int>& hidden, int last) {
    std::ostringstream oss;
    oss << first;
    for (int h : hidden) {
        oss << "->" << h;
    }
    oss << "->" << last;
    return oss.str();
}

std::string describeAutoencoder(const WaveLatent& latent) {
    if (!latent.ready()) {
        return "AE: not ready";
    }
    const auto& cfg = latent.config();
    const int coeffDim = std::max(1, latent.basisRank());
    std::ostringstream oss;
    oss << "AE enc " << formatLayerChain(coeffDim, cfg.encoderHidden, cfg.latentDim)
        << "  dec " << formatLayerChain(cfg.latentDim, cfg.decoderHidden, coeffDim);
    return oss.str();
}
} // namespace

class Sketch_WaveLatent_Triptych : public ISketch {
public:
    std::string getName() const override        { return "WaveLatent Interp Triptych"; }
    std::string getDescription() const override { return "Compare latent / coeff / field interpolations."; }
    std::string getAuthor() const override      { return "alice2 User"; }

    void setup() override {
        scene().setBackgroundColor(Color(0.92f, 0.92f, 0.94f, 1.0f));
        scene().setShowAxes(false);
        scene().setShowGrid(false);

        displayCount_ = std::max(2, gDisplayFieldCount);

        if (!WaveLatent::loadDatasetFromJson(datasetPath_, dataset_, datasetOptions_)) {
            status_ = "Failed to load dataset";
            return;
        }
        configureRow(latentInterpFields_);
        configureRow(coeffInterpFields_);
        configureRow(fieldLerpFields_);

        if (!waveLatent_.loadModel(modelPath_)) {
            status_ = "Failed to load model";
            return;
        }
        if (!waveLatent_.ready()) {
            status_ = "Model missing basis/AE";
            return;
        }

        shapeA_ = clampShapeIndex(gShapeIdA);
        shapeB_ = clampShapeIndex(gShapeIdB);
        if (shapeA_ == shapeB_) {
            status_ = "Need two different shape ids";
            return;
        }

        if (!buildRows()) {
            return;
        }

        ready_ = true;
        std::snprintf(statusBuffer_, sizeof(statusBuffer_), "Shapes %d -> %d   steps=%d",
                      shapeA_, shapeB_, displayCount_);
        status_ = statusBuffer_;
        aeSummary_ = describeAutoencoder(waveLatent_);
    }

    void update(float) override {}

    void draw(Renderer& renderer, Camera&) override {
        renderer.setColor(Color(0.1f, 0.1f, 0.1f));
        renderer.drawString("WaveLatent interpolation triptych", kMargin, 24.0f);
        renderer.drawString(status_, kMargin, 44.0f);
        renderer.drawString(aeSummary_, kMargin, 64.0f);

        if (!ready_) {
            renderer.drawString("Sketch not ready.", kMargin, 84.0f);
            return;
        }

        const float startY = kMargin + 96.0f;
        float rowTop = startY;
        drawRow(renderer, latentInterpFields_,
                rowTop, "Latent lerp (decode)");

        rowTop += kTileSize + kRowGap;
        drawRow(renderer, coeffInterpFields_,
                rowTop, "Coeff lerp (denorm + basis)");

        rowTop += kTileSize + kRowGap;
        drawRow(renderer, fieldLerpFields_,
                rowTop, "Field lerp (value space)");
    }

private:
    void configureRow(std::vector<GridField>& row) {
        row.resize(displayCount_);
        for (auto& field : row) {
            field.configure(dataset_.gridResX, dataset_.gridResY,
                            dataset_.xMin, dataset_.xMax,
                            dataset_.yMin, dataset_.yMax);
        }
    }

    int clampShapeIndex(int requested) const {
        const int count = static_cast<int>(dataset_.sampleCount());
        if (count == 0) {
            return 0;
        }
        return std::clamp(requested, 0, count - 1);
    }

    bool buildRows() {
        if (dataset_.sampleCount() == 0 || waveLatent_.basis().empty()) {
            status_ = "Missing dataset or basis";
            return false;
        }

        std::vector<float> latentA;
        std::vector<float> latentB;
        if (!fetchStoredLatent(shapeA_, latentA) || !fetchStoredLatent(shapeB_, latentB)) {
            status_ = "Stored latents unavailable";
            return false;
        }

        std::vector<float> coeffNormA;
        std::vector<float> coeffNormB;
        if (!computeNormalizedCoeffs(shapeA_, coeffNormA) ||
            !computeNormalizedCoeffs(shapeB_, coeffNormB)) {
            status_ = "Coeff projection failed";
            return false;
        }

        const auto& fieldA = dataset_.fields[shapeA_];
        const auto& fieldB = dataset_.fields[shapeB_];
        if (fieldA.size() != fieldB.size() || fieldA.empty()) {
            status_ = "Invalid dataset fields";
            return false;
        }

        std::vector<float> latentBlend;
        std::vector<float> coeffBlend;
        std::vector<float> coeffDenorm;
        std::vector<float> gridBuffer(fieldA.size(), 0.0f);

        for (int i = 0; i < displayCount_; ++i) {
            const float t = (displayCount_ <= 1) ? 0.0f : float(i) / float(displayCount_ - 1);

            // Latent interpolation
            lerpVector(latentA, latentB, t, latentBlend);
            waveLatent_.decodeLatentToGrid(latentBlend, gridBuffer);
            latentInterpFields_[static_cast<size_t>(i)].updateValues(gridBuffer);

            // Coefficient interpolation (normalised -> denormalised -> basis projection).
            lerpVector(coeffNormA, coeffNormB, t, coeffBlend);
            coeffNormToGrid(coeffBlend, coeffDenorm, gridBuffer);
            coeffInterpFields_[static_cast<size_t>(i)].updateValues(gridBuffer);

            // Naive field interpolation.
            for (size_t p = 0; p < gridBuffer.size(); ++p) {
                gridBuffer[p] = (1.0f - t) * fieldA[p] + t * fieldB[p];
            }
            fieldLerpFields_[static_cast<size_t>(i)].updateValues(gridBuffer);
        }
        return true;
    }

    bool fetchStoredLatent(int index, std::vector<float>& latent) const {
        const auto& latents = waveLatent_.storedLatents();
        if (index < 0 || index >= static_cast<int>(latents.size())) {
            return false;
        }
        latent = latents[index];
        return !latent.empty();
    }

    bool computeNormalizedCoeffs(int shapeIndex, std::vector<float>& coeffNorm) const {
        if (shapeIndex < 0 || shapeIndex >= static_cast<int>(dataset_.sampleCount())) {
            return false;
        }
        const auto& field = dataset_.fields[shapeIndex];
        const auto& basis = waveLatent_.basis();
        if (basis.empty()) {
            return false;
        }

        coeffNorm.assign(basis.size(), 0.0f);
        for (size_t k = 0; k < basis.size(); ++k) {
            const auto& mode = basis[k];
            const size_t P = std::min(field.size(), mode.size());
            double dot = 0.0;
            for (size_t p = 0; p < P; ++p) {
                dot += double(field[p]) * double(mode[p]);
            }
            const float mean = (k < waveLatent_.coeffMean().size()) ? waveLatent_.coeffMean()[k] : 0.0f;
            const float stdv = (k < waveLatent_.coeffStd().size()) ? waveLatent_.coeffStd()[k] : 1.0f;
            coeffNorm[k] = (static_cast<float>(dot) - mean) / std::max(stdv, kCoeffStdEps);
        }
        return true;
    }

    void coeffNormToGrid(const std::vector<float>& coeffNorm,
                         std::vector<float>& coeffDenorm,
                         std::vector<float>& gridOut) const {
        const auto& mean = waveLatent_.coeffMean();
        const auto& stdv = waveLatent_.coeffStd();
        coeffDenorm.resize(coeffNorm.size());
        for (size_t k = 0; k < coeffNorm.size(); ++k) {
            const float mu = (k < mean.size()) ? mean[k] : 0.0f;
            const float sigma = (k < stdv.size()) ? stdv[k] : 1.0f;
            coeffDenorm[k] = coeffNorm[k] * std::max(sigma, kCoeffStdEps) + mu;
        }

        const auto& basis = waveLatent_.basis();
        gridOut.assign(gridOut.size(), 0.0f);
        for (size_t k = 0; k < basis.size(); ++k) {
            const auto& mode = basis[k];
            const float c = (k < coeffDenorm.size()) ? coeffDenorm[k] : 0.0f;
            const size_t P = std::min(mode.size(), gridOut.size());
            for (size_t p = 0; p < P; ++p) {
                gridOut[p] += c * mode[p];
            }
        }
    }

    void lerpVector(const std::vector<float>& a, const std::vector<float>& b, float t,
                    std::vector<float>& out) const {
        if (a.empty() || b.empty()) {
            out.clear();
            return;
        }
        const size_t n = std::min(a.size(), b.size());
        out.resize(n);
        for (size_t i = 0; i < n; ++i) {
            out[i] = (1.0f - t) * a[i] + t * b[i];
        }
    }

    void drawRow(Renderer& renderer, const std::vector<GridField>& fields,
                 float top, const std::string& label) const {
        renderer.setColor(kLabelColor);
        renderer.drawString(label, kMargin, top - 10.0f);

        if (dataset_.gridResX <= 0 || dataset_.gridResY <= 0) {
            return;
        }

        const float cellW = kTileSize / static_cast<float>(dataset_.gridResX);
        const float cellH = kTileSize / static_cast<float>(dataset_.gridResY);
        for (size_t i = 0; i < fields.size(); ++i) {
            const float left = kMargin + float(i) * (kTileSize + kTileGap);
            drawFrame(renderer, left, top);
            fields[i].draw(renderer, left, top, cellW, cellH, kIsoColor, 1.3f);
        }
    }

    void drawFrame(Renderer& renderer, float left, float top) const {
        const float right = left + kTileSize;
        const float bottom = top + kTileSize;
        renderer.draw2dLine(Vec2(left, top), Vec2(right, top), kFrameColor, kFrameThickness);
        renderer.draw2dLine(Vec2(right, top), Vec2(right, bottom), kFrameColor, kFrameThickness);
        renderer.draw2dLine(Vec2(right, bottom), Vec2(left, bottom), kFrameColor, kFrameThickness);
        renderer.draw2dLine(Vec2(left, bottom), Vec2(left, top), kFrameColor, kFrameThickness);
    }

private:
    WaveLatent waveLatent_;
    WaveLatentDataset dataset_;
    WaveLatentDatasetOptions datasetOptions_{128, 128, -1.2f, 1.2f, -1.2f, 1.2f};

    std::vector<GridField> latentInterpFields_;
    std::vector<GridField> coeffInterpFields_;
    std::vector<GridField> fieldLerpFields_;

    std::string datasetPath_ = "inShapes.json";
    std::string modelPath_ = "WaveLatentModel.json";

    int displayCount_ = 0;
    int shapeA_ = 0;
    int shapeB_ = 1;

    bool ready_ = false;
    std::string status_ = "Init";
    char statusBuffer_[128]{};
    std::string aeSummary_ = "AE: offline";
};

ALICE2_REGISTER_SKETCH_AUTO(Sketch_WaveLatent_Triptych)

#endif // __MAIN__

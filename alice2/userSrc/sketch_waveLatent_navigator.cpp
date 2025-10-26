//#define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "ML/WaveLatent/WaveLatent.h"

using namespace alice2;

namespace {
const Color kIsoColor(0.10f, 0.35f, 0.85f, 1.0f);
const Color kFrameColor(0.18f, 0.18f, 0.2f, 1.0f);
const int   kDefaultTileCount = 25;
const int   kMinTileCount = 4;
const int   kMaxTileCount = 196;
}

class Sketch_WaveLatent_Navigator : public ISketch {
public:
    std::string getName() const override        { return "WaveLatent (Navigator)"; }
    std::string getDescription() const override { return "Explore the WaveLatent PCA plane with a 5x5 panel."; }
    std::string getAuthor() const override      { return "alice2 User"; }

    void setup() override {
        scene().setBackgroundColor(Color(0.92f, 0.92f, 0.94f, 1.f));
        scene().setShowGrid(false);
        scene().setShowAxes(false);

        if (!WaveLatent::loadDatasetFromJson(datasetPath_, dataset_, datasetOptions_)) {
            std::printf("[WaveLatent][Navigator] failed to load dataset '%s'\n", datasetPath_.c_str());
            return;
        }
        previewField_.configure(dataset_.gridResX, dataset_.gridResY,
                                dataset_.xMin, dataset_.xMax, dataset_.yMin, dataset_.yMax);
        zeroField_.assign(dataset_.fieldSize(), 0.f);
        previewField_.updateValues(zeroField_);
        updatePanelGrid();

        if (!initialiseModel(true)) {
            std::printf("[WaveLatent][Navigator] model initialisation failed.\n");
        } else {
            std::printf("[WaveLatent][Navigator] Ready. Move over the panel. Keys: I=info, T=train(%d), L=load\n",
                        trainEpochs_);
        }
    }

    void update(float) override {}

    void draw(Renderer& renderer, Camera&) override {
        if (dataset_.empty() || panelRows_ == 0 || panelCols_ == 0) {
            return;
        }

        computeLayout();

        const float cellW = tileSize_ / std::max(1.f, float(dataset_.gridResX));
        const float cellH = tileSize_ / std::max(1.f, float(dataset_.gridResY));
        const float previewCellW = previewTileSize_ / std::max(1.f, float(dataset_.gridResX));
        const float previewCellH = previewTileSize_ / std::max(1.f, float(dataset_.gridResY));

        renderer.setColor(Color(0.25f, 0.25f, 0.30f));
        renderer.drawString("Latent plane (PCA sample grid)", panelLeft_, panelTop_ - 12.f);

        for (int row = 0; row < panelRows_; ++row) {
            for (int col = 0; col < panelCols_; ++col) {
                float left, top;
                tileOrigin(row, col, left, top);
                drawTileFrame(renderer, left, top, tileSize_);
                const int tileIndex = row * panelCols_ + col;
                if (tileIndex < static_cast<int>(panelFields_.size())) {
                    panelFields_[tileIndex].draw(renderer, left, top, cellW, cellH, kIsoColor, 1.8f);
                }
            }
        }

        if (hoverTileI_ >= 0 && hoverTileJ_ >= 0) {
            float left, top;
            tileOrigin(hoverTileI_, hoverTileJ_, left, top);
            drawTileFrame(renderer, left - 2.f, top - 2.f, tileSize_ + 4.f,
                          Color(0.95f, 0.95f, 0.95f, 1.f), 2.2f);
        }

        renderer.setColor(Color(0.25f, 0.25f, 0.30f));
        renderer.drawString("Preview (decoded)", previewLeft_, previewTop_ - 12.f);
        drawTileFrame(renderer, previewLeft_, previewTop_, previewTileSize_);
        previewField_.draw(renderer, previewLeft_, previewTop_, previewCellW, previewCellH, kIsoColor, 2.2f);

        renderer.setColor(Color(0.95f, 0.2f, 0.2f, 1.0f));
        for (size_t i = 0; i < sampleUV_.size(); ++i) {
            Vec2 pt;
            if (i < sampleTileIndex_.size() && sampleTileIndex_[i] >= 0) {
                int tile = sampleTileIndex_[i];
                int row = tile / panelCols_;
                int col = tile % panelCols_;
                float left, top;
                tileOrigin(row, col, left, top);
                pt = Vec2(left + 0.5f * tileSize_, top + 0.5f * tileSize_);
            } else {
                pt = panelUVToXY(sampleUV_[i].x, sampleUV_[i].y);
            }
            if (pointInPanel(pt.x, pt.y)) {
                renderer.draw2dPoint(pt, Color(0.95f, 0.2f, 0.2f, 1.0f), 4.0f);
            }
        }

        renderer.setColor(Color(0.4f, 0.4f, 0.42f));
        renderer.drawString("[T] train  [L] load  [I] info  [ ] +/-1  { } +/-row",
                            previewLeft_, previewTop_ + previewTileSize_ + 28.f);
    }

    bool onMouseMove(int x, int y) override {
        if (x == lastMouseX_ && y == lastMouseY_) {
            return false;
        }
        lastMouseX_ = x;
        lastMouseY_ = y;

        computeLayout();

        if (!pointInPanel(float(x), float(y))) {
            hoverTileI_ = hoverTileJ_ = -1;
            previewField_.updateValues(zeroField_);
            return false;
        }

        float u, v;
        panelXYtoUV(float(x), float(y), u, v);

        hoverTileI_ = 0;
        hoverTileJ_ = 0;
        float bestDist = std::numeric_limits<float>::max();
        for (int row = 0; row < panelRows_; ++row) {
            for (int col = 0; col < panelCols_; ++col) {
                float left, top;
                tileOrigin(row, col, left, top);
                const float cx = left + 0.5f * tileSize_;
                const float cy = top + 0.5f * tileSize_;
                const float dx = float(x) - cx;
                const float dy = float(y) - cy;
                const float dist2 = dx * dx + dy * dy;
                if (dist2 < bestDist) {
                    bestDist = dist2;
                    hoverTileI_ = row;
                    hoverTileJ_ = col;
                }
            }
        }

        // snap to latent UV
        // int sampleIdx = -1;
        // if (hoverTileI_ >= 0 && hoverTileJ_ >= 0) {
        //     int tileIndex = hoverTileI_ * panelCols_ + hoverTileJ_;
        //     if (tileIndex >= 0 && tileIndex < static_cast<int>(tileSampleIndex_.size())) {
        //         sampleIdx = tileSampleIndex_[tileIndex];
        //     }
        // }
        // if (sampleIdx >= 0 && sampleIdx < static_cast<int>(sampleReconBuffers_.size())) {
        //     rebuildPreviewSample(sampleIdx);
        // } else {
        //     rebuildPreviewPlane(u, v);
        // }
        // return false;

        rebuildPreviewPlane(u, v);
        return false;
    }

    bool onKeyPress(unsigned char key, int, int) override {
        switch (key) {
        case 'i':
        case 'I':
            printInfo();
            return true;
        case 't':
        case 'T':
            runTraining(trainEpochs_);
            return true;
        case 'l':
        case 'L':
            if (waveLatent_.loadModel(modelPath_)) {
                modelConfig_ = waveLatent_.config();
                refreshLatentSpace();
                std::printf("[WaveLatent][Navigator] Model loaded from '%s'\n", modelPath_.c_str());
            }
            return true;
        case '[':
            changePanelTileCount(-1);
            return true;
        case '{':
            changePanelTileCount(-std::max(4, panelCols_));
            return true;
        case ']':
            changePanelTileCount(+1);
            return true;
        case '}':
            changePanelTileCount(+std::max(4, panelCols_));
            return true;
        default:
            return false;
        }
    }

private:
    bool initialiseModel(bool runInitialTraining) {
        if (!waveLatent_.setFields(dataset_)) {
            std::printf("[WaveLatent] setFields failed.\n");
            return false;
        }
        if (!waveLatent_.initialize(modelConfig_)) {
            std::printf("[WaveLatent] initialise failed.\n");
            return false;
        }

        if (runInitialTraining && initEpochs_ > 0) {
            WaveLatentTrainingParams params = baseTrainParams_;
            params.epochs = initEpochs_;
            waveLatent_.train(params);
        }

        refreshLatentSpace();
        waveLatent_.printDiagnostics();
        return true;
    }

    void changePanelTileCount(int delta) {
        int newCount = std::clamp(panelTileCount_ + delta, kMinTileCount, kMaxTileCount);
        if (newCount == panelTileCount_) {
            return;
        }
        panelTileCount_ = newCount;
        updatePanelGrid();
        hoverTileI_ = hoverTileJ_ = -1;
        previewField_.updateValues(zeroField_);
        refreshLatentSpace();
    }

    void updatePanelGrid() {
        panelTileCount_ = std::clamp(panelTileCount_, kMinTileCount, kMaxTileCount);
        panelCols_ = std::max(1, int(std::ceil(std::sqrt(float(panelTileCount_)))));
        panelRows_ = std::max(1, (panelTileCount_ + panelCols_ - 1) / panelCols_);
        const int tileCount = panelRows_ * panelCols_;
        panelFields_.resize(tileCount);
        tileSampleIndex_.assign(tileCount, -1);
        if (!dataset_.empty()) {
            for (auto& field : panelFields_) {
                field.configure(dataset_.gridResX, dataset_.gridResY,
                                dataset_.xMin, dataset_.xMax, dataset_.yMin, dataset_.yMax);
                field.updateValues(zeroField_);
            }
        }
    }

    void runTraining(int epochs) {
        if (epochs <= 0 || !waveLatent_.ready()) {
            return;
        }
        WaveLatentTrainingParams params = baseTrainParams_;
        params.epochs = epochs;
        waveLatent_.train(params);
        refreshLatentSpace();
        waveLatent_.printDiagnostics();
    }

    void refreshLatentSpace() {
        rebuildSampleRecon();
        waveLatent_.getAllLatents(latents_);

        buildPCA();
        precomputePanel();
        rebuildPreviewPlane(0.f, 0.f);
    }

    void rebuildSampleRecon() {
        const int sampleCount = waveLatent_.sampleCount();
        if (sampleCount <= 0) {
            sampleReconBuffers_.clear();
            sampleTileIndex_.clear();
            return;
        }
        sampleReconBuffers_.assign(sampleCount, std::vector<float>());
        sampleTileIndex_.assign(sampleCount, -1);
        std::vector<float> grid;
        grid.reserve(dataset_.fieldSize());
        for (int i = 0; i < sampleCount; ++i) {
            if (!waveLatent_.reconstructAutoencoder(i, grid)) {
                grid.assign(dataset_.fieldSize(), 0.f);
            }
            sampleReconBuffers_[i] = grid;
        }
    }

    void buildPCA() {
        const int sampleCount = static_cast<int>(latents_.size());
        if (sampleCount == 0) {
            return;
        }
        const int dim = static_cast<int>(latents_.front().size());
        pcaMean_.assign(dim, 0.f);
        for (const auto& z : latents_) {
            for (int d = 0; d < dim; ++d) {
                pcaMean_[d] += z[d];
            }
        }
        for (float& v : pcaMean_) {
            v /= float(sampleCount);
        }

        std::vector<float> covariance(dim * dim, 0.f);
        for (const auto& z : latents_) {
            for (int i = 0; i < dim; ++i) {
                const float zi = z[i] - pcaMean_[i];
                for (int j = 0; j < dim; ++j) {
                    covariance[i * dim + j] += zi * (z[j] - pcaMean_[j]);
                }
            }
        }
        const float invCount = 1.f / std::max(1, sampleCount);
        for (float& v : covariance) {
            v *= invCount;
        }

        pcaE1_.assign(dim, 0.f);
        pcaE2_.assign(dim, 0.f);
        float lam1 = 0.f;
        float lam2 = 0.f;
        powerEigen(covariance, dim, 64, pcaE1_, lam1);
        deflate(covariance, dim, pcaE1_, lam1);
        powerEigen(covariance, dim, 64, pcaE2_, lam2);

        const float s1 = std::sqrt(std::max(0.f, lam1));
        const float s2 = std::sqrt(std::max(0.f, lam2));
        const float scale = 2.0f;
        uMin_ = -scale * s1;
        uMax_ = +scale * s1;
        vMin_ = -scale * s2;
        vMax_ = +scale * s2;
        if (uMax_ - uMin_ < 1e-6f) { uMin_ -= 1.f; uMax_ += 1.f; }
        if (vMax_ - vMin_ < 1e-6f) { vMin_ -= 1.f; vMax_ += 1.f; }

        sampleUV_.clear();
        sampleUV_.reserve(latents_.size());
        for (const auto& z : latents_) {
            float u = 0.f;
            float v = 0.f;
            for (int d = 0; d < dim; ++d) {
                const float centered = z[d] - pcaMean_[d];
                u += centered * pcaE1_[d];
                v += centered * pcaE2_[d];
            }
            sampleUV_.emplace_back(u, v);
        }
    }

    void precomputePanel() {
        const int tileCount = panelRows_ * panelCols_;
        if (tileCount <= 0) {
            return;
        }
        tileSampleIndex_.assign(tileCount, -1);
        std::vector<float> tileBestDist(tileCount, std::numeric_limits<float>::max());
        sampleTileIndex_.assign(sampleUV_.size(), -1);

        const float denomU = std::max(1e-6f, uMax_ - uMin_);
        const float denomV = std::max(1e-6f, vMax_ - vMin_);

        for (size_t i = 0; i < sampleUV_.size(); ++i) {
            float xi = clamp01((sampleUV_[i].x - uMin_) / denomU);
            float yi = clamp01((sampleUV_[i].y - vMin_) / denomV);
            int col = std::clamp(int(std::floor(xi * panelCols_)), 0, std::max(0, panelCols_ - 1));
            int row = std::clamp(int(std::floor((1.f - yi) * panelRows_)), 0, std::max(0, panelRows_ - 1));
            const float targetU = lerp(uMin_, uMax_, (col + 0.5f) / float(std::max(1, panelCols_)));
            const float targetV = lerp(vMax_, vMin_, (row + 0.5f) / float(std::max(1, panelRows_)));
            const float du = sampleUV_[i].x - targetU;
            const float dv = sampleUV_[i].y - targetV;
            const float dist = du * du + dv * dv;
            const int tileIndex = row * panelCols_ + col;
            if (dist < tileBestDist[tileIndex]) {
                tileBestDist[tileIndex] = dist;
                tileSampleIndex_[tileIndex] = static_cast<int>(i);
            }
        }

        std::vector<float> grid;
        grid.reserve(dataset_.fieldSize());
        for (int row = 0; row < panelRows_; ++row) {
            for (int col = 0; col < panelCols_; ++col) {
                const int tileIndex = row * panelCols_ + col;
                const float u = lerp(uMin_, uMax_, (col + 0.5f) / float(std::max(1, panelCols_)));
                const float v = lerp(vMax_, vMin_, (row + 0.5f) / float(std::max(1, panelRows_)));
                const int sampleIdx = tileSampleIndex_[tileIndex];
                if (sampleIdx >= 0 && sampleIdx < static_cast<int>(sampleReconBuffers_.size())) {
                    panelFields_[tileIndex].updateValues(sampleReconBuffers_[sampleIdx]);
                    if (sampleIdx < static_cast<int>(sampleTileIndex_.size())) {
                        sampleTileIndex_[sampleIdx] = tileIndex;
                    }
                } else {
                    decodeOnPlane(u, v, grid);
                    panelFields_[tileIndex].updateValues(grid);
                }
            }
        }
    }

    void decodeOnPlane(float u, float v, std::vector<float>& gridOut) const {
        const int dim = static_cast<int>(pcaMean_.size());
        if (dim == 0) {
            gridOut.assign(dataset_.fieldSize(), 0.f);
            return;
        }
        std::vector<float> z(dim, 0.f);
        for (int d = 0; d < dim; ++d) {
            z[d] = pcaMean_[d] + u * pcaE1_[d] + v * pcaE2_[d];
        }
        waveLatent_.decodeLatentToGrid(z, gridOut);
    }

    void rebuildPreviewPlane(float u, float v) {
        std::vector<float> grid;
        grid.reserve(dataset_.fieldSize());
        decodeOnPlane(u, v, grid);
        previewField_.updateValues(grid);
    }

    void rebuildPreviewSample(int sampleIdx) {
        if (sampleIdx >= 0 && sampleIdx < static_cast<int>(sampleReconBuffers_.size())) {
            previewField_.updateValues(sampleReconBuffers_[sampleIdx]);
        }
    }

    void computeLayout() {
        const float winW = 1280.f;
        const float winH = 720.f;
        const float availableH = winH - 2.f * margin_;
        const float minPreview = 140.f;
        const float maxPreview = std::min(320.f, availableH);

        float maxPanelWidth = winW - 2.f * margin_ - previewGap_ - minPreview;
        if (maxPanelWidth <= 0.f) {
            maxPanelWidth = winW - 2.f * margin_;
        }

        float tileW = (maxPanelWidth - tileGap_ * std::max(0, panelCols_ - 1)) / std::max(1, panelCols_);
        float tileH = (availableH - tileGap_ * std::max(0, panelRows_ - 1)) / std::max(1, panelRows_);
        tileSize_ = std::max(32.f, std::min(tileW, tileH));

        panelWidth_ = panelCols_ * tileSize_ + tileGap_ * std::max(0, panelCols_ - 1);
        panelHeight_ = panelRows_ * tileSize_ + tileGap_ * std::max(0, panelRows_ - 1);

        float remainingW = winW - 2.f * margin_ - panelWidth_ - previewGap_;
        previewTileSize_ = std::clamp(remainingW, minPreview, maxPreview);
        previewTileSize_ = std::clamp(previewTileSize_, minPreview, availableH);

        panelLeft_ = margin_;
        panelTop_ = margin_ + 10.f;
        previewLeft_ = panelLeft_ + panelWidth_ + previewGap_;
        previewTop_ = margin_;
        previewWidth_ = previewTileSize_;
        previewHeight_ = previewTileSize_;
    }

    void tileOrigin(int row, int col, float& left, float& top) const {
        left = panelLeft_ + col * (tileSize_ + tileGap_);
        top  = panelTop_  + row * (tileSize_ + tileGap_);
    }

    bool pointInPanel(float x, float y) const {
        return (x >= panelLeft_ && x <= panelLeft_ + panelWidth_ &&
                y >= panelTop_  && y <= panelTop_  + panelHeight_);
    }

    void panelXYtoUV(float x, float y, float& u, float& v) const {
        const float xi = clamp01((x - panelLeft_) / std::max(1e-6f, panelWidth_));
        const float yi = clamp01((y - panelTop_)  / std::max(1e-6f, panelHeight_));
        u = lerp(uMin_, uMax_, xi);
        v = lerp(vMax_, vMin_, yi);
    }
    Vec2 panelUVToXY(float u, float v) const {
        const float xi = clamp01((u - uMin_) / std::max(1e-6f, uMax_ - uMin_));
        const float yi = clamp01((v - vMin_) / std::max(1e-6f, vMax_ - vMin_));
        const float sx = panelLeft_ + xi * panelWidth_;
        const float sy = panelTop_ + (1.f - yi) * panelHeight_;
        return Vec2(sx, sy);
    }

    void drawTileFrame(Renderer& renderer, float left, float top, float size,
                       const Color& color = kFrameColor, float thickness = 1.4f) const {
        const float right = left + size;
        const float bottom = top + size;
        renderer.draw2dLine(Vec2(left, top), Vec2(right, top), color, thickness);
        renderer.draw2dLine(Vec2(right, top), Vec2(right, bottom), color, thickness);
        renderer.draw2dLine(Vec2(right, bottom), Vec2(left, bottom), color, thickness);
        renderer.draw2dLine(Vec2(left, bottom), Vec2(left, top), color, thickness);
    }

    static void powerEigen(const std::vector<float>& cov, int dim, int iterations,
                           std::vector<float>& outVec, float& outLambda) {
        outVec.assign(dim, 0.f);
        for (int d = 0; d < dim; ++d) {
            outVec[d] = 0.1f + 0.013f * float(d + 1);
        }
        normalize(outVec);

        std::vector<float> tmp(dim, 0.f);
        for (int it = 0; it < iterations; ++it) {
            std::fill(tmp.begin(), tmp.end(), 0.f);
            for (int i = 0; i < dim; ++i) {
                float s = 0.f;
                for (int j = 0; j < dim; ++j) {
                    s += cov[i * dim + j] * outVec[j];
                }
                tmp[i] = s;
            }
            normalize(tmp);
            outVec.swap(tmp);
        }

        float numerator = 0.f;
        float denominator = 0.f;
        for (int i = 0; i < dim; ++i) {
            float Ci = 0.f;
            for (int j = 0; j < dim; ++j) {
                Ci += cov[i * dim + j] * outVec[j];
            }
            numerator += outVec[i] * Ci;
            denominator += outVec[i] * outVec[i];
        }
        outLambda = (denominator > 0.f) ? numerator / denominator : 0.f;
    }

    static void deflate(std::vector<float>& cov, int dim, const std::vector<float>& vec, float lambda) {
        for (int i = 0; i < dim; ++i) {
            for (int j = 0; j < dim; ++j) {
                cov[i * dim + j] -= lambda * vec[i] * vec[j];
            }
        }
    }

    static void normalize(std::vector<float>& vec) {
        double norm2 = 0.0;
        for (float v : vec) {
            norm2 += double(v) * double(v);
        }
        const float inv = (norm2 > 0.0) ? float(1.0 / std::sqrt(norm2)) : 1.f;
        for (float& v : vec) {
            v *= inv;
        }
    }

    static float clamp01(float x) {
        return std::max(0.f, std::min(1.f, x));
    }

    static float lerp(float a, float b, float t) {
        return a + (b - a) * t;
    }

    void printInfo() const {
        std::printf("[WaveLatent][Navigator] samples=%d  grid=%dx%d  basisSelected=%d/%d  latentDim=%d\n",
                    waveLatent_.sampleCount(),
                    dataset_.gridResX, dataset_.gridResY,
                    waveLatent_.basisRank(), waveLatent_.fullBasisRank(),
                    waveLatent_.latentDim());
        std::printf("  bbox=[%.3f %.3f] x [%.3f %.3f]\n",
                    dataset_.xMin, dataset_.xMax, dataset_.yMin, dataset_.yMax);
        std::printf("  PCA limits: u=[%.3f, %.3f] v=[%.3f, %.3f]\n", uMin_, uMax_, vMin_, vMax_);
    }

private:
    std::string datasetPath_ = "inShapes.json";
    std::string modelPath_   = "WaveLatentModel.json";

    WaveLatentDatasetOptions datasetOptions_{256, 256, -1.2f, 1.2f, -1.2f, 1.2f};
    WaveLatentDataset dataset_{};

    WaveLatentConfig modelConfig_{64, 64, 2048, 16, 1234};
    WaveLatentTrainingParams baseTrainParams_{200, 5e-3f, 1e-6f, 1e-3f, 50};
    int initEpochs_ = 10;
    int trainEpochs_ = 200;

    WaveLatent waveLatent_;
    std::vector<std::vector<float>> latents_;
    std::vector<Vec2> sampleUV_;
    std::vector<float> zeroField_;
    std::vector<std::vector<float>> sampleReconBuffers_;
    std::vector<int> sampleTileIndex_;

    std::vector<float> pcaMean_;
    std::vector<float> pcaE1_;
    std::vector<float> pcaE2_;
    float uMin_ = -1.f;
    float uMax_ = 1.f;
    float vMin_ = -1.f;
    float vMax_ = 1.f;

    std::vector<GridField> panelFields_;
    GridField previewField_;

    int panelTileCount_ = kDefaultTileCount;
    int panelRows_ = 0;
    int panelCols_ = 0;
    std::vector<int> tileSampleIndex_;

    float panelLeft_ = 0.f;
    float panelTop_ = 0.f;
    float panelWidth_ = 0.f;
    float panelHeight_ = 0.f;
    float previewLeft_ = 0.f;
    float previewTop_ = 0.f;
    float previewWidth_ = 0.f;
    float previewHeight_ = 0.f;
    float margin_ = 20.f;
    float tileSize_ = 140.f;
    float tileGap_ = 18.f;
    float previewTileSize_ = 260.f;
    float previewGap_ = 60.f;

    int hoverTileI_ = -1;
    int hoverTileJ_ = -1;
    int lastMouseX_ = -1;
    int lastMouseY_ = -1;
};

ALICE2_REGISTER_SKETCH_AUTO(Sketch_WaveLatent_Navigator)

#endif // __MAIN__

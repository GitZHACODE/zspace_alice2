#define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>

#include <cstdio>
#include <string>
#include <vector>

#include "ML/WaveLatent/WaveLatent.h"

using namespace alice2;

namespace {
const Color kIsoColor(0.10f, 0.35f, 0.85f, 1.0f);
const Color kFrameColor(0.15f, 0.15f, 0.15f, 1.0f);
}

class Sketch_WaveLatent_Trainer : public ISketch {
public:
    std::string getName() const override        { return "WaveLatent (Trainer)"; }
    std::string getDescription() const override { return "Train WaveLatent and compare reconstructions."; }
    std::string getAuthor() const override      { return "alice2 User"; }

    void setup() override {
        scene().setBackgroundColor(Color(0.92f, 0.92f, 0.94f, 1.f));
        scene().setShowGrid(false);
        scene().setShowAxes(false);

        if (!WaveLatent::loadDatasetFromJson(datasetPath_, dataset_, datasetOptions_)) {
            std::printf("[WaveLatent][Trainer] failed to load dataset '%s'\n", datasetPath_.c_str());
            return;
        }
        configureFields(gtFields_, dataset_.fields);
        configureFields(aeFields_, dataset_.fields);
        configureFields(basisFields_, dataset_.fields);

        if (!initialiseModel(true)) {
            std::printf("[WaveLatent][Trainer] model initialisation failed.\n");
        } else {
            std::printf("[WaveLatent] Trainer ready. Keys: I=info, T=train(%d), J=save, L=load\n", trainEpochs_);
        }
    }

    void update(float) override {}

    void draw(Renderer& renderer, Camera&) override {
        if (dataset_.empty() || waveLatent_.sampleCount() == 0) {
            return;
        }

        const float startY = margin_ + 10.f;
        drawFieldRow(renderer, gtFields_, startY, "Ground Truth");
        drawFieldRow(renderer, aeFields_, startY + tileSize_ + rowGap_, "AE Reconstruction");
        drawFieldRow(renderer, basisFields_, startY + 2.f * (tileSize_ + rowGap_), "Basis-only Reconstruction");

        renderer.setColor(Color(0.35f, 0.35f, 0.38f));
        renderer.drawString("[T] train  [J] save  [L] load  [I] info", margin_, startY + 3.f * (tileSize_ + rowGap_) - 18.f);
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
        case 'j':
        case 'J':
            if (waveLatent_.saveModel(modelPath_)) {
                std::printf("[WaveLatent] Model saved to '%s'\n", modelPath_.c_str());
            }
            return true;
        case 'l':
        case 'L':
            if (waveLatent_.loadModel(modelPath_)) {
                modelConfig_ = waveLatent_.config();
                rebuildReconstructions();
                std::printf("[WaveLatent] Model loaded from '%s'\n", modelPath_.c_str());
            }
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

        rebuildReconstructions();
        waveLatent_.printDiagnostics();
        return true;
    }

    void runTraining(int epochs) {
        if (epochs <= 0 || !waveLatent_.ready()) {
            return;
        }
        WaveLatentTrainingParams params = baseTrainParams_;
        params.epochs = epochs;
        waveLatent_.train(params);
        rebuildReconstructions();
        waveLatent_.printDiagnostics();
    }

    void rebuildReconstructions() {
        const int sampleCount = waveLatent_.sampleCount();
        std::vector<float> buffer;
        buffer.reserve(dataset_.fieldSize());

        for (int i = 0; i < sampleCount; ++i) {
            if (waveLatent_.reconstructAutoencoder(i, buffer)) {
                aeFields_[i].updateValues(buffer);
            }
            if (waveLatent_.reconstructBasisOnly(i, buffer)) {
                basisFields_[i].updateValues(buffer);
            }
        }
    }

    void configureFields(std::vector<GridField>& fields, const std::vector<std::vector<float>>& samples) const {
        fields.resize(samples.size());
        for (size_t i = 0; i < samples.size(); ++i) {
            fields[i].configure(dataset_.gridResX, dataset_.gridResY,
                                dataset_.xMin, dataset_.xMax, dataset_.yMin, dataset_.yMax);
            fields[i].updateValues(samples[i]);
        }
    }

    void drawFieldRow(Renderer& renderer, const std::vector<GridField>& fields,
                      float top, const char* label) const {
        if (fields.empty()) {
            return;
        }
        const float cellW = tileSize_ / static_cast<float>(dataset_.gridResX);
        const float cellH = tileSize_ / static_cast<float>(dataset_.gridResY);

        renderer.setColor(Color(0.25f, 0.25f, 0.30f));
        renderer.drawString(label, margin_, top - 8.f);

        for (size_t i = 0; i < fields.size(); ++i) {
            const float left = margin_ + float(i) * (tileSize_ + tileGap_);
            drawTileFrame(renderer, left, top);
            fields[i].draw(renderer, left, top, cellW, cellH, kIsoColor, 2.0f);
        }
    }

    void drawTileFrame(Renderer& renderer, float left, float top) const {
        const float right  = left + tileSize_;
        const float bottom = top + tileSize_;
        renderer.draw2dLine(Vec2(left, top), Vec2(right, top), kFrameColor, 1.4f);
        renderer.draw2dLine(Vec2(right, top), Vec2(right, bottom), kFrameColor, 1.4f);
        renderer.draw2dLine(Vec2(right, bottom), Vec2(left, bottom), kFrameColor, 1.4f);
        renderer.draw2dLine(Vec2(left, bottom), Vec2(left, top), kFrameColor, 1.4f);
    }

    void printInfo() const {
        std::printf("[WaveLatent] samples=%d  grid=%dx%d  basisSelected=%d/%d  latentDim=%d\n",
                    waveLatent_.sampleCount(),
                    dataset_.gridResX, dataset_.gridResY,
                    waveLatent_.basisRank(), waveLatent_.fullBasisRank(),
                    waveLatent_.latentDim());
        std::printf("[WaveLatent] bbox=[%.3f %.3f] x [%.3f %.3f]\n",
                    dataset_.xMin, dataset_.xMax, dataset_.yMin, dataset_.yMax);
    }

private:
    std::string datasetPath_ = "inShapes.json";
    std::string modelPath_   = "WaveLatentModel.json";

    WaveLatentDatasetOptions datasetOptions_{128, 128, -1.2f, 1.2f, -1.2f, 1.2f};
    WaveLatentDataset dataset_{};

    WaveLatentConfig modelConfig_{64, 64, 2048, 16, 1234};
    WaveLatentTrainingParams baseTrainParams_{200, 5e-3f, 1e-6f, 1e-3f, 50};
    int initEpochs_ = 10;
    int trainEpochs_ = 200;

//     struct WaveLatentConfig {
//     int Kx = 32;
//     int Ky = 32;
//     int keepK = 512;
//     int latentDim = 16;
//     unsigned seed = 1234;
// };

// struct WaveLatentTrainingParams {
//     int epochs = 200;
//     float learningRate = 5e-3f;
//     float weightDecay = 1e-6f;
//     float latentReg = 1e-3f;
//     int printEvery = 50;
// };

    std::vector<GridField> gtFields_;
    std::vector<GridField> aeFields_;
    std::vector<GridField> basisFields_;

    float margin_   = 20.f;
    float tileSize_ = 220.f;
    float tileGap_  = 25.f;
    float rowGap_   = 48.f;

    WaveLatent waveLatent_;
};

ALICE2_REGISTER_SKETCH_AUTO(Sketch_WaveLatent_Trainer)

#endif // __MAIN__

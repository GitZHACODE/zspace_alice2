#define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>

#include <ML/DeepSDF/LatentSDF_CUDA.h>
#include <ML/DeepSDF/LatentNavigator_CUDA.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <cstdio>

using namespace alice2;
using namespace DeepSDF;

namespace {
constexpr float kPanelLeft  = 10.0f;
constexpr float kPanelTop   = 540.0f;
constexpr float kPanelSize  = 400.0f;
constexpr float kFieldLeft  = 10;
constexpr float kFieldTop   = 80;
constexpr float kFieldSize  = 400.0f;
} // namespace

class Sketch_LatentNavigator_CUDA : public ISketch {
public:
    std::string getName() const override { return "Latent Navigator (CUDA)"; }
    std::string getDescription() const override { return "GPU latent interpolation panel with decoder detail."; }
    std::string getAuthor() const override { return "alice2 User"; }

    void setup() override {
        scene().setBackgroundColor(Color(0.0f, 0.0f, 0.0f));
        scene().setShowGrid(false);
        scene().setShowAxes(false);

        bool loaded = decoder_.loadModelJSON(modelPath_, domain_, /*maxBatch*/256, /*seed*/1234);
        if (!loaded) {
            std::printf("[Navigator][CUDA] Failed to load '%s'. Panel will be empty.\n", modelPath_.c_str());
        } else {
            std::printf("[Navigator][CUDA] Loaded model '%s' (shapes=%d)\n",
                        modelPath_.c_str(), decoder_.numShapes());
        }

        navigator_.initialize(&decoder_, &domain_, &decoder_.latents());
        navigator_.setPanelResolution(panelN_, panelTileRes_, panelGap_);
        navigator_.markPanelDirty();

        updateDetailField(0.5f, 0.5f);
    }

    void cleanup() override {
        navigator_.shutdown();
    }

    void update(float) override {}

    void draw(Renderer& renderer, Camera&) override {
        renderer.setColor(Color(0.9f, 0.9f, 0.9f));
        renderer.drawString("Latent Navigator (CUDA) - hover over panel to update detail. J: reload model",
                            20.0f, 24.0f);

        navigator_.drawPanel(renderer, kPanelLeft, kPanelTop, kPanelSize, config_);

        if (hoverUV_) {
            const float u = hoverUV_->first;
            const float v = hoverUV_->second;
            const float x = kPanelLeft + u * kPanelSize;
            const float y = kPanelTop  + v * kPanelSize;
            renderer.draw2dLine(Vec2(x - 6.0f, y), Vec2(x + 6.0f, y), Color(1.0f, 0.25f, 0.25f), 2.0f);
            renderer.draw2dLine(Vec2(x, y - 6.0f), Vec2(x, y + 6.0f), Color(1.0f, 0.25f, 0.25f), 2.0f);
        }

        renderer.setColor(Color(0.8f, 0.8f, 0.9f));
        renderer.drawString("Reconstruction", kFieldLeft, kFieldTop - 18.0f);
        renderer.drawString("Latent Panel", kPanelLeft, kPanelTop - 18.0f);
        navigator_.drawField(renderer, detailField_, kFieldLeft, kFieldTop, kFieldSize, config_);
    }

    bool onMouseMove(int x, int y) override {
        const float fx = static_cast<float>(x);
        const float fy = static_cast<float>(y);
        if (fx < kPanelLeft || fy < kPanelTop ||
            fx > kPanelLeft + kPanelSize || fy > kPanelTop + kPanelSize) {
            return false;
        }
        const float u = (fx - kPanelLeft) / kPanelSize;
        const float v = (fy - kPanelTop)  / kPanelSize;
        hoverUV_ = std::make_pair(std::clamp(u, 0.0f, 1.0f),
                                  std::clamp(v, 0.0f, 1.0f));
        updateDetailField(hoverUV_->first, hoverUV_->second);
        return true;
    }

    bool onKeyPress(unsigned char k, int, int) override {
        switch (k) {
        case '0':
            config_.debugMode = 0;
            navigator_.markPanelDirty();
            updateDetailForCurrent();
            return true;
        case '1':
            config_.debugMode = 1;
            navigator_.markPanelDirty();
            updateDetailForCurrent();
            return true;
        case 'm': case 'M':
            config_.softMask = !config_.softMask;
            navigator_.markPanelDirty();
            updateDetailForCurrent();
            return true;
        case '[':
            config_.tau = std::max(0.005f, config_.tau * 0.8f);
            navigator_.markPanelDirty();
            updateDetailForCurrent();
            return true;
        case ']':
            config_.tau = std::min(0.5f, config_.tau * 1.25f);
            navigator_.markPanelDirty();
            updateDetailForCurrent();
            return true;
        case 'j': case 'J': {
            FieldDomain loadedDomain = domain_;
            if (decoder_.loadModelJSON(modelPath_, loadedDomain)) {
                domain_ = loadedDomain;
                navigator_.shutdown();
                navigator_.initialize(&decoder_, &domain_, &decoder_.latents());
                navigator_.setPanelResolution(panelN_, panelTileRes_, panelGap_);
                navigator_.markPanelDirty();
                updateDetailField(0.5f, 0.5f);
                hoverUV_.reset();
                std::printf("[Navigator][CUDA] Reloaded '%s'\n", modelPath_.c_str());
            } else {
                std::printf("[Navigator][CUDA] Failed to reload '%s'\n", modelPath_.c_str());
            }
            return true;
        }
        default:
            break;
        }
        return false;
    }

private:
    void updateDetailForCurrent() {
        if (hoverUV_) updateDetailField(hoverUV_->first, hoverUV_->second);
        else          updateDetailField(0.5f, 0.5f);
    }

    void updateDetailField(float u, float v) {
        if (!navigator_.getBlendedField(u, v, detailField_)) {
            detailField_.values.clear();
            detailField_.minValue = detailField_.maxValue = 0.0f;
        }
    }

    TinyAutoDecoderCUDA decoder_;
    FieldDomain domain_{};
    LatentNavigator_CUDA navigator_;
    FieldRenderConfig config_{};

    std::optional<std::pair<float,float>> hoverUV_;
    GridField detailField_;

    int panelN_ = 10;
    int panelTileRes_ = 56;
    int panelGap_ = 4;

    std::string modelPath_ = "latent_model.json";
};

ALICE2_REGISTER_SKETCH_AUTO(Sketch_LatentNavigator_CUDA)

#endif // __MAIN__

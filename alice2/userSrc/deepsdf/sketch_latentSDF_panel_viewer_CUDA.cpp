#define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>

#include <ML/DeepSDF/LatentSDF_CUDA.h>
#include <ML/DeepSDF/LatentNavigator_CUDA.h>

#include <algorithm>
#include <cstdio>
#include <optional>
#include <string>
#include <utility>

using namespace alice2;
using DeepSDF::TinyAutoDecoderCUDA;
using DeepSDF::LatentNavigator_CUDA;
using DeepSDF::FieldRenderConfig;
using DeepSDF::GridField;
using DeepSDF::FieldDomain;

class Sketch_LatentPanelViewer_CUDA : public ISketch {
public:
    std::string getName() const override        { return "Latent Panel Viewer (CUDA)"; }
    std::string getDescription() const override { return "Loads a trained auto-decoder and displays a large latent panel."; }
    std::string getAuthor() const override      { return "alice2 User"; }

    void setup() override {
        scene().setBackgroundColor(Color(0.95f, 0.95f, 0.95f));
        scene().setShowAxes(false);
        scene().setShowGrid(false);

        const bool loaded = decoder_.loadModelJSON(modelPath_, domain_, 256, 1234);
        if (!loaded) {
            decoder_.initialize(/*numShapes*/4, /*latentDim*/16, {64,64,64}, 1234, 256);
            domain_ = FieldDomain{};
            status_ = "WARNING: Failed to load latent model. Showing default latents.";
        } else {
            decoder_.syncLatentsToHost();
            status_ = "Loaded '" + modelPath_ + "'  Shapes: " + std::to_string(decoder_.numShapes());
        }

        navigator_.initialize(&decoder_, &domain_, &decoder_.latents());
        navigator_.setPanelResolution(panelTiles_, panelTileRes_, panelGap_);
        navigator_.markPanelDirty();

        updateDetailField(0.5f, 0.5f);
    }

    void cleanup() override {
        navigator_.shutdown();
    }

    void update(float) override {}

    void draw(Renderer& r, Camera&) override {
        r.setColor(Color(0.1f, 0.1f, 0.1f));
        r.drawString("Latent Panel Viewer (CUDA)", 20.0f, 30.0f);
        r.drawString(status_, 20.0f, 54.0f);
        r.drawString("Mouse hover: preview tile. Click = lock preview. Press R to reload JSON.", 20.0f, 78.0f);

        // Big panel
        navigator_.drawPanel(r, panelLeft_, panelTop_, panelPixelSize_, fieldRenderCfg_);
        if (hoverUV_) {
            const float u = hoverUV_->first;
            const float v = hoverUV_->second;
            const float px = panelLeft_ + u * panelPixelSize_;
            const float py = panelTop_ + v * panelPixelSize_;
            r.draw2dLine(Vec2(px - 10.0f, py), Vec2(px + 10.0f, py), Color(1.0f, 0.2f, 0.2f), 2.0f);
            r.draw2dLine(Vec2(px, py - 10.0f), Vec2(px, py + 10.0f), Color(1.0f, 0.2f, 0.2f), 2.0f);
        }

        // Detail preview next to the panel
        const float detailLeft = panelLeft_ + panelPixelSize_ + 40.0f;
        const float detailTop  = panelTop_;
        r.setColor(Color(0.2f, 0.2f, 0.2f));
        r.drawString("Blended field preview", detailLeft, detailTop - 20.0f);
        if (!detailField_.values.empty()) {
            navigator_.drawField(r, detailField_, detailLeft, detailTop, detailPixelSize_, fieldRenderCfg_);
        }
    }

    bool onMouseMove(int x, int y) override {
        const auto uv = panelUVFromMouse(static_cast<float>(x), static_cast<float>(y));
        if (uv) {
            hoverUV_ = *uv;
            updateDetailField(uv->first, uv->second);
            status_ = "Hover uv=(" + fmt2(uv->first) + ", " + fmt2(uv->second) + ")";
            return true;
        }
        return false;
    }

    bool onMousePress(int button, int state, int x, int y) override {
        if (button != 0 || state != 0) return false;
        const auto uv = panelUVFromMouse(static_cast<float>(x), static_cast<float>(y));
        if (!uv) return false;
        hoverUV_ = *uv;
        updateDetailField(uv->first, uv->second);
        status_ = "Pinned uv=(" + fmt2(uv->first) + ", " + fmt2(uv->second) + ")";
        return true;
    }

    bool onKeyPress(unsigned char key, int, int) override {
        if (key == 'r' || key == 'R') {
            reloadModel();
            return true;
        }
        return false;
    }

private:
    TinyAutoDecoderCUDA   decoder_;
    FieldDomain           domain_{};
    LatentNavigator_CUDA  navigator_;
    FieldRenderConfig     fieldRenderCfg_{};
    GridField             detailField_;
    std::optional<std::pair<float,float>> hoverUV_;

    std::string status_ = "Ready";
    std::string modelPath_ = "latent_model.json";

    int   panelTiles_    = 12;
    int   panelTileRes_  = 88;
    int   panelGap_      = 6;
    float panelPixelSize_ = 720.0f;
    float panelLeft_      = 20.0f;
    float panelTop_       = 120.0f;
    float detailPixelSize_ = 340.0f;

    void reloadModel() {
        const bool loaded = decoder_.loadModelJSON(modelPath_, domain_, 256, 1234);
        if (!loaded) {
            status_ = "ERROR: reload failed – keeping existing decoder.";
            return;
        }
        decoder_.syncLatentsToHost();
        navigator_.shutdown();
        navigator_.initialize(&decoder_, &domain_, &decoder_.latents());
        navigator_.setPanelResolution(panelTiles_, panelTileRes_, panelGap_);
        navigator_.markPanelDirty();
        updateDetailField(0.5f, 0.5f);
        hoverUV_.reset();
        status_ = "Reloaded '" + modelPath_ + "'";
    }

    std::optional<std::pair<float,float>> panelUVFromMouse(float mx, float my) const {
        if (mx < panelLeft_ || my < panelTop_) return std::nullopt;
        if (mx > panelLeft_ + panelPixelSize_) return std::nullopt;
        if (my > panelTop_ + panelPixelSize_) return std::nullopt;
        const float u = (mx - panelLeft_) / panelPixelSize_;
        const float v = (my - panelTop_)  / panelPixelSize_;
        return std::make_pair(std::clamp(u, 0.0f, 1.0f),
                              std::clamp(v, 0.0f, 1.0f));
    }

    void updateDetailField(float u, float v) {
        if (!navigator_.getBlendedField(u, v, detailField_)) {
            detailField_.values.clear();
            detailField_.minValue = detailField_.maxValue = 0.0f;
        }
    }

    static std::string fmt2(float v) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.2f", v);
        return std::string(buf);
    }
};

ALICE2_REGISTER_SKETCH_AUTO(Sketch_LatentPanelViewer_CUDA)

#endif // __MAIN__

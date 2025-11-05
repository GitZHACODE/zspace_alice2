#define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>

#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

#include "ML/WaveLatent/WaveLatentNavigator.h"
#include "computeGeom/FieldStack.h"

using namespace alice2;

namespace {
constexpr float kMargin = 20.0f;
constexpr float kDetailSize = 300.0f;
constexpr float kPanelSize = 300.0f;
constexpr float kDetailPanelGap = 16.0f;
constexpr float kTileGap = 3.0f;
constexpr float kVerticalOffset = 0.0f;
constexpr float kUIExtraOffset = 80.0f;
constexpr float kUIContentHeight = 190.0f;
constexpr float kUIToDetailGap = 30.0f;

std::string f2(float value) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << value;
    return oss.str();
}
} // namespace

class Sketch_WaveLatent_Stack : public ISketch {
public:
    std::string getName() const override        { return "WaveLatent Panel"; }
    std::string getDescription() const override { return "Inspect a saved WaveLatent model through a latent panel."; }
    std::string getAuthor() const override      { return "alice2 User"; }

    void setup() override {
        scene().setBackgroundColor(Color(0.92f, 0.92f, 0.94f));
        scene().setShowAxes(false);
        scene().setShowGrid(false);

        ui_ = std::make_unique<SimpleUI>(input());
        ui_->setTheme(SimpleUI::UITheme::Dark);

        const float uiLeft = kMargin;
        const float uiTop = kMargin + kVerticalOffset + kUIExtraOffset;
        const float sliderWidth = 260.0f;

        const float detailTop = kMargin + kVerticalOffset + kUIExtraOffset + kUIContentHeight + kUIToDetailGap;
        WaveLatentNavigator::Config navConfig;
        navConfig.modelPath = modelPath_;
        navConfig.detailOrigin = Vec2{kMargin, detailTop};
        navConfig.panelOrigin = Vec2{kMargin, detailTop + kDetailSize + kDetailPanelGap};
        navConfig.detailSize = kDetailSize;
        navConfig.panelSize = kPanelSize;
        navConfig.tileGap = kTileGap;
        navConfig.latentSnapRadius = latentSnapRadius_;
        navConfig.defaultGenLayers = genLayers_;
        navConfig.ui = ui_.get();
        navConfig.uiGenSliderPos = Vec2{uiLeft, uiTop + 60.0f};
        navConfig.uiGenSliderWidth = sliderWidth;

        WaveLatentNavigator::Callbacks navCallbacks;
        navCallbacks.onSliceDecoded = [this](ScalarField2D&& slice) {
            fieldStack_.appendSlice(std::move(slice));
        };
        navCallbacks.onStackGenerated = [this](std::vector<ScalarField2D>&& slices) {
            fieldStack_.replaceSlices(std::move(slices));
        };
        navCallbacks.onClearRequested = [this]() {
            fieldStack_.clearSlices();
        };
        navCallbacks.onStatusChanged = [this](const std::string& msg) {
            setStatus(msg);
        };

        if (!navigator_.initialize(navConfig, navCallbacks)) {
            setStatus("Navigator init failed");
            ready_ = false;
            return;
        }

        FieldStack::Config stackConfig;
        stackConfig.scene = &scene();
        stackConfig.boundsMin = navigator_.boundsMin();
        stackConfig.boundsMax = navigator_.boundsMax();
        stackConfig.totalHeight = totalHeight_;
        stackConfig.isoLevel = isoLevel_;
        stackConfig.contoursVisible = contoursVisible_;
        stackConfig.meshObjectName = "WaveStackIsoMesh";
        stackConfig.bboxObjectName = "WaveStackBounds";
        stackConfig.exportMeshPath = exportMeshName_;
        stackConfig.exportContoursPath = exportContoursName_;
        stackConfig.exportFieldsPath = exportStackFieldsName_;

        FieldStack::Callbacks stackCallbacks;
        stackCallbacks.onStatusChanged = [this](const std::string& msg) {
            setStatus(msg);
        };

        if (!fieldStack_.initialize(stackConfig, stackCallbacks)) {
            setStatus("Field stack init failed");
            ready_ = false;
            return;
        }

        FieldStack::UIConfig stackUI;
        stackUI.ui = ui_.get();
        stackUI.sliderWidth = sliderWidth;
        stackUI.sliderTotalHeightPos = Vec2{uiLeft, uiTop};
        stackUI.sliderIsoPos = Vec2{uiLeft, uiTop + 30.0f};
        stackUI.toggleSize = Vec2{120.0f, 22.0f};
        stackUI.toggleSmoothPos = Vec2{uiLeft, uiTop + 100.0f};
        stackUI.toggleLaplacianPos = Vec2{uiLeft + 140.0f, uiTop + 100.0f};
        stackUI.toggleBuildMeshPos = Vec2{uiLeft, uiTop + 130.0f};
        stackUI.toggleMeshVisiblePos = Vec2{uiLeft + 140.0f, uiTop + 130.0f};
        stackUI.toggleContoursPos = Vec2{uiLeft + 280.0f, uiTop + 130.0f};
        stackUI.toggleExportPos = Vec2{uiLeft, uiTop + 160.0f};
        fieldStack_.attachUI(stackUI);

        ready_ = true;
        setStatus("Ready");
    }

    void cleanup() override {
        fieldStack_.shutdown();
        navigator_.shutdown();
        ui_.reset();
        ready_ = false;
    }

    void update(float) override {
        if (!ready_) {
            return;
        }
        navigator_.update();
        fieldStack_.update();
    }

    void draw(Renderer& renderer, Camera&) override {
        renderer.setColor(Color(0.1f, 0.1f, 0.1f));
        const float textYOffset = kVerticalOffset;
        renderer.drawString("WaveLatent Stack Viewer", kMargin, 24.0f + textYOffset);
        renderer.drawString("Slices: " + std::to_string(fieldStack_.size()) +
                            "   Height: " + f2(fieldStack_.totalHeight()) +
                            "   Iso: " + f2(fieldStack_.isoLevel()),
                            kMargin, 44.0f + textYOffset);
        renderer.drawString(statusMessage_, kMargin, 64.0f + textYOffset);

        if (!ready_) {
            if (ui_) {
                ui_->draw(renderer);
            }
            return;
        }

        navigator_.draw(renderer);

        if (ui_) {
            ui_->draw(renderer);
        }
    }

    bool onMouseMove(int x, int y) override {
        if (ui_ && ui_->onMouseMove(x, y)) {
            return true;
        }
        if (!ready_) {
            return false;
        }
        return navigator_.handleMouseMove(x, y);
    }

    bool onMousePress(int button, int state, int x, int y) override {
        if (ui_ && ui_->onMousePress(button, state, x, y)) {
            return true;
        }
        if (!ready_) {
            return false;
        }
        return navigator_.handleMousePress(button, state, x, y);
    }

    bool onKeyPress(unsigned char key, int, int) override {
        if (!ready_) {
            return false;
        }
        if (navigator_.handleKeyPress(key)) {
            return true;
        }
        switch (key) {
        case 'j':
        case 'J':
            fieldStack_.smoothInPlane();
            return true;
        case 'k':
        case 'K':
            fieldStack_.applyStackLaplacian(5);
            return true;
        default:
            return false;
        }
    }

private:
    void setStatus(const std::string& message) {
        statusMessage_ = message;
    }

private:
    WaveLatentNavigator navigator_;
    FieldStack fieldStack_;
    std::unique_ptr<SimpleUI> ui_;

    std::string statusMessage_ = "Idle";
    std::string modelPath_ = "WaveLatentModel.json";
    std::string exportMeshName_ = "waveStackMesh.obj";
    std::string exportContoursName_ = "waveStackContours.obj";
    std::string exportStackFieldsName_ = "waveStackFields.json";

    float totalHeight_ = 200.0f;
    float isoLevel_ = 0.0f;
    float genLayers_ = 60.0f;
    float latentSnapRadius_ = 0.02f;
    bool contoursVisible_ = true;
    bool ready_ = false;
};

ALICE2_REGISTER_SKETCH_AUTO(Sketch_WaveLatent_Stack)

#endif // __MAIN__

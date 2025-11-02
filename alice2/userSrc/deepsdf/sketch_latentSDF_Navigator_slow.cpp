//#define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>

#include <ML/DeepSDF/LatentSDF_CPU.h>
#include <ML/DeepSDF/FieldViewer.h>

using namespace alice2;
using DeepSDF::LatentSDF_CPU;

class Sketch_LatentNavigator_Fast : public ISketch {
public:
    std::string getName() const override { return "Latent Navigator (Cached)"; }
    std::string getDescription() const override { return "400x400 panel (cached) + on-hover detail (cached)"; }
    std::string getAuthor() const override { return "alice2 User"; }

    void setup() override {
        scene().setBackgroundColor(Color(0.0f, 0.0f, 0.0f));
        scene().setShowGrid(false);
        scene().setShowAxes(false);

        // 1) Load model (decoder + latents + PE + domain)
        if (!trainer.loadModelJSON(snapshotPath)) {
            trainer.initialize(128, 128, -1.2f, 1.2f, -1.2f, 1.2f,
                               /*numShapes*/4, /*latentDim*/16, /*hidden*/{64,64,64}, /*seed*/1234);
        }

        // 2) Bind viewer
        viewer
            .setDomain(&trainer.getDomain())
            .setOriginal(&trainer.getOriginal())
            .setLatents(&trainer.getLatentCodes())
            .setDecoder([this](const std::vector<float>& z, DeepSDF::GridField& out){
                return trainer.getFieldFromLatent(z, out);   // full decode
            });

        // Fast hooks for panel bitmap (direct encode+forward)
        viewer.setFastDecodeHooks(
            // enc(x,y) -> coordEncDim floats
            [this](float x, float y, float* out){
                trainer.ad.enc.encode(x, y, trainer.ad.encBuf);
                for (int j=0;j<trainer.ad.coordEncDim;++j) out[j] = trainer.ad.encBuf[j];
            },
            // single forward
            [this](const std::vector<float>& in){
                return trainer.ad.decoder.forward(in);
            },
            trainer.ad.coordEncDim
        );

        // 3) Choose 4 corners from first shapes
        const auto& Z = trainer.getLatentCodes();
        const std::vector<float>* z00 = Z.size() > 0 ? &Z[0] : nullptr;
        const std::vector<float>* z10 = Z.size() > 1 ? &Z[1] : z00;
        const std::vector<float>* z01 = Z.size() > 2 ? &Z[2] : z00;
        const std::vector<float>* z11 = Z.size() > 3 ? &Z[3] : z10;
        viewer.setInterpCorners(z00, z10, z01, z11);

        // Detail preview res (speed/quality knob)
        viewer.setDetailPreviewRes(192, 192);

        // Start detail at center
        viewer.requestDetail(0.5f, 0.5f);
    }

    void update(float) override { /* no-op */ }

    void draw(Renderer& r, Camera&) override {
        r.setColor(Color(0.9f, 0.9f, 0.9f));
        r.drawString("Latent Navigator (cached) — hover over panel; detail updates below.  S:save  L:load  M:[ ]:tau  0/1 modes",
                     20.f, 16.f);

        // 400x400 cached panel (N fixed; tweak if you like)
        viewer.drawPanel(r, N);

        // draw a small crosshair at last hover
        if (hoverUV_) {
            const float u = hoverUV_->first, v = hoverUV_->second;
            const float x = DeepSDF::FieldViewer::kPanelLeft + u * DeepSDF::FieldViewer::kPanelSize;
            const float y = DeepSDF::FieldViewer::kPanelTop  + v * DeepSDF::FieldViewer::kPanelSize;
            r.setColor(Color(1,0.25f,0.25f));
            r.draw2dLine(Vec2(x-6,y), Vec2(x+6,y));
            r.draw2dLine(Vec2(x,y-6), Vec2(x,y+6));
        }

        // Cached detail bitmap below the panel
        const float detailLeft = 10.0f;
        const float detailTop  = DeepSDF::FieldViewer::kPanelTop + DeepSDF::FieldViewer::kPanelSize + 20.0f;
        // viewer.drawDetail(r, detailLeft, detailTop, /*size*/ 420.0f);

        viewer.drawHelp(r, detailTop + 430.0f);
    }

    bool onMouseMove(int x, int y) override {
        auto uv = viewer.panelUVFromMouse((float)x, (float)y, N);
        if (uv) {
            hoverUV_ = *uv;
            viewer.requestDetail(hoverUV_->first, hoverUV_->second);
            return true;
        }
        return false;
    }

    bool onKeyPress(unsigned char k, int, int) override {
        if (k=='M' || k=='m') { viewer.config().softMask = !viewer.config().softMask; viewer.requestDetailForLast(); return true; }
        if (k=='[') { viewer.config().tau = std::max(0.005f, viewer.config().tau*0.8f); viewer.requestDetailForLast(); return true; }
        if (k==']') { viewer.config().tau = std::min(0.5f,   viewer.config().tau*1.25f); viewer.requestDetailForLast(); return true; }
        if (k=='0') { viewer.config().debugMode = 0; viewer.requestDetailForLast(); return true; }
        if (k=='1') { viewer.config().debugMode = 2; viewer.requestDetailForLast(); return true; } // heat

        if (k=='S') { trainer.saveModelJSON(snapshotPath); return true; }
        if (k=='L' || k=='l') {
            if (trainer.loadModelJSON(snapshotPath)) {
                // rebind + reset corners
                viewer
                    .setDomain(&trainer.getDomain())
                    .setOriginal(&trainer.getOriginal())
                    .setLatents(&trainer.getLatentCodes())
                    .setDecoder([this](const std::vector<float>& z, DeepSDF::GridField& out){
                        return trainer.getFieldFromLatent(z, out);
                    })
                    .setFastDecodeHooks(
                        [this](float x, float y, float* out){
                            trainer.ad.enc.encode(x, y, trainer.ad.encBuf);
                            for (int j=0;j<trainer.ad.coordEncDim;++j) out[j] = trainer.ad.encBuf[j];
                        },
                        [this](const std::vector<float>& in){
                            return trainer.ad.decoder.forward(in);
                        },
                        trainer.ad.coordEncDim
                    );
                const auto& Z = trainer.getLatentCodes();
                const std::vector<float>* z00 = Z.size() > 0 ? &Z[0] : nullptr;
                const std::vector<float>* z10 = Z.size() > 1 ? &Z[1] : z00;
                const std::vector<float>* z01 = Z.size() > 2 ? &Z[2] : z00;
                const std::vector<float>* z11 = Z.size() > 3 ? &Z[3] : z10;
                viewer.setInterpCorners(z00, z10, z01, z11);
                // force refresh of both caches
                viewer.requestDetail(hoverUV_ ? hoverUV_->first : 0.5f,
                                     hoverUV_ ? hoverUV_->second: 0.5f, 0.0f);
            }
            return true;
        }
        return false;
    }

private:
    // Small helper to re-request last detail after config changes
    // (added by extending FieldViewer; here we implement inline to keep things simple)
    struct RequestLastDetail {
        Sketch_LatentNavigator_Fast* self;
        void operator()() const {
            if (self->hoverUV_)
                self->viewer.requestDetail(self->hoverUV_->first, self->hoverUV_->second, 0.0f);
            else
                self->viewer.requestDetail(0.5f, 0.5f, 0.0f);
        }
    } requestDetailForLast{this};

private:
    int N = 5; // panel grid

    std::string snapshotPath = "latent_model.json";

    DeepSDF::LatentSDF_CPU trainer;
    DeepSDF::FieldViewer   viewer;

    std::optional<std::pair<float,float>> hoverUV_;
};

ALICE2_REGISTER_SKETCH_AUTO(Sketch_LatentNavigator_Fast)

#endif // __MAIN__

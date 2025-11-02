//#define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>
#include <ML/DeepSDF/LatentSDF_CPU.h>
#include <ML/DeepSDF/FieldViewer.h>

using namespace alice2;
using DeepSDF::LatentSDF_CPU;

class Sketch_LatentSDF_CPU : public ISketch {
public:
    std::string getName() const override { return "LatentSDF (CPU)"; }
    std::string getDescription() const override { return "Tiny auto-decoder training with visualisation."; }
    std::string getAuthor() const override { return "alice2 User"; }

    void setup() override {
        scene().setBackgroundColor(Color(0.0f, 0.0f, 0.0f));
        scene().setShowGrid(false);
        scene().setShowAxes(false);

        trainer.initialize(gridResX, gridResY,
                       xMin, xMax, yMin, yMax,
                       /*numShapes*/3, latentDim,
                       /*hidden*/{64,64,64}, /*seed*/1234);

        viewer
        .setDomain(&trainer.getDomain())
        .setOriginal(&trainer.getOriginal())
        .setReconstructed(&trainer.getReconstructed());

        std::ifstream f(datasetPath);
        if (f.good()) {
            DeepSDF::TrainingDataset ds;
            if (ds.loadJSON(datasetPath)) {
                trainer.setTrainingDataset(ds);
                std::printf("[Dataset] Loaded existing '%s' (%zu samples)\n",
                            datasetPath.c_str(), ds.size());
            } else {
                std::printf("[Dataset] Found '%s' but failed to parse - using default.\n",
                            datasetPath.c_str());
                DeepSDF::TrainingDataset def; 
                def.generateDefault(/*withTinySeed*/true);
                trainer.setTrainingDataset(def);
            }
        } else {
            DeepSDF::TrainingDataset def; 
            def.generateDefault(/*withTinySeed*/true);
            trainer.setTrainingDataset(def);
            std::printf("[Dataset] No existing JSON found - generated default dataset.\n");
        }
    }

    void update(float) override { /* no-op */ }

    void draw(alice2::Renderer& r, Camera&) override {
        const float startY = 20.0f, gapY = 40.0f;
        const float tile   = viewer.config().tileSize;
        viewer.drawOriginalRow(r, startY);
        viewer.drawReconstructionRow(r, startY + tile + gapY);
        viewer.drawHelp(r, startY + 2*tile + gapY + 40.0f);
    }

    bool onKeyPress(unsigned char k, int, int) {
        if (k=='m' || k=='M') { viewer.config().softMask = !viewer.config().softMask; return true; }
        if (k=='1') { viewer.config().debugMode = 0; return true; }
        if (k=='2') { viewer.config().debugMode = 1; return true; }
        if (k=='3') { viewer.config().debugMode = 2; return true; }
        if (k=='4') { viewer.config().debugMode = 3; return true; }
        if (k=='[') { viewer.config().tau = std::max(0.005f, viewer.config().tau*0.8f); return true; }
        if (k==']') { viewer.config().tau = std::min(0.5f, viewer.config().tau*1.25f); return true; }
        if (k=='t' || k=='T') { trainer.trainBurst(trainEpochs, trainStepsPerEpoch, lrW, lrZ, dataSeed, sampSeed, /*record*/true); trainer.generateReconstruction(); return true; }
        if (k=='r' || k=='R') { trainer.generateReconstruction(); return true; }
        return false;
    }

private:
    // Parameters
    int   gridResX          = 128;
    int   gridResY          = 128;
    float xMin              = -1.2f, xMax = 1.2f;
    float yMin              = -1.2f, yMax = 1.2f;

    int   latentDim         = 16;

    int   trainEpochs       = 50;
    int   trainStepsPerEpoch= 10000;
    float lrW               = 5e-4f;
    float lrZ               = 1e-3f;
    unsigned dataSeed       = 2025;
    unsigned sampSeed       = 777;

    int numEpochs = 0;

    std::string datasetPath = "latentSDF_dataset.json"; // NEW: default save/load path

    DeepSDF::LatentSDF_CPU trainer;
    DeepSDF::FieldViewer  viewer;
    DeepSDF::FieldViewer::ViewConfig viewConfig;
};

ALICE2_REGISTER_SKETCH_AUTO(Sketch_LatentSDF_CPU)

#endif // __MAIN__

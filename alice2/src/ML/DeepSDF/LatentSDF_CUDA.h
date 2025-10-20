#pragma once
#include <vector>
#include <tuple>
#include <random>
#include <cmath>
#include <limits>
#include <algorithm>
#include <cstring>
#include <string>

struct uchar4;

#include "TrainingDataSet.h"
#include "FieldViewer.h"

namespace DeepSDF {

inline void buildAnalyticGrid(const std::vector<std::vector<float>>& shapes,
                              int shapeIdx,int resX,int resY,float xMin,float xMax,float yMin,float yMax,
                              std::vector<float>& out,float& vmin,float& vmax){
    out.resize(size_t(resX)*size_t(resY));
    vmin= std::numeric_limits<float>::max(); vmax=-vmin;
    float xStep=(resX>1)?(xMax-xMin)/float(resX-1):0.f, yStep=(resY>1)?(yMax-yMin)/float(resY-1):0.f;
    for(int y=0;y<resY;++y){ float yy=yMin+yStep*float(y);
        for(int x=0;x<resX;++x){ float xx=xMin+xStep*float(x);
            float v=clampSDF(evalShapeSDF(shapes, shapeIdx, xx, yy),0.1f);
            size_t i=size_t(y)*size_t(resX)+size_t(x);
            out[i]=v; vmin=std::min(vmin,v); vmax=std::max(vmax,v);
        }
    }
}

struct FieldRenderConfig {
    int   debugMode = 0;
    bool  softMask  = true;
    float tau       = 0.05f;
};

class LatentNavigator_CUDA;

// ---------- CUDA auto-decoder (opaque API; implemented in .cu) ----------
class TinyAutoDecoderCUDA {
public:
    TinyAutoDecoderCUDA();
    ~TinyAutoDecoderCUDA();

    void initialize(int numShapes, int latentDim,
                    const std::vector<int>& hidden,
                    unsigned seed, int maxBatch,
                    int numFreqs = 6, bool includeInput = true);

    // micro-batch training step (GPU): batch W update, per-sample Z update
    void trainMicroBatchGPU(int B, Sampler& sampler, std::mt19937& rng,
                            float lrW, float lrZ,
                            const std::vector<std::vector<float>>& shapes,
                            bool useRandom = false);

    // row forward for visualization
    void forwardRowGPU(int shapeIdx, const std::vector<float>& xs, float y,
                       std::vector<float>& outY) const;

    void decodeLatentGridToDevice(const float* latentHost,
                                  int resX, int resY,
                                  float xMin, float xMax,
                                  float yMin, float yMax,
                                  float* dstDevice,
                                  int dstStride,
                                  int dstOffsetX,
                                  int dstOffsetY,
                                  float* scratchLatentDevice);

    void panelToRGBA(const float* panelFieldDevice,
                     int panelWidth, int panelHeight,
                     int tileRes, int gap, int panelN,
                     const FieldRenderConfig& cfg,
                     float* tileMinDevice,
                     float* tileMaxDevice,
                     uchar4* rgbaDevice);

    // sync device latents to host (e.g., before visualization)
    void syncLatentsToHost();

    // pull running stats (no training sync unless you call this)
    // If reset=true, running loss & sample counters are zeroed after read.
    void syncStatsToHost(double& avgLoss, double& meanZ, bool reset = true);

    void saveModelJSON(const std::string& path,
                       const FieldDomain& domain) const;
    bool loadModelJSON(const std::string& path,
                       FieldDomain& outDomain,
                       int maxBatch = 256,
                       unsigned seed = 1234);

    // accessors / knobs
    const std::vector<std::vector<float>>& latents() const { return Z_; }
    std::vector<std::vector<float>>&       latents()       { return Z_; }
    int  latentDim()   const { return latentDim_; }
    int  coordEncDim() const { return coordEncDim_; }
    int  numShapes()   const { return numShapes_; }

    void setLambdaLatent(float v){ lambdaLatent_ = v; }
    void setWeightDecayW(float v){ weightDecayW_ = v; }
private:
    friend class LatentNavigator_CUDA;

    struct Impl; Impl* impl_ = nullptr;

    int   numShapes_    = 0;
    int   latentDim_    = 0;
    int   coordEncDim_  = 0;
    float lambdaLatent_ = 1e-4f;
    float weightDecayW_ = 1e-6f;
    std::vector<std::vector<float>> Z_; // host mirror of latents (used only when you request)
};

} //namescape DeepSDF

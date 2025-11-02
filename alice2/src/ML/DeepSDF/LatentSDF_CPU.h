#pragma once
// CPU latent SDF trainer (no drawing). Visualisation is in FieldViewer.h

#include <alice2.h>
#include <vector>
#include <tuple>
#include <cmath>
#include <cstdio>
#include <random>
#include <limits>

#include "TrainingDataSet.h"
#include "FieldViewer.h"   // brings GridField + FieldDomain

namespace DeepSDF {

// ---------- Minimal MLP + AutoDecoder ----------
struct TinyMLP {
    int inDim=0, outDim=1;
    std::vector<int> hidden;
    std::vector<int> layerIn, layerOut;
    std::vector<std::vector<float>> W, b, gW, gb, pre, gPre, act, gAct;
    bool linearOutput = true;

    void initialize(int inputDim, const std::vector<int>& hiddenLayers, int outputDim=1, unsigned seed=42) {
        inDim = inputDim; hidden = hiddenLayers; outDim = outputDim;
        const int L = (int)hidden.size() + 1;
        layerIn.resize(L); layerOut.resize(L);
        int prev = inDim;
        for (int l=0; l<L; ++l) {
            int width = (l < (int)hidden.size()) ? hidden[l] : outDim;
            layerIn[l] = prev; layerOut[l] = width; prev = width;
        }
        W.resize(L); b.resize(L); gW.resize(L); gb.resize(L);
        pre.resize(L); gPre.resize(L);
        act.resize(L+1); gAct.resize(L+1);

        std::mt19937 rng(seed); std::normal_distribution<float> N(0.f,1.f);
        for (int l=0; l<L; ++l) {
            int rows = layerOut[l], cols = layerIn[l];
            W[l].resize(rows*cols);
            b[l].assign(rows, 0.f);
            gW[l].assign(rows*cols, 0.f);
            gb[l].assign(rows, 0.f);
            pre[l].assign(rows, 0.f);
            gPre[l].assign(rows, 0.f);

            float scale = (l == L-1) ? (0.5f/std::sqrt((float)cols)) : (1.0f/std::sqrt((float)cols));
            for (int i=0;i<rows*cols;i++) W[l][i] = scale * N(rng);
        }
        act[0].assign(inDim, 0.f);
        gAct[0].assign(inDim, 0.f);
        act[L].assign(outDim, 0.f);
        gAct[L].assign(outDim, 0.f);
    }

    float forward(const std::vector<float>& x) {
        act[0] = x;
        const int L = (int)layerIn.size();
        for (int l=0; l<L; ++l) {
            int rows = layerOut[l], cols = layerIn[l];
            auto& out = act[l+1]; auto& z = pre[l];
            z.assign(rows, 0.f); out.assign(rows, 0.f);
            const auto& Wl = W[l]; const auto& bl = b[l];
            for (int i=0;i<rows;i++){
                float s = bl[i];
                const float* wrow = &Wl[i*cols];
                for (int j=0;j<cols;j++) s += wrow[j]*act[l][j];
                z[i]=s;
            }
            if (l == L-1 && linearOutput) {
                for (int i=0;i<rows;i++) out[i] = z[i];
            } else {
                for (int i=0;i<rows;i++) out[i] = std::tanh(z[i]);
            }
        }
        return act[L][0];
    }

    std::vector<float> backward_and_accumulate(float target, float weightDecay=0.f) {
        const int L = (int)layerIn.size();
        for (int l=0;l<L;l++){
            std::fill(gW[l].begin(), gW[l].end(), 0.f);
            std::fill(gb[l].begin(), gb[l].end(), 0.f);
            std::fill(gPre[l].begin(), gPre[l].end(), 0.f);
        }
        for (int l=0;l<=L;l++){
            if ((int)gAct[l].size()!=(int)act[l].size()) gAct[l].assign(act[l].size(),0.f);
            else std::fill(gAct[l].begin(), gAct[l].end(), 0.f);
        }

        const float y = act[L][0];
        gAct[L][0] = (y - target);

        for (int l=L-1; l>=0; --l){
            int rows = layerOut[l], cols = layerIn[l];
            if (l == L-1 && linearOutput) {
                for (int i=0;i<rows;i++) gPre[l][i] = gAct[l+1][i];
            } else {
                for (int i=0;i<rows;i++){
                    float a = act[l+1][i];
                    gPre[l][i] = gAct[l+1][i]*(1.f - a*a);
                }
            }
            for (int i=0;i<rows;i++){
                gb[l][i] += gPre[l][i];
                float* gWrow = &gW[l][i*cols];
                for (int j=0;j<cols;j++) gWrow[j] += gPre[l][i]*act[l][j];
            }
            for (int j=0;j<cols;j++){
                float acc=0.f;
                for (int i=0;i<rows;i++) acc += W[l][i*cols+j]*gPre[l][i];
                gAct[l][j] += acc;
            }
            if (weightDecay > 0.f) {
                float* gWptr = gW[l].data();
                const float* Wptr = W[l].data();
                const int n = rows*cols;
                for (int k=0;k<n;k++) gWptr[k] += weightDecay * Wptr[k];
            }
        }
        return gAct[0];
    }

    void sgd(float lr) {
        const int L=(int)layerIn.size();
        for (int l=0;l<L;l++){
            int rows=layerOut[l], cols=layerIn[l];
            for (int i=0;i<rows;i++){
                b[l][i] -= lr*gb[l][i];
                float* Wrow = &W[l][i*cols];
                const float* gWrow=&gW[l][i*cols];
                for (int j=0;j<cols;j++) Wrow[j] -= lr*gWrow[j];
            }
        }
    }
};

struct PosEnc2D {
    int   numFreqs = 6;
    bool  includeInput = true;
    float twoPi = 6.283185307179586f;

    int encodedDim() const {
        int base = includeInput ? 2 : 0;
        return base + 2 * 2 * numFreqs;
    }
    void encode(float x, float y, std::vector<float>& out) const {
        out.clear();
        out.reserve(encodedDim());
        if (includeInput) { out.push_back(x); out.push_back(y); }
        float freq = 1.0f;
        for (int k=0;k<numFreqs;k++){
            float ax = freq * twoPi * x;
            float ay = freq * twoPi * y;
            out.push_back(std::sinf(ax));
            out.push_back(std::cosf(ax));
            out.push_back(std::sinf(ay));
            out.push_back(std::cosf(ay));
            freq *= 2.0f;
        }
    }
};

struct TinyAutoDecoder {
    int numShapes=0, latentDim=16, coordDim=2;
    float lambdaLatent = 1e-4f;
    float weightDecayW = 1e-6f;
    TinyMLP decoder;
    std::vector<std::vector<float>> Z;

    PosEnc2D enc;
    int coordEncDim = 0;
    mutable std::vector<float> encBuf;

    void initialize(int nShapes, int zDim, const std::vector<int>& hidden, unsigned seed=1234){
        numShapes=nShapes; latentDim=zDim;
        enc.numFreqs = 6; enc.includeInput = true;
        coordEncDim = enc.encodedDim();

        decoder.linearOutput = true;
        decoder.initialize(latentDim + coordEncDim, hidden, 1, seed);

        Z.assign(numShapes, std::vector<float>(latentDim, 0.f));
        std::mt19937 rng(seed); std::normal_distribution<float> N(0.f,0.01f);
        for (int i=0;i<numShapes;i++) for (int j=0;j<latentDim;j++) Z[i][j]=N(rng);
        encBuf.reserve(coordEncDim);
    }

    std::vector<float> buildInput(int shapeIdx, float x, float y) const {
        enc.encode(x, y, encBuf);
        std::vector<float> in(latentDim + coordEncDim);
        for (int j=0;j<latentDim;j++) in[j]=Z[shapeIdx][j];
        for (int j=0;j<coordEncDim;j++) in[latentDim + j] = encBuf[j];
        return in;
    }

    std::tuple<float,float,float> trainSample(int shapeIdx, float x, float y, float target, float lrW, float lrZ){
        auto in = buildInput(shapeIdx, x, y);
        float ypred = decoder.forward(in);

        float diff=ypred-target;
        float mse=0.5f*diff*diff;

        float l2z=0.f; for (int j=0;j<latentDim;j++) l2z += Z[shapeIdx][j]*Z[shapeIdx][j];
        float loss = mse + 0.5f*lambdaLatent*l2z;

        auto dL_din = decoder.backward_and_accumulate(target, /*weightDecayW*/weightDecayW);
        for (int j=0;j<latentDim;j++) dL_din[j] += lambdaLatent*Z[shapeIdx][j];

        decoder.sgd(lrW);
        for (int j=0;j<latentDim;j++) Z[shapeIdx][j] -= lrZ*dL_din[j];

        return {loss, ypred, target};
    }
};

// ---------- Build analytic label field ----------
inline void buildAnalyticGrid(const std::vector<std::vector<float>>& shapes,
                              int shapeIdx,
                              int resX, int resY,
                              float xMin, float xMax, float yMin, float yMax,
                              std::vector<float>& out, float& vmin, float& vmax)
{
    out.resize(size_t(resX)*size_t(resY));
    vmin =  std::numeric_limits<float>::max();
    vmax = -std::numeric_limits<float>::max();

    const float xStep = (resX > 1) ? (xMax - xMin) / float(resX - 1) : 0.f;
    const float yStep = (resY > 1) ? (yMax - yMin) / float(resY - 1) : 0.f;

    for (int y = 0; y < resY; ++y) {
        const float yy = yMin + yStep * float(y);
        for (int x = 0; x < resX; ++x) {
            const float xx = xMin + xStep * float(x);
            const float d  = evalShapeSDF(shapes, shapeIdx, xx, yy);
            const float v  = labelFromSDF(d, 0.02f); // -1 / 0 / +1
            const size_t idx = size_t(y) * size_t(resX) + size_t(x);
            out[idx] = v;
            vmin = std::min(vmin, v);
            vmax = std::max(vmax, v);
        }
    }
}

// ---------- Trainer façade (no drawing) ----------
class LatentSDF_CPU {
public:
    FieldDomain domain;
    std::vector<GridField> original;
    std::vector<GridField> reconstructed;
    TrainingDataset dataset;
    TinyAutoDecoder ad;

    // Dataset accessors
    TrainingDataset& getTrainingDataset() { return dataset; }
    const TrainingDataset& getTrainingDataset() const { return dataset; }
    void setTrainingDataset(const TrainingDataset& ds) { dataset = ds; }
    void swapTrainingDataset(TrainingDataset& ds) { std::swap(dataset, ds); }

    // Field getters for viewer binding
    const FieldDomain&            getDomain()        const { return domain; }
    const std::vector<GridField>& getOriginal()      const { return original; }
    const std::vector<GridField>& getReconstructed() const { return reconstructed; }
    const std::vector<std::vector<float>>& getLatentCodes()const { return ad.Z; }

    void initialize(int gridX, int gridY,
                    float xMin, float xMax, float yMin, float yMax,
                    int numShapes, int latentDim,
                    const std::vector<int>& hidden = {64,64,64},
                    unsigned seed = 1234)
    {
        domain.resX = gridX; domain.resY = gridY;
        domain.xMin = xMin;  domain.xMax = xMax;
        domain.yMin = yMin;  domain.yMax = yMax;

        dataset.ensureShapes();
        original.assign(numShapes, GridField{});
        for (int s = 0; s < numShapes; ++s) {
            buildAnalyticGrid(dataset.shapes, s, domain.resX, domain.resY,
                              domain.xMin, domain.xMax, domain.yMin, domain.yMax,
                              original[s].values, original[s].minValue, original[s].maxValue);
        }

        ad.initialize(numShapes, latentDim, hidden, seed);
        ad.lambdaLatent = 1e-4f;

        trainBurst(/*epochs*/1, /*stepsPerEpoch*/5000);
        generateReconstruction();
    }

    void trainBurst(int epochs, int stepsPerEpoch,
                    float lrW = 5e-4f, float lrZ = 1e-3f,
                    unsigned dataSeed = 2025, unsigned sampSeed = 777,
                    bool recordDataset = true)
    {
        dataset.ensureShapes();
        Sampler samp(sampSeed);
        samp.setShapes(&dataset.shapes);
        std::uniform_int_distribution<int> pick(0, ad.numShapes - 1);
        std::mt19937 rng(dataSeed);

        if (recordDataset) dataset.reserve(dataset.size() + size_t(epochs) * size_t(stepsPerEpoch));

        for (int e = 1; e <= epochs; ++e) {
            double runLoss = 0.0;
            for (int s = 0; s < stepsPerEpoch; ++s) {
                int idx = pick(rng);
                auto [xx, yy, tt] = samp.sampleForShape(idx);
                auto [L, ypred, tgt] = ad.trainSample(idx, xx, yy, tt, lrW, lrZ);
                runLoss += (double)L;
                if (recordDataset) dataset.add(idx, xx, yy, tgt, ypred);
            }
            double avgLoss = runLoss / (double)stepsPerEpoch;
            double meanZ = 0.0;
            for (int i = 0; i < ad.numShapes; ++i) {
                double nz = 0.0;
                for (int j = 0; j < ad.latentDim; ++j) nz += (double)ad.Z[i][j] * ad.Z[i][j];
                meanZ += std::sqrt(nz + 1e-12);
            }
            meanZ /= std::max(1, ad.numShapes);
            std::printf("[TrainBurst] avgLoss=%.6f  mean||z||=%.6f\n", (float)avgLoss, (float)meanZ);
        }
    }

    bool decodeGrid(const std::vector<float>& z, GridField& out)
    {
        if ((int)z.size() != ad.latentDim) return false;

        const int resX = domain.resX;
        const int resY = domain.resY;
        out.values.resize(size_t(resX) * size_t(resY));

        float vmin =  std::numeric_limits<float>::max();
        float vmax = -std::numeric_limits<float>::max();

        std::vector<float> in(size_t(ad.latentDim + ad.coordEncDim), 0.f);

        const float xStep = (resX > 1) ? (domain.xMax - domain.xMin) / float(resX - 1) : 0.0f;
        const float yStep = (resY > 1) ? (domain.yMax - domain.yMin) / float(resY - 1) : 0.0f;

        for (int y = 0; y < resY; ++y)
        {
            const float yy = domain.yMin + yStep * float(y);
            for (int x = 0; x < resX; ++x)
            {
                const float xx = domain.xMin + xStep * float(x);
                const size_t idx = size_t(y) * size_t(resX) + size_t(x);

                // Build [z ; encoded(x,y)]
                std::copy(z.begin(), z.end(), in.begin());
                ad.enc.encode(xx, yy, ad.encBuf);
                for (int j = 0; j < ad.coordEncDim; ++j)
                    in[ad.latentDim + j] = ad.encBuf[j];

                const float v = ad.decoder.forward(in);
                out.values[idx] = v;
                vmin = std::min(vmin, v);
                vmax = std::max(vmax, v);
            }
        }

        out.minValue = vmin;
        out.maxValue = vmax;
        return true;
    }

    void generateReconstruction() {
        if (ad.latentDim <= 0 || ad.coordDim != 2) return;
        if (domain.resX <= 0 || domain.resY <= 0) return;

        reconstructed.assign(ad.numShapes, GridField{});

        const float xStep = (domain.resX > 1) ? (domain.xMax - domain.xMin) / float(domain.resX - 1) : 0.0f;
        const float yStep = (domain.resY > 1) ? (domain.yMax - domain.yMin) / float(domain.resY - 1) : 0.0f;

        std::vector<float> in(size_t(ad.latentDim + ad.coordEncDim), 0.f);
        for (int shape = 0; shape < ad.numShapes; ++shape) {
            const auto& latent = ad.Z[shape];
            GridField& grid = reconstructed[shape];
            grid.values.resize(size_t(domain.resX) * size_t(domain.resY));
            grid.minValue =  std::numeric_limits<float>::max();
            grid.maxValue = -std::numeric_limits<float>::max();

            for (int y = 0; y < domain.resY; ++y) {
                const float yy = domain.yMin + yStep * float(y);
                for (int x = 0; x < domain.resX; ++x) {
                    const float xx = domain.xMin + xStep * float(x);
                    const size_t idx = size_t(y) * size_t(domain.resX) + size_t(x);

                    std::copy(latent.begin(), latent.end(), in.begin());
                    ad.enc.encode(xx, yy, ad.encBuf);
                    for (int j=0;j<ad.coordEncDim;j++) in[ad.latentDim + j] = ad.encBuf[j];

                    const float v = ad.decoder.forward(in);
                    grid.values[idx] = v;
                    grid.minValue = std::min(grid.minValue, v);
                    grid.maxValue = std::max(grid.maxValue, v);
                }
            }
            if (grid.minValue ==  std::numeric_limits<float>::max()) grid.minValue = 0.f;
            if (grid.maxValue == -std::numeric_limits<float>::max()) grid.maxValue = 0.f;
        }
    }
};

} // namespace DeepSDF

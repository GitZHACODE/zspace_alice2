// ===============================
// alice2 Micro-Batch GEMM Auto-Decoder (single file)
// - CPU-only, dependency-free
// - Batch forward via GEMM
// - Per-sample backward + rank-1 updates (preserves per-sample latent updates)
// - Visual: heatmap/mask toggle with crisp edges
// ===============================

//#define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>

using namespace alice2;

#pragma once
#include <vector>
#include <tuple>
#include <random>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <algorithm>
#include <limits>
#include <memory>

// ----------------- Utility -----------------
inline Color valueToGray(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return Color(t, t, t);
}

struct GridField {
    std::vector<float> values;
    float minValue = 0.0f;
    float maxValue = 0.0f;
};

struct FieldDomain {
    int   resX = 128, resY = 128;
    float xMin = -1.2f, xMax = 1.2f;
    float yMin = -1.2f, yMax = 1.2f;
};

// ---------- Analytic SDFs ----------
inline float sdCircle(float x, float y, float r = 0.6f) {
    return std::sqrt(x*x + y*y) - r;
}
inline float sdBox(float x, float y, float hx = 0.55f, float hy = 0.55f) {
    float ax = std::fabs(x) - hx;
    float ay = std::fabs(y) - hy;
    float ox = std::max(ax, 0.0f);
    float oy = std::max(ay, 0.0f);
    float outside = std::sqrt(ox*ox + oy*oy);
    float inside  = std::max(ax, ay); // negative inside
    return (inside <= 0.0f) ? inside : outside;
}
inline float sdTriangleUp(float x, float y, float s = 1.1f) {
    const float k = std::sqrt(3.0f);
    x = std::fabs(x);
    float d1 = (k*x + y) - s;
    float d2 = (k*x - y) - s;
    float d3 = -y - s * 0.3f;
    float outside = std::max(std::max(d1, d2), d3);
    if (outside > 0.0f) return outside;
    float de1 = (k*x + y) - s;
    float de2 = (k*x - y) - s;
    float de3 = -y - s * 0.3f;
    float m = std::max(std::max(de1, de2), de3);
    return m;
}

// Soft clamp SDF to [-1,1] by scale beta
inline float clampSDF(float d, float beta = 0.1f) {
    float v = d / beta;
    if (v < -1.f) v = -1.f;
    if (v >  1.f) v =  1.f;
    return v;
}

// ---------- Sampler with boundary & corner bias ----------
struct Sampler {
    float range = 1.2f;
    float boundaryFrac = 0.6f;
    float boundaryBand = 0.02f;
    float cornerFrac   = 0.15f;

    std::mt19937 rng;
    std::uniform_real_distribution<float> U;

    Sampler(unsigned seed = 999) : rng(seed), U(-1.f, 1.f) {}

    static float sdf(int shapeIdx, float x, float y) {
        switch (shapeIdx) {
            case 0: return sdCircle(x, y, 0.6f);
            case 1: return sdBox(x, y, 0.55f, 0.55f);
            case 2: return sdTriangleUp(x, y, 1.1f);
            default: return 1e9f;
        }
    }

    std::tuple<float,float,float> sampleForShape(int shapeIdx) {
        std::uniform_real_distribution<float> Udom(-range, range);
        std::uniform_real_distribution<float> U01(0.f, 1.f);

        auto emit = [&](float X, float Y){
            float d = sdf(shapeIdx, X, Y);
            return std::tuple<float,float,float>{X, Y, clampSDF(d)};
        };

        float r = U01(rng);
        if (r < cornerFrac) {
            for (int tries=0; tries<200; ++tries){
                float x = Udom(rng)*0.8f;
                float y = Udom(rng)*0.8f;
                float d = sdf(shapeIdx, x, y);
                if (std::fabs(d) < boundaryBand*1.5f && (std::fabs(x)+std::fabs(y) > 0.6f))
                    return emit(x,y);
            }
        }
        if (r < cornerFrac + boundaryFrac) {
            for (int tries = 0; tries < 200; ++tries) {
                float x = Udom(rng), y = Udom(rng);
                float d = sdf(shapeIdx, x, y);
                if (std::fabs(d) < boundaryBand) return emit(x,y);
            }
        }
        float x = Udom(rng), y = Udom(rng);
        return emit(x,y);
    }
};

// ---------- Fourier Positional Encoding ----------
struct PosEnc2D {
    int   numFreqs = 6;     // try 4–8
    bool  includeInput = true;
    float twoPi = 6.283185307179586f;

    int encodedDim() const {
        int base = includeInput ? 2 : 0;
        return base + 2 /*sin,cos*/ * 2 /*x,y*/ * numFreqs;
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

// ---------- tiny GEMM helpers (row-major) ----------
// C[m x n] = A[m x k] * B[k x n]  (no bias; beta=0)
inline void gemm(float* C, const float* A, const float* B,
                 int m, int n, int k)
{
    // naive O(mnk) — small sizes so OK; later swap to BLAS/CUDA
    for (int i=0;i<m;i++){
        float* cRow = C + i*n;
        for (int j=0;j<n;j++) cRow[j] = 0.f;
        for (int p=0;p<k;p++){
            const float a = A[i*k + p];
            const float* bRow = B + p*n;
            for (int j=0;j<n;j++) cRow[j] += a * bRow[j];
        }
    }
}

// y = tanh(x) elementwise on matrix [m x n]
inline void tanh_inplace(float* X, int m, int n)
{
    const int N = m*n;
    for (int i=0;i<N;i++) X[i] = std::tanh(X[i]);
}

// add bias: for each row i in [m], add b[i] to all n columns
inline void add_bias_rows(float* X, const float* b, int m, int n)
{
    for (int i=0;i<m;i++){
        float* row = X + i*n;
        float bi = b[i];
        for (int j=0;j<n;j++) row[j] += bi;
    }
}

// column view helper: returns pointer to col j of matrix [m x n]
inline void col_copy(float* dst, const float* M, int m, int n, int j)
{
    for (int i=0;i<m;i++) dst[i] = M[i*n + j];
}
inline void col_axpy(float* dst, const float* src, float alpha, int m)
{
    for (int i=0;i<m;i++) dst[i] += alpha * src[i];
}

// ---------- Minimal MLP (batch forward via GEMM; per-sample backprop updates) ----------
struct TinyMLP {
    int inDim=0, outDim=1;
    std::vector<int> hidden;
    std::vector<int> layerIn, layerOut;

    // params
    std::vector<std::vector<float>> W, b;

    // buffers for batch forward (kept to avoid re-allocs)
    std::vector<std::vector<float>> A; // activations per layer (including input)
    std::vector<std::vector<float>> Z; // pre-activations per hidden/output

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

        W.resize(L); b.resize(L);
        A.resize(L+1); Z.resize(L);

        std::mt19937 rng(seed); std::normal_distribution<float> N(0.f,1.f);
        for (int l=0; l<L; ++l) {
            int rows = layerOut[l], cols = layerIn[l];
            W[l].resize(rows*cols);
            b[l].assign(rows, 0.f);
            float scale = (l == L-1) ? (0.5f/std::sqrt((float)cols)) : (1.0f/std::sqrt((float)cols));
            for (int i=0;i<rows*cols;i++) W[l][i] = scale * N(rng);
        }
    }

    // Batch forward: inputs X is [inDim x B] (column-major in our row-major storage: we treat columns as samples)
    // We store A[0]=X, then for each layer: Z[l]=W[l]*A[l]+b, A[l+1]=tanh(Z) or identity for last layer if linearOutput
    void forwardBatch(const std::vector<float>& X, int B) {
        const int L = (int)layerIn.size();
        // set A0
        A[0] = X; // size inDim x B (row-major m=inDim, n=B)
        for (int l=0; l<L; ++l) {
            int m = layerOut[l], k = layerIn[l], n = B;
            Z[l].assign(m*n, 0.f);
            gemm(Z[l].data(), W[l].data(), A[l].data(), m, n, k);
            add_bias_rows(Z[l].data(), b[l].data(), m, n);
            // activation
            if (l == L-1 && linearOutput) {
                A[l+1] = Z[l]; // identity
            } else {
                A[l+1] = Z[l];
                tanh_inplace(A[l+1].data(), m, n);
            }
        }
    }

    // Per-sample backward + rank-1 SGD update:
    // Given cached A and Z for the current batch, apply update for sample column 'j'
    // target t, lrW, optional weight decay
    // Returns dL/du (input gradient, length = inDim)
    void backwardUpdateOneSample(int jCol, float t,
                                 float lrW, float weightDecay,
                                 std::vector<float>& dL_du_out)
    {
        const int L = (int)layerIn.size();
        // we’ll walk backwards, keeping a delta vector per layer output for this sample
        std::vector<std::vector<float>> delta(L); // size = layerOut[l] for each l

        // last layer delta: (y - t) * act'(z)
        {
            int m = layerOut[L-1];
            delta[L-1].assign(m, 0.f);
            // y = A[L][:,j]
            std::vector<float> y(m), z(m);
            col_copy(y.data(), A[L].data(), m, /*n=*/A[L].size()/m, jCol);
            col_copy(z.data(), Z[L-1].data(), m, /*n=*/Z[L-1].size()/m, jCol);
            float resid = y[0] - t; // scalar output
            if (linearOutput) {
                delta[L-1][0] = resid; // derivative 1.0
            } else {
                float a = std::tanh(z[0]); // (we never use this path in our config)
                delta[L-1][0] = resid * (1.f - a*a);
            }
        }

        // backprop & updates layer by layer
        for (int l=L-1; l>=0; --l) {
            int rows = layerOut[l];
            int cols = layerIn[l];

            // ---- rank-1 update for W[l], b[l] using sample j ----
            // dW = delta[l] * a_prev^T  (outer product), db = delta
            std::vector<float> aPrev(cols);
            col_copy(aPrev.data(), A[l].data(), cols, /*n=*/A[l].size()/cols, jCol);

            // weight decay on W
            if (weightDecay > 0.f) {
                float* Wl = W[l].data();
                const int nW = rows*cols;
                for (int k=0;k<nW;k++) Wl[k] -= lrW * weightDecay * Wl[k];
            }

            // SGD step (rank-1)
            for (int i=0;i<rows;i++){
                float* Wrow = &W[l][i*cols];
                float dl_i = delta[l][i];
                b[l][i] -= lrW * dl_i;
                for (int j=0;j<cols;j++){
                    Wrow[j] -= lrW * (dl_i * aPrev[j]);
                }
            }

            // ---- compute delta for previous layer (unless l==0) ----
            if (l > 0) {
                delta[l-1].assign(cols, 0.f);
                // delta_prev = (W[l]^T * delta[l]) ⊙ tanh'(Z[l-1][:,j])
                // first tmp = W^T * delta
                for (int j=0;j<cols;j++){
                    float acc = 0.f;
                    for (int i=0;i<rows;i++) acc += W[l][i*cols + j] * delta[l][i];
                    delta[l-1][j] = acc;
                }
                // multiply by derivative of tanh at pre-activation
                std::vector<float> zPrev(cols);
                col_copy(zPrev.data(), Z[l-1-0].data(), cols, /*n=*/Z[l-1-0].size()/cols, jCol);
                for (int j=0;j<cols;j++){
                    float a = std::tanh(zPrev[j]);
                    delta[l-1][j] *= (1.f - a*a);
                }
            }
        }

        // output dL/du = delta at layer 0 propagated to input (which equals delta[-1] already computed)
        dL_du_out = delta[0]; // length = inDim
    }
};

// ---------- Auto-decoder with micro-batch training ----------
struct TinyAutoDecoder {
    int numShapes=0, latentDim=16, coordEncDim=0;
    float lambdaLatent = 1e-4f;
    float weightDecayW = 1e-6f;

    PosEnc2D enc;
    TinyMLP decoder;

    std::vector<std::vector<float>> Z;     // [numShapes][latentDim]
    std::vector<float> encBuf;             // coord encode buffer (reused)

    void initialize(int nShapes, int zDim, const std::vector<int>& hidden, unsigned seed=1234){
        numShapes=nShapes; latentDim=zDim;

        enc.numFreqs = 6;
        enc.includeInput = true;
        coordEncDim = enc.encodedDim();

        decoder.linearOutput = true;
        decoder.initialize(latentDim + coordEncDim, hidden, 1, seed);

        Z.assign(numShapes, std::vector<float>(latentDim, 0.f));
        std::mt19937 rng(seed); std::normal_distribution<float> N(0.f,0.01f);
        for (int i=0;i<numShapes;i++) for (int j=0;j<latentDim;j++) Z[i][j]=N(rng);

        encBuf.reserve(coordEncDim);
    }

    // Build a concatenated [z|phi(x,y)] for ONE sample
    void buildInputSingle(int shapeIdx, float x, float y, std::vector<float>& out) {
        enc.encode(x, y, encBuf);
        out.resize(latentDim + coordEncDim);
        std::copy(Z[shapeIdx].begin(), Z[shapeIdx].end(), out.begin());
        std::copy(encBuf.begin(), encBuf.end(), out.begin()+latentDim);
    }

    // Micro-batch training step:
    // - samples B points across shapes
    // - forward batch via GEMM
    // - for j in 0..B-1: per-sample backward + rank-1 W,b update + latent update
    void trainMicroBatch(int B,
                         Sampler& samp,
                         std::mt19937& pickRng,
                         float lrW, float lrZ,
                         double& outAvgLoss, double& outMeanZ)
    {
        std::uniform_int_distribution<int> pickShape(0, numShapes-1);

        // 1) build batch matrices:
        const int inDim = latentDim + coordEncDim;
        std::vector<int>   shapeIdxs(B);
        std::vector<float> xs(B), ys(B), ts(B);

        // X is [inDim x B] row-major
        std::vector<float> X(inDim * B, 0.f);

        // Keep a copy of the latents used in this batch (to compute dL/dz against what forward saw)
        std::vector<std::vector<float>> Zsnap(B, std::vector<float>(latentDim, 0.f));

        for (int j=0;j<B;j++){
            int si = pickShape(pickRng);
            auto [x, y, t] = samp.sampleForShape(si);

            shapeIdxs[j] = si; xs[j]=x; ys[j]=y; ts[j]=t;

            // build input column j
            enc.encode(x, y, encBuf);
            for (int p=0;p<latentDim;p++) {
                X[p*B + j] = Z[si][p]; // row-major [inDim x B]
                Zsnap[j][p] = Z[si][p];
            }
            for (int p=0;p<coordEncDim;p++) {
                X[(latentDim + p)*B + j] = encBuf[p];
            }
        }

        // 2) forward batch
        decoder.forwardBatch(X, B);

        // 3) per-sample backward + updates (preserve per-sample latent update)
        double runLoss = 0.0;
        for (int j=0;j<B;j++){
            // predicted y from A[L][:,j]
            float ypred;
            {
                int m = decoder.layerOut.back(); // 1
                (void)m;
                std::vector<float> ycol(1);
                col_copy(ycol.data(), decoder.A.back().data(), 1, /*n=*/B, j);
                ypred = ycol[0];
            }
            float target = ts[j];
            float diff   = ypred - target;
            float mse    = 0.5f * diff * diff;

            // latent regularization
            int si = shapeIdxs[j];
            float l2z = 0.f;
            for (int p=0;p<latentDim;p++) l2z += Z[si][p]*Z[si][p];
            float loss = mse + 0.5f * lambdaLatent * l2z;
            runLoss += loss;

            // backprop for this column + W,b rank-1 update
            std::vector<float> dL_du;
            decoder.backwardUpdateOneSample(j, target, lrW, /*weightDecay*/weightDecayW, dL_du);

            // dL/dz = first latentDim entries + lambda*z
            for (int p=0;p<latentDim;p++) {
                float g = dL_du[p] + lambdaLatent * Z[si][p];
                Z[si][p] -= lrZ * g;
            }
        }

        // 4) stats
        outAvgLoss = runLoss / double(B);
        double meanZ = 0.0;
        for (int i=0;i<numShapes;i++){
            double nz = 0.0; for (int p=0;p<latentDim;p++) nz += (double)Z[i][p]*Z[i][p];
            meanZ += std::sqrt(nz + 1e-12);
        }
        outMeanZ = meanZ / double(numShapes);
    }
};

// ---------- Build analytic label field on a grid (for "ground truth" view) ----------
inline void buildAnalyticGrid(int shapeIdx,
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
            const float d  = Sampler::sdf(shapeIdx, xx, yy);
            const float v  = clampSDF(d, 0.1f); // show scaled SDF in [-1,1]
            const size_t idx = size_t(y) * size_t(resX) + size_t(x);
            out[idx] = v;
            vmin = std::min(vmin, v);
            vmax = std::max(vmax, v);
        }
    }
}

// ========================== SKETCH ==========================
class LatentSDFSketch_Batched : public ISketch {
public:
    LatentSDFSketch_Batched() = default;
    ~LatentSDFSketch_Batched() = default;

    std::string getName() const override { return "LatentSDFSketch_BatchedGEMM"; }
    std::string getDescription() const override { return "Original vs Reconstructed (micro-batch GEMM forward)"; }
    std::string getAuthor() const override { return "alice2 User"; }

    void setup() override {
        scene().setBackgroundColor(Color(0.0f, 0.0f, 0.0f));
        scene().setShowGrid(false);
        scene().setShowAxes(false);

        // Domain/grid
        m_domain.resX = (m_gridResolutionX > 0) ? m_gridResolutionX : 128;
        m_domain.resY = (m_gridResolutionY > 0) ? m_gridResolutionY : 128;
        m_domain.xMin = (m_xMin < m_xMax) ? m_xMin : -1.2f;
        m_domain.xMax = (m_xMin < m_xMax) ? m_xMax :  1.2f;
        m_domain.yMin = (m_yMin < m_yMax) ? m_yMin : -1.2f;
        m_domain.yMax = (m_yMin < m_yMax) ? m_yMax :  1.2f;

        // Mirror back for convenience
        m_gridResolutionX = m_domain.resX;
        m_gridResolutionY = m_domain.resY;
        m_xMin = m_domain.xMin; m_xMax = m_domain.xMax;
        m_yMin = m_domain.yMin; m_yMax = m_domain.yMax;

        // Build analytic originals (0 circle, 1 square, 2 triangle)
        m_originalGridFields.assign(3, GridField{});
        for (int s = 0; s < 3; ++s) {
            buildAnalyticGrid(s, m_domain.resX, m_domain.resY,
                              m_domain.xMin, m_domain.xMax, m_domain.yMin, m_domain.yMax,
                              m_originalGridFields[s].values,
                              m_originalGridFields[s].minValue,
                              m_originalGridFields[s].maxValue);
        }
        m_hasOriginal = true;

        // Init auto-decoder
        const std::vector<int> hidden = {64,64,64};
        m_ad.initialize(/*numShapes*/3, /*latentDim*/m_latentDim, hidden, /*seed*/1234);
        m_ad.lambdaLatent = 1e-4f;
        m_ad.weightDecayW = 1e-6f;

        // quick warmup
        trainBurst(/*epochs*/1, /*microBatchesPerEpoch*/100, /*B*/64);
        generateReconstruction();
    }

    void update(float) override { /* no-op */ }

    void draw(Renderer& renderer, Camera&) override {
        const float startY = 20.0f;
        const float gapY   = 40.0f;

        renderer.setColor(Color(0.9f, 0.9f, 0.9f));
        renderer.drawString("Original (analytic scaled SDF)", 20.0f, startY - 8.0f);
        drawFieldRow(renderer, m_originalGridFields, startY);

        const float reconTop = startY + m_tileSize + gapY;
        renderer.setColor(Color(0.9f, 0.9f, 0.9f));
        renderer.drawString(m_showMask ? (m_softMask? "Reconstructed (soft mask)":"Reconstructed (hard mask)")
                                       : "Reconstructed (continuous SDF)",
                            20.0f, reconTop - 8.0f);
        drawFieldRow(renderer, m_reconstructedGridFields, reconTop);

        renderer.setColor(Color(0.7f, 0.7f, 0.7f));
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "Hotkeys: [T] train x1   [B/b] ++/-- batch  [M] mask  [S] soft  [j/k] tau=%.3f   B=%d",
            m_tau, m_batchSize);
        renderer.drawString(buf, 20.0f, reconTop + m_tileSize + 40.0f);
    }

    bool onKeyPress(unsigned char key, int, int) override {
        switch (key) {
            case 't': case 'T':
                trainBurst(/*epochs*/50, /*microBatchesPerEpoch*/200, /*B*/m_batchSize);
                generateReconstruction();
                return true;
            case 'r': case 'R':
                generateReconstruction();
                return true;
            case 'b':
                m_batchSize = std::max(8, m_batchSize/2);
                return true;
            case 'B':
                m_batchSize = std::min(512, m_batchSize*2);
                return true;
            case 'm': case 'M':
                m_showMask = !m_showMask;
                return true;
            case 's': case 'S':
                m_softMask = !m_softMask;
                return true;
            case 'j':
                m_tau = std::max(0.005f, m_tau * 0.8f);
                return true;
            case 'k':
                m_tau = std::min(0.5f, m_tau * 1.25f);
                return true;
        }
        return false;
    }

private:
    // -------- Micro-batch training --------
    void trainBurst(int epochs, int microBatchesPerEpoch, int B) {
        Sampler samp(777);
        std::mt19937 rng(2025);

        const float lrW = 3e-4f;
        const float lrZ = 1e-3f;

        for (int e = 1; e <= epochs; ++e) {
            double epochLoss = 0.0;
            double meanZ = 0.0;
            for (int mb = 0; mb < microBatchesPerEpoch; ++mb) {
                double avgL = 0.0, mZ = 0.0;
                m_ad.trainMicroBatch(B, samp, rng, lrW, lrZ, avgL, mZ);
                epochLoss += avgL;
                meanZ += mZ;
            }
            epochLoss /= double(microBatchesPerEpoch);
            meanZ     /= double(microBatchesPerEpoch);
            std::printf("[TrainGEMM] epoch=%d  avgLoss=%.6f  mean||z||=%.6f  (B=%d)\n",
                        e, (float)epochLoss, (float)meanZ, B);
        }
    }

    // -------- build reconstructed grids from current decoder+latents --------
        void generateReconstruction() {
        if (m_latentDim <= 0) return;
        if (m_gridResolutionX <= 0 || m_gridResolutionY <= 0) return;

        const auto& latentCodes = m_ad.Z;
        if (latentCodes.empty()) return;

        m_reconstructedGridFields.assign(latentCodes.size(), GridField{});

        const int W = m_gridResolutionX;
        const int H = m_gridResolutionY;
        const float xStep = (W > 1) ? (m_xMax - m_xMin) / float(W - 1) : 0.0f;
        const float yStep = (H > 1) ? (m_yMax - m_yMin) / float(H - 1) : 0.0f;

        const int inDim = m_latentDim + m_ad.coordEncDim;
        std::vector<float> enc; enc.reserve(m_ad.coordEncDim);
        std::vector<float> X;  X.resize(size_t(inDim) * size_t(W)); // batch = whole row

        for (size_t shape = 0; shape < latentCodes.size(); ++shape) {
            const auto& latent = latentCodes[shape];
            if (latent.size() != size_t(m_latentDim)) continue;

            GridField& grid = m_reconstructedGridFields[shape];
            grid.values.resize(size_t(W) * size_t(H));
            grid.minValue =  std::numeric_limits<float>::max();
            grid.maxValue = -std::numeric_limits<float>::max();

            for (int y = 0; y < H; ++y) {
                const float yy = m_yMin + yStep * float(y);

                // pack one entire row into X: columns are pixels x=0..W-1
                for (int x = 0; x < W; ++x) {
                    const float xx = m_xMin + xStep * float(x);
                    // column j = x, row-major index is r*W + j
                    // fill latent first
                    for (int p=0; p<m_latentDim; ++p)
                        X[p*W + x] = latent[p];
                    // then encoded coords
                    m_ad.enc.encode(xx, yy, enc);
                    for (int p=0; p<m_ad.coordEncDim; ++p)
                        X[(m_latentDim + p)*W + x] = enc[p];
                }

                // batched forward for the whole row
                m_ad.decoder.forwardBatch(X, W);

                // read back outputs (A.back() is [1 x W] row-major)
                const float* Yrow = m_ad.decoder.A.back().data();
                for (int x = 0; x < W; ++x) {
                    const size_t idx = size_t(y) * size_t(W) + size_t(x);
                    const float v = Yrow[x];
                    grid.values[idx] = v;
                    grid.minValue = std::min(grid.minValue, v);
                    grid.maxValue = std::max(grid.maxValue, v);
                }
            }

            if (grid.minValue ==  std::numeric_limits<float>::max()) grid.minValue = 0.f;
            if (grid.maxValue == -std::numeric_limits<float>::max()) grid.maxValue = 0.f;
        }

        m_hasReconstruction = true;
    }

    // ---- rendering helpers (mask/heatmap toggle) ----
    inline float sigmoid01(float x) const { return 1.0f / (1.0f + std::exp(-x)); }
    inline float softMask01(float sdf, float tau) const { return sigmoid01(sdf / tau); }

    void drawFieldRow(Renderer& renderer, const std::vector<GridField>& grids, float top) const {
        if (grids.empty() || m_gridResolutionX <= 0 || m_gridResolutionY <= 0) return;

        const float gap = 25.0f;
        const float cellW = m_tileSize / float(m_gridResolutionX);
        const float cellH = m_tileSize / float(m_gridResolutionY);

        for (size_t fieldIdx = 0; fieldIdx < grids.size(); ++fieldIdx) {
            const float left = 20.0f + float(fieldIdx) * (m_tileSize + gap);
            drawField(renderer, grids[fieldIdx], left, top, cellW, cellH);
            renderer.setColor(Color(0.7f, 0.7f, 0.9f));
            renderer.drawString("#" + std::to_string(fieldIdx), left, top + m_tileSize + 16.0f);
        }
    }

    void drawField(Renderer& renderer, const GridField& field,
                   float left, float top, float cellW, float cellH) const
    {
        if (!m_showMask) {
            // continuous heatmap
            const float safeMin = field.minValue;
            const float safeMax = field.maxValue;
            const float range   = (safeMax - safeMin == 0.0f) ? 1.0f : (safeMax - safeMin);
            for (int y = 0; y < m_gridResolutionY; ++y) {
                for (int x = 0; x < m_gridResolutionX; ++x) {
                    const size_t idx = size_t(y) * size_t(m_gridResolutionX) + size_t(x);
                    const float value = field.values[idx];
                    const float norm  = (value - safeMin) / range;
                    const Color color = valueToGray(norm);

                    const float px = left + (float(x) + 0.5f) * cellW;
                    const float py = top  + (float(y) + 0.5f) * cellH;
                    const float pointSize = std::max(cellW, cellH) * 0.8f;
                    renderer.draw2dPoint(Vec2(px, py), color, pointSize);
                }
            }
            return;
        }

        // mask mode
        for (int y = 0; y < m_gridResolutionY; ++y) {
            for (int x = 0; x < m_gridResolutionX; ++x) {
                const size_t idx = size_t(y) * size_t(m_gridResolutionX) + size_t(x);
                const float sdf  = field.values[idx];

                float v01 = m_softMask ? softMask01(sdf, m_tau)
                                       : (sdf < 0.0f ? 0.0f : 1.0f);
                const Color color = valueToGray(v01);

                const float px = left + (float(x) + 0.5f) * cellW;
                const float py = top  + (float(y) + 0.5f) * cellH;
                const float pointSize = std::max(cellW, cellH) * 0.8f;
                renderer.draw2dPoint(Vec2(px, py), color, pointSize);
            }
        }

        // zero-cross hint
        renderer.setColor(Color(1.0f, 0.2f, 0.2f));
        const float ps = std::max(cellW, cellH) * 0.9f;
        for (int y = 0; y < m_gridResolutionY - 1; ++y) {
            for (int x = 0; x < m_gridResolutionX - 1; ++x) {
                const size_t i00 = size_t(y)   * size_t(m_gridResolutionX) + size_t(x);
                const size_t i10 = size_t(y)   * size_t(m_gridResolutionX) + size_t(x+1);
                const size_t i01 = size_t(y+1) * size_t(m_gridResolutionX) + size_t(x);
                const size_t i11 = size_t(y+1) * size_t(m_gridResolutionX) + size_t(x+1);

                const float s00 = field.values[i00];
                const float s10 = field.values[i10];
                const float s01 = field.values[i01];
                const float s11 = field.values[i11];

                const bool signMix = (s00<0)!=(s10<0) || (s10<0)!=(s11<0) || (s11<0)!=(s01<0) || (s01<0)!=(s00<0);
                if (signMix) {
                    const float cx = left + (float(x)+1.0f) * cellW;
                    const float cy = top  + (float(y)+1.0f) * cellH;
                    renderer.draw2dPoint(Vec2(cx, cy), Color(1.0f, 0.2f, 0.2f), ps*0.25f);
                }
            }
        }
    }

private:
    // domain & layout
    FieldDomain m_domain;
    float m_tileSize = 220.0f;

    // flags
    bool  m_hasOriginal       = false;
    bool  m_hasReconstruction = false;

    // visual toggles
    bool  m_showMask          = true;
    bool  m_softMask          = true;
    float m_tau               = 0.05f;

    // fields
    std::vector<GridField> m_originalGridFields;
    std::vector<GridField> m_reconstructedGridFields;

    // model
    TinyAutoDecoder m_ad;

    // UI/config
    int   m_gridResolutionX   = 128;
    int   m_gridResolutionY   = 128;
    int   m_latentDim         = 16;
    float m_xMin              = -1.2f, m_xMax = 1.2f;
    float m_yMin              = -1.2f, m_yMax = 1.2f;

    int   m_batchSize         = 64; // adjustable with B/b
};

// Register the sketch with alice2
ALICE2_REGISTER_SKETCH_AUTO(LatentSDFSketch_Batched)

#endif // __MAIN__

#pragma once
// WaveLatent.h — DCT-II orthonormal basis + power-based top-K selection + tiny linear AE.
// Minimal core: the sketch supplies fields via setFields(...). No viewer/loader here.

#include <alice2.h>
#include <vector>
#include <cmath>
#include <cstdio>
#include <random>
#include <limits>
#include <algorithm>
#include <numeric>

// ====================== DCT-II Orthonormal Basis ======================
struct DCT2Basis2D {
    int resX=0, resY=0;
    int Kx=32, Ky=32;                      // candidate modes per axis (usually >= resolution low-pass budget)
    std::vector<std::pair<int,int>> modes; // all (kx,ky) pairs in row-major (no truncation here)
    std::vector<std::vector<float>> B;     // B[k][p], p=y*resX+x, ORTHONORMAL images

    // Build full Kx*Ky orthonormal DCT-II basis on an resX x resY grid.
    // phi_{kx,ky}(x,y) = ax(kx)*ay(ky) * cos(pi/Nx * (x+0.5)*kx) * cos(pi/Ny * (y+0.5)*ky)
    void setup(int gridX, int gridY, int Kx_, int Ky_) {
        resX=gridX; resY=gridY; Kx=std::max(1,Kx_); Ky=std::max(1,Ky_);

        modes.clear(); modes.reserve(Kx*Ky);
        for (int ky=0; ky<Ky; ++ky)
            for (int kx=0; kx<Kx; ++kx)
                modes.emplace_back(kx,ky);

        const size_t P = size_t(resX)*size_t(resY);
        B.assign(modes.size(), std::vector<float>(P, 0.f));

        auto alphaX = [&](int k){ return (k==0) ? std::sqrt(1.0f/float(resX)) : std::sqrt(2.0f/float(resX)); };
        auto alphaY = [&](int k){ return (k==0) ? std::sqrt(1.0f/float(resY)) : std::sqrt(2.0f/float(resY)); };

        // Precompute 1D DCT tables
        std::vector<std::vector<float>> cx(Kx, std::vector<float>(resX));
        std::vector<std::vector<float>> cy(Ky, std::vector<float>(resY));
        for (int kx=0; kx<Kx; ++kx)
            for (int x=0; x<resX; ++x)
                cx[kx][x] = std::cos((alice2::PI/float(resX)) * (float(x)+0.5f) * float(kx));

        for (int ky=0; ky<Ky; ++ky)
            for (int y=0; y<resY; ++y)
                cy[ky][y] = std::cos((alice2::PI/float(resY)) * (float(y)+0.5f) * float(ky));

        // Compose 2D orthonormal images
        for (size_t k=0; k<modes.size(); ++k) {
            const int kx = modes[k].first;
            const int ky = modes[k].second;
            const float s = alphaX(kx) * alphaY(ky);
            auto& bk = B[k];
            for (int y=0; y<resY; ++y)
                for (int x=0; x<resX; ++x) {
                    size_t p = size_t(y)*size_t(resX) + size_t(x);
                    bk[p] = s * cx[kx][x] * cy[ky][y];
                }
        }
    }

    inline int Kfull() const { return (int)modes.size(); }
};

// ====================== Tiny Linear Autoencoder ======================
struct LinearAE {
    int inDim=0, latentDim=16;
    std::vector<float> We, Wd, be, bd;
    std::vector<float> z, xhat;
    std::vector<float> g_xhat, g_z, g_We, g_Wd, g_be, g_bd;

    void initialize(int inputDim, int zDim, unsigned seed=1234) {
        inDim = inputDim; latentDim = zDim;
        std::mt19937 rng(seed); std::normal_distribution<float> N(0.f, 0.02f);
        We.resize(latentDim*inDim); for (auto& v:We) v=N(rng);
        Wd.resize(inDim*latentDim); for (auto& v:Wd) v=N(rng);
        be.assign(latentDim, 0.f);
        bd.assign(inDim, 0.f);
        z.assign(latentDim, 0.f);
        xhat.assign(inDim, 0.f);
        g_xhat.assign(inDim, 0.f);
        g_z.assign(latentDim, 0.f);
        g_We.assign(latentDim*inDim, 0.f);
        g_Wd.assign(inDim*latentDim, 0.f);
        g_be.assign(latentDim, 0.f);
        g_bd.assign(inDim, 0.f);
    }

    void encode(const std::vector<float>& x, std::vector<float>& out_z) const {
        out_z.assign(latentDim, 0.f);
        for (int i=0;i<latentDim;i++){
            float s = be[i];
            const float* w = &We[i*inDim];
            for (int j=0;j<inDim;j++) s += w[j]*x[j];
            out_z[i] = s;
        }
    }
    void decode(const std::vector<float>& in_z, std::vector<float>& out_xhat) const {
        out_xhat.assign(inDim, 0.f);
        for (int i=0;i<inDim;i++){
            float s = bd[i];
            const float* w = &Wd[i*latentDim];
            for (int j=0;j<latentDim;j++) s += w[j]*in_z[j];
            out_xhat[i] = s;
        }
    }
    float forward(const std::vector<float>& x) {
        encode(x, z);
        decode(z, xhat);
        float L=0.f;
        for (int i=0;i<inDim;i++){ float d=xhat[i]-x[i]; L+=0.5f*d*d; }
        return L/float(inDim);
    }
    void backward(const std::vector<float>& x, float wdecay=0.f, float zReg=1e-3f) {
        for (int i=0;i<inDim;i++) g_xhat[i] = (xhat[i]-x[i])/float(inDim);
        std::fill(g_Wd.begin(), g_Wd.end(), 0.f);
        std::fill(g_bd.begin(), g_bd.end(), 0.f);
        std::fill(g_z.begin(), g_z.end(), 0.f);
        for (int i=0;i<inDim;i++){
            g_bd[i] += g_xhat[i];
            const float* WdRow = &Wd[i*latentDim];
            float* gWdRow = &g_Wd[i*latentDim];
            for (int j=0;j<latentDim;j++){
                gWdRow[j] += g_xhat[i]*z[j];
                g_z[j]    += g_xhat[i]*WdRow[j];
            }
        }
        if (zReg>0.f) for (int j=0;j<latentDim;j++) g_z[j] += zReg * z[j];

        std::fill(g_We.begin(), g_We.end(), 0.f);
        std::fill(g_be.begin(), g_be.end(), 0.f);
        for (int i=0;i<latentDim;i++){
            g_be[i] += g_z[i];
            float* gWeRow = &g_We[i*inDim];
            for (int j=0;j<inDim;j++) gWeRow[j] += g_z[i] * x[j];
        }
        if (wdecay>0.f) {
            for (size_t k=0;k<We.size();k++) g_We[k] += wdecay*We[k];
            for (size_t k=0;k<Wd.size();k++) g_Wd[k] += wdecay*Wd[k];
        }
    }
    void sgd(float lr) {
        for (int i=0;i<latentDim;i++){
            be[i] -= lr * g_be[i];
            float* WeRow = &We[i*inDim]; const float* gWeRow = &g_We[i*inDim];
            for (int j=0;j<inDim;j++) WeRow[j] -= lr * gWeRow[j];
        }
        for (int i=0;i<inDim;i++){
            bd[i] -= lr * g_bd[i];
            float* WdRow = &Wd[i*latentDim]; const float* gWdRow = &g_Wd[i*latentDim];
            for (int j=0;j<latentDim;j++) WdRow[j] -= lr * gWdRow[j];
        }
    }
};

// ====================== WaveLatent Core ======================
class WaveLatent {
public:
    // data / params
    int H=0, W=0;
    int Kx=32, Ky=32, keepK=512; // candidate per-axis + final kept K
    int latentDim=16;

    // Fields (GT)
    std::vector<std::vector<float>> fields;  // [S][H*W]

    // Basis (selected K after power-based selection)
    DCT2Basis2D basis_full;                  // full Kx*Ky
    std::vector<std::pair<int,int>> modes;   // selected K modes (subset of full)
    std::vector<std::vector<float>> B;       // selected basis images size K

    // Coefficients (normalized over dataset, selected K)
    std::vector<std::vector<float>> coeffs;  // [S][K] normalized
    std::vector<float> c_mean, c_std;        // per channel (K)

    // AE
    LinearAE ae;
    float lr=5e-3f, wdecay=1e-6f, zReg=1e-3f;

    // ---- IO: fields from sketch ----
    bool setFields(int H_, int W_, const std::vector<std::vector<float>>& f) {
        H=H_; W=W_; fields = f;
        if (H<=0 || W<=0 || fields.empty()) return false;
        const size_t P = size_t(H)*size_t(W);
        for (auto& g : fields) if (g.size()!=P) return false;
        return true;
    }

    // ---- Setup: build full DCT basis, project, select top-K by power, normalize, init AE ----
    bool initialize(int Kx_, int Ky_, int keepK_, int latentDim_, unsigned seed=1234) {
        if (fields.empty()) return false;
        Kx=Kx_; Ky=Ky_; keepK = keepK_; latentDim = latentDim_;

        // 1) full orthonormal DCT basis
        basis_full.setup(W, H, Kx, Ky);
        const int Kfull = basis_full.Kfull();
        const size_t P = size_t(H)*size_t(W);

        // 2) project all fields onto full basis -> coeffs_full[S][Kfull]
        std::vector<std::vector<float>> coeffs_full(fields.size(), std::vector<float>(Kfull, 0.f));
        for (size_t s=0; s<fields.size(); ++s) {
            const auto& f = fields[s];
            for (int k=0;k<Kfull;k++){
                const auto& bk = basis_full.B[k];
                double acc=0.0;
                for (size_t p=0;p<P;p++) acc += double(f[p]) * double(bk[p]); // orthonormal -> no /P
                coeffs_full[s][k] = float(acc);
            }
        }

        // 3) dataset-average power per mode & select top-keepK
        std::vector<double> power(Kfull, 0.0);
        for (int k=0; k<Kfull; ++k) {
            double acc=0.0;
            for (size_t s=0; s<coeffs_full.size(); ++s) {
                double v = double(coeffs_full[s][k]);
                acc += v*v;
            }
            power[k] = acc / std::max<size_t>(1, coeffs_full.size());
        }
        std::vector<int> idx(Kfull); std::iota(idx.begin(), idx.end(), 0);
        const int Ksel = std::min(keepK, Kfull);
        std::partial_sort(idx.begin(), idx.begin()+Ksel, idx.end(),
                          [&](int a,int b){ return power[a] > power[b]; });
        idx.resize(Ksel);

        // 4) materialize selected basis/modes and coeffs
        modes.clear(); modes.reserve(Ksel);
        B.clear();     B.reserve(Ksel);
        for (int r=0; r<Ksel; ++r) {
            modes.push_back(basis_full.modes[idx[r]]);
            B.push_back(basis_full.B[idx[r]]);
        }

        coeffs.assign(fields.size(), std::vector<float>(Ksel, 0.f));
        for (size_t s=0; s<fields.size(); ++s)
            for (int r=0; r<Ksel; ++r)
                coeffs[s][r] = coeffs_full[s][ idx[r] ];

        // 5) channel-wise normalization (stabilizes AE)
        c_mean.assign(Ksel, 0.f); c_std.assign(Ksel, 1.f);
        for (int k=0;k<Ksel;k++){
            double m=0.0; for (size_t s=0;s<coeffs.size();s++) m += coeffs[s][k];
            m /= std::max<size_t>(1, coeffs.size());
            double v=0.0; for (size_t s=0;s<coeffs.size();s++){ double d=coeffs[s][k]-m; v+=d*d; }
            v /= std::max<size_t>(1, coeffs.size());
            c_mean[k] = float(m);
            c_std[k]  = float(std::sqrt(v + 1e-12));
            for (size_t s=0;s<coeffs.size();s++)
                coeffs[s][k] = (coeffs[s][k]-c_mean[k]) / c_std[k];
        }

        // 6) init AE on selected K
        ae.initialize(/*inputDim*/Ksel, latentDim, seed);
        return true;
    }

    // ---- Train AE ----
    void train(int epochs) {
        for (int e=0; e<epochs; ++e) {
            double L=0.0;
            for (const auto& c : coeffs) {
                L += ae.forward(c);
                ae.backward(c, wdecay, zReg);
                ae.sgd(lr);
            }
            if (((e+1)%50)==0)
                std::printf("[WaveLatent] epoch %d  avgLoss=%.6f\n",
                            e+1, float(L/std::max<size_t>(1,coeffs.size())));
        }
    }

    // ---- Encode / Decode / Reconstruct ----
    void encodeSample(int i, std::vector<float>& z) const {
        if (i<0 || i>=(int)coeffs.size()) { z.assign(ae.latentDim, 0.f); return; }
        ae.encode(coeffs[i], z);
    }
    void latentToCoeffsDenorm(const std::vector<float>& z, std::vector<float>& c_den) const {
        std::vector<float> chat; ae.decode(z, chat);        // normalized
        c_den.resize(chat.size());
        for (int k=0;k<(int)chat.size();k++) c_den[k] = chat[k]*c_std[k] + c_mean[k];
    }
    void coeffsToGrid(const std::vector<float>& c_den, std::vector<float>& grid_out) const {
        const size_t P = size_t(H)*size_t(W);
        grid_out.assign(P, 0.f);
        const int K = (int)B.size();
        for (int k=0;k<K;k++){
            const auto& bk = B[k];
            const float ck = c_den[k];
            for (size_t p=0;p<P;p++) grid_out[p] += ck * bk[p];
        }
    }
    bool blendToGrid(int i, int j, float t, std::vector<float>& grid_out) const {
        if (i<0||j<0||i>=(int)coeffs.size()||j>=(int)coeffs.size()) return false;
        std::vector<float> zi, zj, zb; encodeSample(i, zi); encodeSample(j, zj);
        zb.resize(ae.latentDim);
        for (int d=0; d<ae.latentDim; ++d) zb[d] = (1.f-t)*zi[d] + t*zj[d];
        std::vector<float> cden; latentToCoeffsDenorm(zb, cden);
        coeffsToGrid(cden, grid_out);
        return true;
    }

    // ---- Diagnostics ----
    bool basisOnlyReconstruct(int i, std::vector<float>& grid_out) const {
        if (i<0 || i>=(int)fields.size()) return false;
        std::vector<float> cden(coeffs[i].size());
        for (int k=0;k<(int)cden.size();++k)
            cden[k] = coeffs[i][k]*c_std[k] + c_mean[k];
        coeffsToGrid(cden, grid_out);
        return true;
    }
    bool aeReconstruct(int i, std::vector<float>& grid_out) const {
        if (i<0 || i>=(int)fields.size()) return false;
        std::vector<float> z, cden; ae.encode(coeffs[i], z); latentToCoeffsDenorm(z, cden);
        coeffsToGrid(cden, grid_out); return true;
    }
    void printDiagnostics() const {
        const int S = (int)fields.size();
        const size_t P = (size_t)H*(size_t)W;
        auto mse = [&](const std::vector<float>& a, const std::vector<float>& b){
            double e=0.0; for (size_t p=0;p<P;++p){ double d=double(a[p])-double(b[p]); e+=d*d; }
            return float(e / std::max<size_t>(1,P));
        };
        std::printf("[WaveLatent] Diagnostics per sample:\n");
        for (int i=0;i<S;++i){
            std::vector<float> gb, ga;
            basisOnlyReconstruct(i, gb);
            aeReconstruct(i, ga);
            float mse_basis = mse(gb, fields[i]);
            float mse_ae    = mse(ga, fields[i]);

            // coeff space error (normalized)
            std::vector<float> z; ae.encode(coeffs[i], z);
            std::vector<float> chat; ae.decode(z, chat);
            double eC=0.0; for (int k=0;k<(int)chat.size();++k){ double d = double(chat[k])-double(coeffs[i][k]); eC += d*d; }
            eC /= std::max(1,(int)chat.size());

            std::printf("  #%d  MSE[basis]=%.6g  MSE[AE]=%.6g  MSE[coeff-norm]=%.6g\n",
                        i, mse_basis, mse_ae, float(eC));
        }
    }
    void getAllLatents(std::vector<std::vector<float>>& Z) const {
        const int S = (int)coeffs.size();
        Z.assign(S, std::vector<float>(ae.latentDim, 0.f));
        for (int i=0;i<S;++i) ae.encode(coeffs[i], Z[i]);
    }
    void decodeZtoGrid(const std::vector<float>& z, std::vector<float>& grid_out) const {
        std::vector<float> cden; latentToCoeffsDenorm(z, cden); coeffsToGrid(cden, grid_out);
    }
    void printCrossID() const {
        const int S = (int)fields.size();
        if (S==0) return;
        const size_t P = (size_t)H*(size_t)W;
        auto mse = [&](const std::vector<float>& a, const std::vector<float>& b){
            double e=0.0; for (size_t p=0;p<P;++p){ double d=double(a[p])-double(b[p]); e+=d*d; }
            return float(e / std::max<size_t>(1,P));
        };
        std::vector<std::vector<float>> Z; getAllLatents(Z);
        std::printf("[WaveLatent] Cross-ID (argmin MSE over GT):\n");
        for (int i=0;i<S;++i){
            std::vector<float> ghat; decodeZtoGrid(Z[i], ghat);
            int argmin = 0; float best = std::numeric_limits<float>::max();
            for (int j=0;j<S;++j){
                float e = mse(ghat, fields[j]);
                if (e < best){ best = e; argmin = j; }
            }
            std::printf("  z_%d -> GT #%d   MSE=%.6g\n", i, argmin, best);
        }
    }
};

#define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>

#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <limits>
#include <numeric>
#include <cmath>

#include <computeGeom/scalarField.h>   // your working ScalarField2D
#include "ML/WaveLatent/WaveLatent.h"                // updated DCT-II + top-K selection

using namespace alice2;
using nlohmann::json;

static const float kShapeGridMarker = 7777.0f;

class Sketch_WaveLatent_LatentPanel : public ISketch {
public:
    std::string getName() const override        { return "WaveLatent (5x5 Latent Panel, Continuous)"; }
    std::string getDescription() const override { return "Navigate a 2D latent plane (PCA) with the mouse; 5x5 samples + preview."; }
    std::string getAuthor() const override      { return "alice2 User"; }

    void setup() override {
        scene().setBackgroundColor(Color(0,0,0));
        scene().setShowGrid(false);
        scene().setShowAxes(false);

        // 1) Load shapes via your JSON loader (identical to CUDA sketch)
        std::vector<std::vector<float>> packedShapes;
        if (!loadShapesFromJSON(inShapePath_, packedShapes)) {
            std::printf("[LatentPanel] Failed to load JSON.\n");
            return;
        }
        if (packedShapes.empty()) {
            std::printf("[LatentPanel] No shapes in JSON.\n");
            return;
        }

        // 2) Unpack fields
        if (packedShapes[0].size() < 7) {
            std::printf("[LatentPanel] Packed shape too small.\n");
            return;
        }
        gridResX_ = int(packedShapes[0][1]);
        gridResY_ = int(packedShapes[0][2]);
        xMin_ = packedShapes[0][3];
        yMin_ = packedShapes[0][4];
        xMax_ = packedShapes[0][5];
        yMax_ = packedShapes[0][6];

        const size_t P = size_t(gridResX_) * size_t(gridResY_);
        fields_.clear(); fields_.reserve(packedShapes.size());
        for (auto& pk : packedShapes) {
            if (pk.size() < 7 + P) continue;
            fields_.emplace_back(pk.begin()+7, pk.begin()+7+P);
        }
        if (fields_.empty()) {
            std::printf("[LatentPanel] No valid fields after unpack.\n");
            return;
        }

        // 3) Init WaveLatent
        if (!wl_.setFields(/*H*/gridResY_, /*W*/gridResX_, fields_)) {
            std::printf("[LatentPanel] setFields failed.\n");
            return;
        }
        if (!wl_.initialize(Kx_, Ky_, keepK_, latentDim_)) {
            std::printf("[LatentPanel] initialize failed.\n");
            return;
        }

        wl_.train(initEpochs_);
        wl_.printDiagnostics();

        // 4) Gather latents for PCA
        const int S = int(fields_.size());
        Z_.assign(S, std::vector<float>(wl_.ae.latentDim, 0.f));
        for (int i=0;i<S;++i) wl_.encodeSample(i, Z_[i]);

        // 5) Build 2D PCA plane (e1,e2), mean mu, and limits
        buildPCA2_();

        // 6) Precompute 5x5 panel samples at fixed grid coords in that plane
        precomputePanel_();

        // Init preview from center
        hoverX_ = hoverY_ = -1;  // force rebuild on first mouse move
        previewSegments_.clear();

        std::printf("[LatentPanel] Ready. Move cursor over the 5x5 grid — preview updates on the right.\n"
                    "Keys: I=info, T=train(%d)\n", trainEpochs_);
    }

    void update(float) override {}

    void draw(Renderer& r, Camera&) override {
        // Window & layout
        int winW=1280, winH=720;

        layout_(winW, winH);

        // Draw 5x5 panel tiles
        const int N = 5;
        for (int i=0;i<N;++i) {
            for (int j=0;j<N;++j) {
                float L, T, W, H; tileRect_(i, j, L, T, W, H);
                drawTile_(r, L, T, W, H, panelSegments_[i*N + j]);
            }
        }

        // Hover highlight (nearest tile)
        if (hoverTileI_>=0 && hoverTileJ_>=0) {
            float L, T, W, H; tileRect_(hoverTileI_, hoverTileJ_, L, T, W, H);
            drawRectOutline_(r, L-1, T-1, W+2, H+2, Color(0.9f,0.9f,0.9f,1), 2.0f);
        }

        // Right-side preview
        drawTile_(r, previewL_, previewT_, previewW_, previewH_, previewSegments_);

        for (const Vec2& uv : sampleUV_) {
        // map (u,v) in PCA plane → screen pixel in panel
        float xi = (uv.x - uMin_) / (uMax_ - uMin_);
        float yi = (uv.y - vMin_) / (vMax_ - vMin_);
        // same vertical flip as panelXYtoUV_
        yi = 1.f - yi;
        if (xi < 0.f || xi > 1.f || yi < 0.f || yi > 1.f) continue;

        float sx = panelL_ + xi * panelW_;
        float sy = panelT_ + yi * panelH_;
        r.draw2dPoint(Vec2(sx,sy), Color(0.95f,0.2f,0.2f,1.0f), 4.0f); // red point
    }

        // Titles
        drawText_(r, panelL_, panelT_ - 8, Color(1,1,1,1), "Latent plane (PCA) — 5x5 samples");
        drawText_(r, previewL_, previewT_ - 8, Color(1,1,1,1), "Preview (continuous decode from cursor)");
    }

    // Mouse → map to continuous (u,v) in PCA plane, then rebuild preview
    bool onMouseMove(int x, int y) override {
        if (x==hoverX_ && y==hoverY_) return false;
        hoverX_ = x; hoverY_ = y;

        // Identify which tile is nearest (for simple visual highlight)
        const int N = 5;
        int ii=-1, jj=-1;
        float bestD2 = 1e30f;
        for (int i=0;i<N;++i) {
            for (int j=0;j<N;++j) {
                float L, T, W, H; tileRect_(i, j, L, T, W, H);
                float cx = L + 0.5f*W, cy = T + 0.5f*H;
                float dx = float(x) - cx, dy = float(y) - cy;
                float d2 = dx*dx + dy*dy;
                if (d2 < bestD2) { bestD2 = d2; ii=i; jj=j; }
            }
        }
        hoverTileI_ = ii; hoverTileJ_ = jj;

        // Map (x,y) clipped to panel -> (u,v) in PCA plane limits
        if (pointInPanel_(float(x), float(y))) {
            float u, v; panelXYtoUV_(float(x), float(y), u, v);
            rebuildPreview_(u, v);
        }
        return false;
    }

    bool onKeyPress(unsigned char k, int, int) override {
        if (k=='i' || k=='I') {
            std::printf("[Info] Shapes=%zu  Res=%dx%d  K(selected)=%zu  K(full)=%d  latent=%d  bbox=[%.2f %.2f]x[%.2f %.2f]\n",
                        wl_.fields.size(),
                        wl_.W, wl_.H,
                        wl_.B.size(), wl_.basis_full.Kfull(),
                        wl_.ae.latentDim,
                        xMin_, yMin_, xMax_, yMax_);
            std::printf("       PCA limits: u=[%.3f, %.3f], v=[%.3f, %.3f]\n",
                        uMin_, uMax_, vMin_, vMax_);
            return true;
        }
        if (k=='t' || k=='T') {
            wl_.train(trainEpochs_);
            // Re-encode latents (AE changed), rebuild PCA and panel
            const int S = int(fields_.size());
            Z_.assign(S, std::vector<float>(wl_.ae.latentDim, 0.f));
            for (int i=0;i<S;++i) wl_.encodeSample(i, Z_[i]);

            buildPCA2_();
            precomputePanel_();

            // Rebuild preview at prior cursor if inside panel
            if (pointInPanel_(float(hoverX_), float(hoverY_))) {
                float u, v; panelXYtoUV_(float(hoverX_), float(hoverY_), u, v);
                rebuildPreview_(u, v);
            } else {
                previewSegments_.clear();
            }

            wl_.printDiagnostics();
            return true;
        }
        return false;
    }

private:
    // ====== PCA(2) over latents ======
    // Compute mean mu_, principal directions e1_, e2_, and axis limits uMin_/uMax_, vMin_/vMax_.
    void buildPCA2_() {
        const int S = int(Z_.size());
        if (S==0) return;
        const int D = int(Z_[0].size());

        // mean
        mu_.assign(D, 0.f);
        for (const auto& z : Z_)
            for (int d=0; d<D; ++d) mu_[d] += z[d];
        for (int d=0; d<D; ++d) mu_[d] /= float(S);

        // centered matrix: rows = S, cols = D
        std::vector<float> C(D*D, 0.f); // covariance = (1/S) Zc^T Zc
        for (int s=0; s<S; ++s) {
            for (int i=0; i<D; ++i) {
                float zi = Z_[s][i] - mu_[i];
                for (int j=0; j<D; ++j) {
                    C[i*D + j] += zi * (Z_[s][j] - mu_[j]);
                }
            }
        }
        for (float& v : C) v /= std::max(1, S);

        // Top-2 eigenvectors via simple power iteration + deflation (D is small)
        e1_.assign(D, 0.f); e2_.assign(D, 0.f);
        float lam1=0.f, lam2=0.f;
        powerEigen_(C, D, /*iters*/60, e1_, lam1);
        // Deflate: C = C - lam1 * e1 e1^T
        for (int i=0;i<D;++i){
            for (int j=0;j<D;++j){
                C[i*D + j] -= lam1 * e1_[i] * e1_[j];
            }
        }
        powerEigen_(C, D, /*iters*/60, e2_, lam2);

        // Axis limits based on projection of data (±scale * std)
        float s1 = std::sqrt(std::max(0.f, lam1));
        float s2 = std::sqrt(std::max(0.f, lam2));
        const float kScale = 2.0f; // show most data; tweak if you like
        uMin_ = -kScale * s1; uMax_ = +kScale * s1;
        vMin_ = -kScale * s2; vMax_ = +kScale * s2;
        if (uMax_ - uMin_ < 1e-6f) { uMin_ -= 1.f; uMax_ += 1.f; }
        if (vMax_ - vMin_ < 1e-6f) { vMin_ -= 1.f; vMax_ += 1.f; }

        // project each latent z_i to (u,v)
        sampleUV_.clear();
        for (const auto& z : Z_) {
            float u=0.f, v=0.f;
            for (int d=0; d<(int)mu_.size(); ++d) {
                float zc = z[d] - mu_[d];
                u += zc * e1_[d];
                v += zc * e2_[d];
            }
            sampleUV_.emplace_back(u,v);
        }
    }

    // Power iteration to get dominant eigenvector/value of symmetric C (D×D)
    static void powerEigen_(const std::vector<float>& C, int D, int iters,
                            std::vector<float>& outVec, float& outLam)
    {
        // init random unit vec (deterministic small pattern)
        outVec.assign(D, 0.f);
        for (int d=0; d<D; ++d) outVec[d] = 0.1f + 0.013f*(d+1);
        normalize_(outVec);

        std::vector<float> y(D, 0.f);
        for (int it=0; it<iters; ++it) {
            // y = C * outVec
            std::fill(y.begin(), y.end(), 0.f);
            for (int i=0;i<D;++i){
                float s=0.f;
                for (int j=0;j<D;++j) s += C[i*D + j] * outVec[j];
                y[i] = s;
            }
            normalize_(y);
            outVec.swap(y);
        }
        // Rayleigh quotient for eigenvalue
        float num=0.f, den=0.f;
        for (int i=0;i<D;++i){
            float Ci=0.f; for (int j=0;j<D;++j) Ci += C[i*D + j]*outVec[j];
            num += outVec[i]*Ci;
            den += outVec[i]*outVec[i];
        }
        outLam = (den>0.f)? (num/den) : 0.f;
    }

    static void normalize_(std::vector<float>& v) {
        double n2=0.0; for (float x:v) n2 += double(x)*double(x);
        float inv = (n2>0.0)? float(1.0/std::sqrt(n2)) : 1.0f;
        for (float& x:v) x*=inv;
    }

    // ====== Panel precompute (5x5 samples) ======
    void precomputePanel_() {
        const int N = 5;
        panelSegments_.assign(N*N, Segments{});

        for (int i=0;i<N;++i) {
            float v = lerp(vMax_, vMin_, (i+0.5f)/float(N)); // top -> bottom
            for (int j=0;j<N;++j) {
                float u = lerp(uMin_, uMax_, (j+0.5f)/float(N));
                std::vector<float> z; zOnPlane_(u, v, z);

                std::vector<float> grid; wl_.decodeZtoGrid(z, grid);
                marchingSquaresZero_(grid, panelSegments_[i*N + j]);
            }
        }
    }

    // Build z = mu + u*e1 + v*e2
    void zOnPlane_(float u, float v, std::vector<float>& z) const {
        const int D = int(mu_.size());
        z.assign(D, 0.f);
        for (int d=0; d<D; ++d) z[d] = mu_[d] + u*e1_[d] + v*e2_[d];
    }

    // Map panel pixel (x,y) → (u,v) within [uMin_,uMax_]×[vMin_,vMax_]
    void panelXYtoUV_(float x, float y, float& u, float& v) const {
        float xi = clamp01((x - panelL_) / std::max(1e-6f, panelW_));
        float yi = clamp01((y - panelT_) / std::max(1e-6f, panelH_));
        u = lerp(uMin_, uMax_, xi);
        v = lerp(vMax_, vMin_, yi); // note: increasing y goes down; that's fine for continuity
    }

    void rebuildPreview_(float u, float v) {
        previewSegments_.clear();
        std::vector<float> z; zOnPlane_(u, v, z);
        std::vector<float> grid; wl_.decodeZtoGrid(z, grid);
        marchingSquaresZero_(grid, previewSegments_);
    }

    // ====== Marching squares for zero-iso ======
    struct Segment { float ax,ay,bx,by; };
    using Segments = std::vector<Segment>;

    void marchingSquaresZero_(const std::vector<float>& grid, Segments& outSegs) const {
        outSegs.clear();
        const int H = wl_.H, W = wl_.W;
        if (H<2 || W<2) return;

        auto v = [&](int x,int y)->float { return grid[size_t(y)*size_t(W)+size_t(x)]; };
        auto wx = [&](int x)->float { return lerp(xMin_, xMax_, float(x)/float(W-1)); };
        auto wy = [&](int y)->float { return lerp(yMin_, yMax_, float(y)/float(H-1)); };
        auto tEdge = [&](float a, float b)->float {
            float d = (b - a); return (std::fabs(d) > 1e-12f) ? (-a / d) : 0.5f;
        };

        for (int y=0; y<H-1; ++y) {
            for (int x=0; x<W-1; ++x) {
                float v00 = v(x, y);
                float v10 = v(x+1, y);
                float v01 = v(x, y+1);
                float v11 = v(x+1, y+1);

                int mask = (v00>0.f) | ((v10>0.f)<<1) | ((v11>0.f)<<2) | ((v01>0.f)<<3);
                if (mask==0 || mask==15) continue;

                auto eL = [&](){ float t=tEdge(v00, v01); return std::pair<float,float>( wx(x),       lerp(wy(y), wy(y+1), t) ); };
                auto eR = [&](){ float t=tEdge(v10, v11); return std::pair<float,float>( wx(x+1),     lerp(wy(y), wy(y+1), t) ); };
                auto eB = [&](){ float t=tEdge(v00, v10); return std::pair<float,float>( lerp(wx(x), wx(x+1), t), wy(y)     ); };
                auto eT = [&](){ float t=tEdge(v01, v11); return std::pair<float,float>( lerp(wx(x), wx(x+1), t), wy(y+1)   ); };

                switch (mask) {
                    case 1: case 14: { auto a=eL(), b=eB(); pushSeg_(outSegs,a,b); break; }
                    case 2: case 13: { auto a=eB(), b=eR(); pushSeg_(outSegs,a,b); break; }
                    case 3: case 12: { auto a=eL(), b=eR(); pushSeg_(outSegs,a,b); break; }
                    case 4: case 11: { auto a=eR(), b=eT(); pushSeg_(outSegs,a,b); break; }
                    case 5:          { auto a=eL(), b=eB(); pushSeg_(outSegs,a,b); auto c=eR(), d=eT(); pushSeg_(outSegs,c,d); break; }
                    case 6: case 9:  { auto a=eB(), b=eT(); pushSeg_(outSegs,a,b); break; }
                    case 7: case 8:  { auto a=eL(), b=eT(); pushSeg_(outSegs,a,b); break; }
                    case 10:         { auto a=eL(), b=eT(); pushSeg_(outSegs,a,b); auto c=eB(), d=eR(); pushSeg_(outSegs,c,d); break; }
                    default: break;
                }
            }
        }
    }

    static inline void pushSeg_(Segments& segs, const std::pair<float,float>& a, const std::pair<float,float>& b) {
        segs.push_back(Segment{a.first,a.second,b.first,b.second});
    }

    // ====== Drawing & layout ======
    void drawTile_(Renderer& r, float L, float T, float W, float H, const Segments& segs) const {
        drawRectOutline_(r, L, T, W, H, Color(0.2f,0.2f,0.2f,1.0f), 1.0f);
        for (const auto& s : segs) {
            Vec2 a(mapX_(s.ax, L, W), mapY_(s.ay, T, H));
            Vec2 b(mapX_(s.bx, L, W), mapY_(s.by, T, H));
            r.draw2dLine(a, b, isoColor_, 1.8f);
        }
    }

    void layout_(int winW, int winH) {
        const float pad  = 24.0f;
        const float gap  = 14.0f;

        // Reserve ~65% width for panel, 35% for preview
        float totalW = float(winW) - 2*pad;
        float totalH = float(winH) - 2*pad;

        panelL_ = pad;
        panelT_ = pad;
        panelW_ = totalW * 0.65f;
        panelH_ = totalH;

        previewL_ = panelL_ + panelW_ + gap;
        previewT_ = pad;
        previewW_ = totalW - panelW_ - gap;
        previewH_ = totalH;

        // 5x5 tiles inside panel
        const int N = 5;
        tileW_ = std::max(28.f, (panelW_ - tileGap_*(N-1)) / float(N));
        tileH_ = std::max(28.f, (panelH_ - tileGap_*(N-1)) / float(N));
    }

    void tileRect_(int i, int j, float& L, float& T, float& W, float& H) const {
        const int N = 5;
        L = panelL_ + j * (tileW_ + tileGap_);
        T = panelT_ + i * (tileH_ + tileGap_);
        W = tileW_;
        H = tileH_;
    }

    bool pointInPanel_(float x, float y) const {
        return (x >= panelL_ && x <= panelL_+panelW_ && y >= panelT_ && y <= panelT_+panelH_);
    }

    inline float mapX_(float wx, float L, float W) const { return L + ( (wx - xMin_) / std::max(1e-12f, (xMax_-xMin_)) ) * W; }
    inline float mapY_(float wy, float T, float H) const { return T + (1.f - ( (wy - yMin_) / std::max(1e-12f, (yMax_-yMin_)) )) * H; }
    void drawRectOutline_(Renderer& r, float L, float T, float W, float H, const Color& c, float thickness) const {
        r.draw2dLine(Vec2(L,   T),   Vec2(L+W,T),   c, thickness);
        r.draw2dLine(Vec2(L+W, T),   Vec2(L+W,T+H), c, thickness);
        r.draw2dLine(Vec2(L+W, T+H), Vec2(L,  T+H), c, thickness);
        r.draw2dLine(Vec2(L,   T+H), Vec2(L,  T),   c, thickness);
    }
    void drawText_(Renderer& r, float x, float y, const Color& c, const char* s) const {
        r.drawString(s, x, y);
    }
    static inline float lerp(float a, float b, float t){ return a + (b-a)*t; }
    static inline float clamp01(float x){ return std::max(0.f, std::min(1.f, x)); }

    // === IDENTICAL loader to your CUDA sketch ===
    bool loadShapesFromJSON(const std::string& filePath,
                            std::vector<std::vector<float>>& shapes)
    {
        shapes.clear();

        std::ifstream file(filePath);
        if (!file.is_open()) {
            std::cerr << "Failed to open JSON file: " << filePath << "\n";
            return false;
        }

        json j;
        try {
            file >> j;
        } catch (const std::exception& e) {
            std::cerr << "Failed to parse JSON: " << e.what() << "\n";
            return false;
        }

        if (!j.contains("shapes") || !j["shapes"].is_array()) {
            std::cerr << "JSON file missing 'shapes' array.\n";
            return false;
        }

        Vec3 minBB(-1.2f, -1.2f, 0.0f);
        Vec3 maxBB( 1.2f,  1.2f, 0.0f);
        if (const auto bboxIt = j.find("bbox"); bboxIt != j.end()) {
            const auto& bbox = *bboxIt;
            if (bbox.contains("minbb") && bbox["minbb"].is_array() && bbox["minbb"].size() >= 2) {
                minBB.x = bbox["minbb"][0].get<float>();
                minBB.y = bbox["minbb"][1].get<float>();
            }
            if (bbox.contains("maxbb") && bbox["maxbb"].is_array() && bbox["maxbb"].size() >= 2) {
                maxBB.x = bbox["maxbb"][0].get<float>();
                maxBB.y = bbox["maxbb"][1].get<float>();
            }
        }

        minBB.z = 0.0f;
        maxBB.z = 0.0f;

        shapes.reserve(j["shapes"].size());

        for (const auto& branch : j["shapes"]) {
            ScalarField2D field(minBB, maxBB, gridResX_, gridResY_);

            if (branch.contains("polys") && branch["polys"].is_array()) {
                for (const auto& poly : branch["polys"]) {
                    if (!poly.is_array() || poly.empty()) continue;
                    std::vector<Vec3> pts;
                    pts.reserve(poly.size());
                    for (const auto& p : poly) {
                        if (!p.is_array() || p.size() < 2) continue;
                        const float px = p[0].get<float>();
                        const float py = p[1].get<float>();
                        const float pz = (p.size() > 2) ? p[2].get<float>() : 0.0f;
                        pts.emplace_back(px, py, pz);
                    }
                    if (!pts.empty()) {
                        field.apply_scalar_polygon(pts);
                    }
                }
            }

            const auto& values_before = field.get_values();
            if (!values_before.empty()) {
                auto [minIt_before, maxIt_before] = std::minmax_element(values_before.begin(), values_before.end());
                const float minVal_before = *minIt_before;
                const float maxVal_before = *maxIt_before;
                std::printf("field min max before: %.6f %.6f\n", minVal_before, maxVal_before);
            }

            field.normalise();
            const auto& values = field.get_values();
            if (!values.empty()) {
                auto [minIt, maxIt] = std::minmax_element(values.begin(), values.end());
                const float minVal = *minIt;
                const float maxVal = *maxIt;
                std::printf("field min max: %.6f %.6f\n", minVal, maxVal);
            }

            if (values.size() == size_t(gridResX_) * size_t(gridResY_)) {
                std::vector<float> packed;
                packed.reserve(7 + values.size());
                packed.push_back(kShapeGridMarker);
                packed.push_back(static_cast<float>(gridResX_));
                packed.push_back(static_cast<float>(gridResY_));
                packed.push_back(xMin_);
                packed.push_back(yMin_);
                packed.push_back(xMax_);
                packed.push_back(yMax_);
                packed.insert(packed.end(), values.begin(), values.end());
                shapes.emplace_back(std::move(packed));
            }
        }

        std::printf("Loaded %zu shapes from JSON\n", shapes.size());
        return !shapes.empty();
    }

private:
    // ====== Config ======
    std::string inShapePath_ = "inShapes.json";

    int   gridResX_ = 256;
    int   gridResY_ = 256;
    float xMin_ = -1.2f, xMax_ = 1.2f;
    float yMin_ = -1.2f, yMax_ = 1.2f;

    // Basis/latent
    int Kx_ = 32, Ky_ = 32, keepK_ = 512;
    int latentDim_ = 16;

    // Training schedule
    int initEpochs_  = 200;
    int trainEpochs_ = 200;

    // Data & latent
    std::vector<std::vector<float>> fields_;
    std::vector<std::vector<float>> Z_; // latents z_i
    std::vector<Vec2> sampleUV_;  // projected latent positions in PCA plane

    // PCA plane
    std::vector<float> mu_, e1_, e2_;
    float uMin_= -1.f, uMax_= +1.f;
    float vMin_= -1.f, vMax_= +1.f;

    // Panel state (5x5 samples in PCA plane)
    std::vector<Segments> panelSegments_;   // size 25

    // Preview state
    Segments previewSegments_;

    // Layout cached
    float panelL_=0, panelT_=0, panelW_=0, panelH_=0;
    float previewL_=0, previewT_=0, previewW_=0, previewH_=0;
    float tileW_=0, tileH_=0;
    float tileGap_ = 10.0f;

    // Hover cache
    int hoverX_=-1, hoverY_=-1;
    int hoverTileI_=-1, hoverTileJ_=-1;

    // Colors
    Color isoColor_ = Color(0.95f, 0.85f, 0.10f, 1.0f);

    // Core
    WaveLatent wl_;
};

ALICE2_REGISTER_SKETCH_AUTO(Sketch_WaveLatent_LatentPanel)

#endif // __MAIN__

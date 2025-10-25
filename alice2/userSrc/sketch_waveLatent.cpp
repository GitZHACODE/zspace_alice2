#define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <limits>

#include <computeGeom/scalarField.h>
#include "ML/WaveLatent/WaveLatent.h"

using namespace alice2;
using nlohmann::json;

static const float kShapeGridMarker = 7777.0f;

class Sketch_WaveLatent_Compare : public ISketch {
public:
    std::string getName() const override        { return "WaveLatent (Compare)"; }
    std::string getDescription() const override { return "Compare ground truth, AE recon, and basis-only recon."; }
    std::string getAuthor() const override      { return "alice2 User"; }

    void setup() override {
        scene().setBackgroundColor(Color(0,0,0));
        scene().setShowGrid(false);
        scene().setShowAxes(false);

        // 1) Load shapes via your JSON loader
        std::vector<std::vector<float>> packedShapes;
        if (!loadShapesFromJSON(inShapePath_, packedShapes)) {
            std::printf("[Compare] Failed to load JSON.\n");
            return;
        }
        if (packedShapes.empty()) {
            std::printf("[Compare] No shapes in JSON.\n");
            return;
        }

        // 2) Unpack fields
        if (packedShapes[0].size() < 7) {
            std::printf("[Compare] Packed shape too small.\n");
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
            std::printf("[Compare] No valid fields after unpack.\n");
            return;
        }

        // 3) Init WaveLatent
        wl_.setFields(gridResY_, gridResX_, fields_);
        wl_.initialize(Kx_, Ky_, keepK_, latentDim_);
        wl_.train(initEpochs_);
        wl_.printDiagnostics();

        // 4) Build reconstructions
        const int S = int(fields_.size());
        aeRecon_.assign(S, std::vector<float>(size_t(gridResX_)*size_t(gridResY_), 0.f));
        basisRecon_.assign(S, std::vector<float>(size_t(gridResX_)*size_t(gridResY_), 0.f));
        for (int i=0;i<S;++i) {
            wl_.aeReconstruct(i, aeRecon_[i]);
            wl_.basisOnlyReconstruct(i, basisRecon_[i]);
        }

        std::printf("[Compare] Ready. Keys: I=info, T=train(%d)\n", trainEpochs_);
    }

    void update(float) override {}

    void draw(Renderer& r, Camera&) override {
        int winW=1280, winH=720;

        layout_(winW, winH);

        const int S = int(fields_.size());
        const float gap = tileGap_;
        for (int i=0;i<S;++i) {
            float L = panelL_ + i*(tileW_ + gap);
            drawTile_(r, L, row1T_, tileW_, tileH_, fields_[i]);       // GT
            drawTile_(r, L, row2T_, tileW_, tileH_, aeRecon_[i]);      // AE recon
            drawTile_(r, L, row3T_, tileW_, tileH_, basisRecon_[i]);   // basis-only
        }

        drawText_(r, panelL_, row1T_ - 8, Color(1,1,1,1), "Ground Truth");
        drawText_(r, panelL_, row2T_ - 8, Color(1,1,1,1), "AE Reconstruction");
        drawText_(r, panelL_, row3T_ - 8, Color(1,1,1,1), "Basis-only Reconstruction");
    }

    bool onKeyPress(unsigned char k, int, int) override {
        if (k=='i' || k=='I') {
            std::printf("[Info] Shapes=%zu  Res=%dx%d  K(selected)=%zu  K(full)=%d  latent=%d  bbox=[%.2f %.2f]x[%.2f %.2f]\n",
                        wl_.fields.size(),
                        wl_.W, wl_.H,
                        wl_.B.size(), wl_.basis_full.Kfull(),
                        wl_.ae.latentDim,
                        xMin_, yMin_, xMax_, yMax_);
            return true;
        }
        if (k=='t' || k=='T') {
            wl_.train(trainEpochs_);
            wl_.printDiagnostics();

            // Rebuild reconstructions
            const int S = int(fields_.size());
            for (int i=0;i<S;++i) {
                wl_.aeReconstruct(i, aeRecon_[i]);
                wl_.basisOnlyReconstruct(i, basisRecon_[i]);
            }
            return true;
        }
        return false;
    }

private:
    // ====== Layout & drawing ======
    void layout_(int winW, int winH) {
        const float pad = 20.0f;
        const float gap = 10.0f;
        float totalW = float(winW) - 2*pad;
        float totalH = float(winH) - 2*pad;

        panelL_ = pad;
        panelT_ = pad;
        panelW_ = totalW;
        panelH_ = totalH;

        const int S = int(fields_.size());
        tileW_ = (panelW_ - gap*(S-1)) / float(S);
        tileH_ = (panelH_ - 2*gap) / 3.0f;

        row1T_ = panelT_;
        row2T_ = row1T_ + tileH_ + gap;
        row3T_ = row2T_ + tileH_ + gap;
    }

    void drawTile_(Renderer& r, float L, float T, float W, float H,
                   const std::vector<float>& grid) const {
        drawRectOutline_(r, L, T, W, H, Color(0.3f,0.3f,0.3f,1.0f), 1.0f);

        // Marching-squares zero-iso
        const int Hs = wl_.H, Ws = wl_.W;
        const size_t P = size_t(Hs)*size_t(Ws);
        if (grid.size()!=P) return;

        auto v = [&](int x,int y)->float { return grid[size_t(y)*size_t(Ws)+size_t(x)]; };
        auto wx = [&](int x)->float { return lerp(xMin_, xMax_, float(x)/float(Ws-1)); };
        auto wy = [&](int y)->float { return lerp(yMin_, yMax_, float(y)/float(Hs-1)); };
        auto tEdge = [&](float a, float b)->float { float d=b-a; return (fabs(d)>1e-12f)?(-a/d):0.5f; };

        Color c = Color(0.95f,0.85f,0.1f,1.0f);
        for (int y=0;y<Hs-1;y++){
            for (int x=0;x<Ws-1;x++){
                float v00=v(x,y),v10=v(x+1,y),v01=v(x,y+1),v11=v(x+1,y+1);
                int mask=(v00>0)|((v10>0)<<1)|((v11>0)<<2)|((v01>0)<<3);
                if(mask==0||mask==15)continue;
                auto eL=[&](){float t=tEdge(v00,v01);return Vec2(wx(x),lerp(wy(y),wy(y+1),t));};
                auto eR=[&](){float t=tEdge(v10,v11);return Vec2(wx(x+1),lerp(wy(y),wy(y+1),t));};
                auto eB=[&](){float t=tEdge(v00,v10);return Vec2(lerp(wx(x),wx(x+1),t),wy(y));};
                auto eT=[&](){float t=tEdge(v01,v11);return Vec2(lerp(wx(x),wx(x+1),t),wy(y+1));};
                auto drawSeg=[&](const Vec2&a,const Vec2&b){ 
                    r.draw2dLine(mapToScreen_(a,L,T,W,H), mapToScreen_(b,L,T,W,H), c, 1.5f);
                };
                switch(mask){
                    case 1:case 14:{auto a=eL(),b=eB();drawSeg(a,b);break;}
                    case 2:case 13:{auto a=eB(),b=eR();drawSeg(a,b);break;}
                    case 3:case 12:{auto a=eL(),b=eR();drawSeg(a,b);break;}
                    case 4:case 11:{auto a=eR(),b=eT();drawSeg(a,b);break;}
                    case 5:{auto a=eL(),b=eB();drawSeg(a,b);auto c1=eR(),d=eT();drawSeg(c1,d);break;}
                    case 6:case 9:{auto a=eB(),b=eT();drawSeg(a,b);break;}
                    case 7:case 8:{auto a=eL(),b=eT();drawSeg(a,b);break;}
                    case 10:{auto a=eL(),b=eT();drawSeg(a,b);auto c1=eB(),d=eR();drawSeg(c1,d);break;}
                }
            }
        }
    }

    Vec2 mapToScreen_(const Vec2& w, float L, float T, float W, float H) const {
        float sx = L + ( (w.x - xMin_) / (xMax_-xMin_) ) * W;
        float sy = T + (1.0f - ( (w.y - yMin_) / (yMax_-yMin_) )) * H;
        return Vec2(sx, sy);
    }

    void drawRectOutline_(Renderer& r, float L, float T, float W, float H,
                          const Color& c, float thickness) const {
        r.draw2dLine(Vec2(L, T), Vec2(L+W, T), c, thickness);
        r.draw2dLine(Vec2(L+W, T), Vec2(L+W, T+H), c, thickness);
        r.draw2dLine(Vec2(L+W, T+H), Vec2(L, T+H), c, thickness);
        r.draw2dLine(Vec2(L, T+H), Vec2(L, T), c, thickness);
    }

    void drawText_(Renderer& r, float x, float y, const Color& c, const char* s) const {
        r.drawString(s, x, y);
    }

    static inline float lerp(float a, float b, float t){ return a + (b-a)*t; }

    bool loadShapesFromJSON(const std::string& filePath,
                            std::vector<std::vector<float>>& shapes)
    {
        shapes.clear();
        std::ifstream file(filePath);
        if (!file.is_open()) { std::cerr << "Failed to open JSON file: " << filePath << "\n"; return false; }
        json j; file >> j;
        if (!j.contains("shapes") || !j["shapes"].is_array()) { std::cerr << "Missing shapes array.\n"; return false; }

        Vec3 minBB(-1.2f,-1.2f,0), maxBB(1.2f,1.2f,0);
        if (auto bboxIt = j.find("bbox"); bboxIt != j.end()) {
            const auto& bbox=*bboxIt;
            if (bbox.contains("minbb")) { minBB.x=bbox["minbb"][0]; minBB.y=bbox["minbb"][1]; }
            if (bbox.contains("maxbb")) { maxBB.x=bbox["maxbb"][0]; maxBB.y=bbox["maxbb"][1]; }
        }
        shapes.reserve(j["shapes"].size());
        for (const auto& branch : j["shapes"]) {
            ScalarField2D field(minBB,maxBB,gridResX_,gridResY_);
            if (branch.contains("polys"))
                for (const auto& poly:branch["polys"]) {
                    std::vector<Vec3> pts;
                    for (const auto& p:poly) pts.emplace_back(p[0],p[1],0);
                    if (!pts.empty()) field.apply_scalar_polygon(pts);
                }
            field.normalise();
            const auto& vals=field.get_values();
            if (vals.size()!=size_t(gridResX_)*size_t(gridResY_)) continue;
            std::vector<float> packed;
            packed.reserve(7+vals.size());
            packed.push_back(kShapeGridMarker);
            packed.push_back((float)gridResX_);
            packed.push_back((float)gridResY_);
            packed.push_back(xMin_); packed.push_back(yMin_);
            packed.push_back(xMax_); packed.push_back(yMax_);
            packed.insert(packed.end(), vals.begin(), vals.end());
            shapes.emplace_back(std::move(packed));
        }
        std::printf("Loaded %zu shapes from JSON\n", shapes.size());
        return !shapes.empty();
    }

private:
    // Config
    std::string inShapePath_ = "inShapes.json";
    int gridResX_=256, gridResY_=256;
    float xMin_=-1.2f, xMax_=1.2f, yMin_=-1.2f, yMax_=1.2f;
    int Kx_=32, Ky_=32, keepK_=512;
    int latentDim_=16;
    int initEpochs_=200;
    int trainEpochs_=200;

    // Data
    std::vector<std::vector<float>> fields_;
    std::vector<std::vector<float>> aeRecon_;
    std::vector<std::vector<float>> basisRecon_;

    // Layout
    float panelL_=0,panelT_=0,panelW_=0,panelH_=0;
    float tileW_=0,tileH_=0,tileGap_=10;
    float row1T_=0,row2T_=0,row3T_=0;

    WaveLatent wl_;
};

ALICE2_REGISTER_SKETCH_AUTO(Sketch_WaveLatent_Compare)

#endif // __MAIN__

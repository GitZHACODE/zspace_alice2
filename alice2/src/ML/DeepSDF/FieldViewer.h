#pragma once
// FieldViewer — draws grid fields (original / reconstruction) for Latent SDF
// Depends only on alice2 + the GridField/FieldDomain types defined here.

#include <alice2.h>
#include <vector>
#include <algorithm>
#include <limits>
#include <cmath>

namespace DeepSDF {

// ----------------- Types shared with the trainer -----------------
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

// --------------- Visual-only utilities ---------------
inline float sigmoid01(float x) { return 1.0f / (1.0f + std::exp(-x)); }
inline float softMask01(float sdf, float tau) { return sigmoid01(sdf / tau); }
inline alice2::Color valueToGray(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return alice2::Color(t, t, t);
}

// --------------- Viewer ---------------
class FieldViewer {
public:
    struct ViewConfig {
        bool  showMask   = true;   // keep existing naming
        bool  softMask   = true;   // blurred vs sharp
        float tau        = 0.05f;  // softness
        float tileSize   = 220.0f; // per-field preview tile
        // 0=Mask, 1=TrueField, 2=Recon (heat), 3=Diff (Recon-True; recon row only)
        int   debugMode  = 0;
    };

    FieldViewer() = default;

    // Bind data sources (non-owning pointers)
    FieldViewer& setDomain(const FieldDomain* dom)                  { domain_ = dom; return *this; }
    FieldViewer& setOriginal(const std::vector<GridField>* orig)    { original_ = orig; return *this; }
    FieldViewer& setReconstructed(const std::vector<GridField>* rc) { recon_ = rc; return *this; }

    // Access to config (for your sketch hotkeys/UI)
    ViewConfig&       config()       { return cfg_; }
    const ViewConfig& config() const { return cfg_; }

    // Drawing API (identical signatures to your previous façade methods)
    void drawOriginalRow(alice2::Renderer& r, float top) const {
        if (!domain_ || !original_) return;
        r.setColor(alice2::Color(0.9f, 0.9f, 0.9f));
        r.drawString("Original (analytic labels)", 20.0f, top - 8.0f);
        drawFieldRow(r, *original_, top, /*isRecon*/false);
    }

    void drawReconstructionRow(alice2::Renderer& r, float top) const {
        if (!domain_ || !recon_) return;
        r.setColor(alice2::Color(0.9f, 0.9f, 0.9f));
        r.drawString("Reconstructed (decoder output)", 20.0f, top - 8.0f);
        drawFieldRow(r, *recon_, top, /*isRecon*/true);
    }

    void drawHelp(alice2::Renderer& r, float y) const {
        r.setColor(alice2::Color(0.7f, 0.7f, 0.7f));
        r.drawString(
            "Hotkeys: [T] train  [R] recon  [M] soft/hard mask  [ / ] tau",
            20.0f, y);
        r.drawString(
            "1=Mask  2=True  3=Recon  4=Diff  (J=save JSON, L=load JSON, G=gen default)",
            20.0f, y + 20.0f);
    }

private:
    // Row of thumbnails
    void drawFieldRow(alice2::Renderer& renderer, const std::vector<GridField>& grids,
                      float top, bool isRecon) const
    {
        if (!domain_ || grids.empty() || domain_->resX <= 0 || domain_->resY <= 0) return;

        const float gap   = 25.0f;
        const float cellW = cfg_.tileSize / float(domain_->resX);
        const float cellH = cfg_.tileSize / float(domain_->resY);

        for (size_t fieldIdx = 0; fieldIdx < grids.size(); ++fieldIdx) {
            const float left = 20.0f + float(fieldIdx) * (cfg_.tileSize + gap);
            drawField(renderer, grids[fieldIdx], left, top, cellW, cellH, isRecon, (int)fieldIdx);
            renderer.setColor(alice2::Color(0.7f, 0.7f, 0.9f));
            renderer.drawString("#" + std::to_string(fieldIdx), left, top + cfg_.tileSize + 16.0f);
        }
    }

    // One tile according to current debug mode
    void drawField(alice2::Renderer& renderer, const GridField& field,
                   float left, float top, float cellW, float cellH,
                   bool isRecon, int shapeIdx) const
    {
        const int mode = cfg_.debugMode; // 0=Mask, 1=TrueField, 2=Recon(heat), 3=Diff

        // Fetch reference (true) field when needed
        const GridField* trueField = nullptr;
        if ((mode == 1 || mode == 3) && original_ && shapeIdx >= 0 &&
            shapeIdx < (int)original_->size())
        {
            trueField = &(*original_)[(size_t)shapeIdx];
        }

        if (mode == 3 && !isRecon) {
            // Diff mode: only on reconstruction row; use mask for the original row to avoid confusion.
            drawFieldMaskOrHeat(renderer, field, left, top, cellW, cellH, /*forceMask*/cfg_.showMask);
            return;
        }

        if (mode == 0) {
            drawFieldMaskOrHeat(renderer, field, left, top, cellW, cellH, /*forceMask*/true);
            return;
        }
        if (mode == 1) {
            // True SDF coloring
            if (trueField) drawFieldContinuous(renderer, *trueField, left, top, cellW, cellH);
            else           drawFieldContinuous(renderer, field,      left, top, cellW, cellH);
            return;
        }
        if (mode == 2) {
            drawFieldContinuous(renderer, field, left, top, cellW, cellH);
            return;
        }
        if (mode == 3) {
            if (trueField) drawFieldDifference(renderer, field, *trueField, left, top, cellW, cellH);
            else           drawFieldContinuous(renderer, field, left, top, cellW, cellH);
            return;
        }
    }

    // Mask look (soft/hard), or heatmap if forced off
    void drawFieldMaskOrHeat(alice2::Renderer& renderer, const GridField& field,
                             float left, float top, float cellW, float cellH,
                             bool forceMask) const
    {
        if (!forceMask && !cfg_.showMask) {
            drawFieldContinuous(renderer, field, left, top, cellW, cellH);
            return;
        }

        for (int y = 0; y < domain_->resY; ++y) {
            for (int x = 0; x < domain_->resX; ++x) {
                const size_t idx = size_t(y) * size_t(domain_->resX) + size_t(x);
                const float sdf  = field.values[idx];
                float v01 = cfg_.softMask ? softMask01(sdf, cfg_.tau)
                                          : ((sdf < 0.0f) ? 0.0f : 1.0f);
                const alice2::Color color = valueToGray(v01);
                const float px = left + (float(x) + 0.5f) * cellW;
                const float py = top  + (float(y) + 0.5f) * cellH;
                const float pointSize = std::max(cellW, cellH) * 0.8f;
                renderer.draw2dPoint(alice2::Vec2(px, py), color, pointSize);
            }
        }
    }

    // Continuous grayscale heatmap
    void drawFieldContinuous(alice2::Renderer& renderer, const GridField& field,
                             float left, float top, float cellW, float cellH) const
    {
        const float safeMin = field.minValue;
        const float safeMax = field.maxValue;
        const float range   = (safeMax - safeMin == 0.0f) ? 1.0f : (safeMax - safeMin);

        for (int y = 0; y < domain_->resY; ++y) {
            for (int x = 0; x < domain_->resX; ++x) {
                const size_t idx = size_t(y) * size_t(domain_->resX) + size_t(x);
                const float value = field.values[idx];
                const float norm  = (value - safeMin) / range;
                const alice2::Color color = valueToGray(norm);
                const float px = left + (float(x) + 0.5f) * cellW;
                const float py = top  + (float(y) + 0.5f) * cellH;
                const float pointSize = std::max(cellW, cellH) * 0.8f;
                renderer.draw2dPoint(alice2::Vec2(px, py), color, pointSize);
            }
        }
    }

    // Difference heatmap: clamp to [-1,1] then map to [0,1]
    void drawFieldDifference(alice2::Renderer& renderer, const GridField& recon, const GridField& truth,
                             float left, float top, float cellW, float cellH) const
    {
        for (int y = 0; y < domain_->resY; ++y) {
            for (int x = 0; x < domain_->resX; ++x) {
                const size_t idx = size_t(y) * size_t(domain_->resX) + size_t(x);
                float d = recon.values[idx] - truth.values[idx];
                d = std::clamp(d, -1.0f, 1.0f);
                float v01 = 0.5f * (d + 1.0f); // [-1,1] -> [0,1]
                const alice2::Color color = valueToGray(v01);
                const float px = left + (float(x) + 0.5f) * cellW;
                const float py = top  + (float(y) + 0.5f) * cellH;
                const float pointSize = std::max(cellW, cellH) * 0.8f;
                renderer.draw2dPoint(alice2::Vec2(px, py), color, pointSize);
            }
        }
    }

private:
    const FieldDomain*               domain_   = nullptr;
    const std::vector<GridField>*    original_ = nullptr;
    const std::vector<GridField>*    recon_    = nullptr;

    ViewConfig cfg_;
};

} // namespace DeepSDF

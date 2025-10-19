#define ALICE2_USE_CUDA

#ifdef ALICE2_USE_CUDA

#include "LatentNavigator_CUDA.h"

#include <cuda_runtime.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <array>
#include <limits>

namespace DeepSDF {

namespace {
inline void checkCuda(const char* call, cudaError_t status) {
    if (status != cudaSuccess) {
        std::fprintf(stderr, "[LatentNavigator_CUDA] %s failed: %s\n",
                     call, cudaGetErrorString(status));
        std::abort();
    }
}
}

LatentNavigator_CUDA::LatentNavigator_CUDA() = default;
LatentNavigator_CUDA::~LatentNavigator_CUDA() { shutdown(); }

void LatentNavigator_CUDA::initialize(TinyAutoDecoderCUDA* decoder,
                                      const FieldDomain* domain,
                                      const std::vector<std::vector<float>>* latentSource)
{
    decoder_ = decoder;
    domain_  = domain;
    latents_ = latentSource;
    updateLatentEmbedding();
    markPanelDirty();
}

void LatentNavigator_CUDA::shutdown()
{
    if (panelResource_) {
        cudaGraphicsUnregisterResource(panelResource_);
        panelResource_ = nullptr;
    }
    if (panelTexture_ != 0) {
        glDeleteTextures(1, &panelTexture_);
        panelTexture_ = 0;
    }
    if (dPanelField_)   { checkCuda("cudaFree(panelField)", cudaFree(dPanelField_));   dPanelField_ = nullptr; }
    if (dPanelRGBA_)    { checkCuda("cudaFree(panelRGBA)",  cudaFree(dPanelRGBA_));    dPanelRGBA_ = nullptr; }
    if (dScratchLatent_){ checkCuda("cudaFree(scratchLatent)", cudaFree(dScratchLatent_)); dScratchLatent_ = nullptr; }
    if (dTileMin_)      { checkCuda("cudaFree(tileMin)", cudaFree(dTileMin_)); dTileMin_ = nullptr; }
    if (dTileMax_)      { checkCuda("cudaFree(tileMax)", cudaFree(dTileMax_)); dTileMax_ = nullptr; }
    if (dFieldScratch_) { checkCuda("cudaFree(fieldScratch)", cudaFree(dFieldScratch_)); dFieldScratch_ = nullptr; }
    fieldScratchRes_ = 0;
    tileCapacity_ = 0;
    decoder_ = nullptr;
    domain_  = nullptr;
    latents_ = nullptr;
}

void LatentNavigator_CUDA::setPanelResolution(int N, int tileRes, int gap)
{
    panelN_  = std::max(1, N);
    tileRes_ = std::max(4, tileRes);
    tileGap_ = std::max(0, gap);
    markPanelDirty();
}

void LatentNavigator_CUDA::setLatentSource(const std::vector<std::vector<float>>* latentSource)
{
    latents_ = latentSource;
    updateLatentEmbedding();
    markPanelDirty();
}

void LatentNavigator_CUDA::setDetailResolution(int /*res*/)
{
    // reserved for future
}

void LatentNavigator_CUDA::markPanelDirty()
{
    panelDirty_ = true;
}

void LatentNavigator_CUDA::updateLatentEmbedding()
{
    latentCoords2D_.clear();
    latentBBoxMin_ = Coord2{};
    latentBBoxMax_ = Coord2{};
    latentSampleMin_ = Coord2{-1.0f, -1.0f};
    latentSampleMax_ = Coord2{1.0f, 1.0f};
    latentSampleRange_ = Coord2{2.0f, 2.0f};

    if (!latents_ || latents_->empty()) return;

    const int latentCount = static_cast<int>(latents_->size());
    int latentDim = 0;
    if (decoder_) latentDim = decoder_->latentDim();
    if (latentDim <= 0 && latentCount > 0) {
        latentDim = static_cast<int>((*latents_)[0].size());
    }
    if (latentDim <= 0) return;

    latentCoords2D_.reserve(latentCount);

    Coord2 minV{ std::numeric_limits<float>::max(),  std::numeric_limits<float>::max() };
    Coord2 maxV{-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};

    for (const auto& latent : *latents_) {
        float x = (latentDim > 0 && latent.size() > 0) ? latent[0] : 0.0f;
        float y = (latentDim > 1 && latent.size() > 1) ? latent[1] : 0.0f;
        latentCoords2D_.push_back(Coord2{x, y});
        minV.x = std::min(minV.x, x);
        minV.y = std::min(minV.y, y);
        maxV.x = std::max(maxV.x, x);
        maxV.y = std::max(maxV.y, y);
    }

    if (latentCoords2D_.empty()) return;

    latentBBoxMin_ = minV;
    latentBBoxMax_ = maxV;

    float spanX = maxV.x - minV.x;
    float spanY = maxV.y - minV.y;

    float padX = std::max(spanX * 0.05f, 0.01f);
    float padY = std::max(spanY * 0.05f, 0.01f);

    latentSampleMin_.x = minV.x - padX;
    latentSampleMin_.y = minV.y - padY;
    latentSampleMax_.x = maxV.x + padX;
    latentSampleMax_.y = maxV.y + padY;

    latentSampleRange_.x = latentSampleMax_.x - latentSampleMin_.x;
    latentSampleRange_.y = latentSampleMax_.y - latentSampleMin_.y;

    if (latentSampleRange_.x < 1e-5f) {
        latentSampleMin_.x -= 0.5f;
        latentSampleMax_.x += 0.5f;
        latentSampleRange_.x = latentSampleMax_.x - latentSampleMin_.x;
    }
    if (latentSampleRange_.y < 1e-5f) {
        latentSampleMin_.y -= 0.5f;
        latentSampleMax_.y += 0.5f;
        latentSampleRange_.y = latentSampleMax_.y - latentSampleMin_.y;
    }
}

bool LatentNavigator_CUDA::ensurePanelResources()
{
    if (!decoder_ || !domain_) return false;

    const int newW = panelN_ * tileRes_ + (panelN_ - 1) * tileGap_;
    const int newH = newW;
    if (newW <= 0 || newH <= 0) return false;

    const size_t fieldBytes = size_t(newW) * size_t(newH) * sizeof(float);
    if (!dPanelField_ || panelW_ != newW || panelH_ != newH) {
        if (dPanelField_) checkCuda("cudaFree(panelField)", cudaFree(dPanelField_));
        checkCuda("cudaMalloc(panelField)", cudaMalloc(&dPanelField_, fieldBytes));
        panelDirty_ = true;
    }
    if (!dPanelRGBA_ || panelW_ != newW || panelH_ != newH) {
        if (dPanelRGBA_) checkCuda("cudaFree(panelRGBA)", cudaFree(dPanelRGBA_));
        checkCuda("cudaMalloc(panelRGBA)", cudaMalloc(&dPanelRGBA_, size_t(newW)*size_t(newH)*sizeof(uchar4)));
        panelDirty_ = true;
    }

    if (!dScratchLatent_) {
        checkCuda("cudaMalloc(scratchLatent)", cudaMalloc(&dScratchLatent_, size_t(decoder_->latentDim()) * sizeof(float)));
    }

    const int tileCount = panelN_ * panelN_;
    bool resizedTiles = false;
    if (!dTileMin_ || tileCapacity_ < tileCount) {
        if (dTileMin_) checkCuda("cudaFree(tileMin)", cudaFree(dTileMin_));
        checkCuda("cudaMalloc(tileMin)", cudaMalloc(&dTileMin_, size_t(tileCount) * sizeof(float)));
        resizedTiles = true;
    }
    if (!dTileMax_ || tileCapacity_ < tileCount) {
        if (dTileMax_) checkCuda("cudaFree(tileMax)", cudaFree(dTileMax_));
        checkCuda("cudaMalloc(tileMax)", cudaMalloc(&dTileMax_, size_t(tileCount) * sizeof(float)));
        resizedTiles = true;
    }
    if (resizedTiles) tileCapacity_ = tileCount;

    if (panelTexture_ == 0 || panelW_ != newW || panelH_ != newH) {
        if (panelResource_) {
            cudaGraphicsUnregisterResource(panelResource_);
            panelResource_ = nullptr;
        }
        if (panelTexture_ != 0) {
            glDeleteTextures(1, &panelTexture_);
            panelTexture_ = 0;
        }

        glGenTextures(1, &panelTexture_);
        glBindTexture(GL_TEXTURE_2D, panelTexture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, newW, newH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);

        checkCuda("cudaGraphicsGLRegisterImage",
                  cudaGraphicsGLRegisterImage(&panelResource_, panelTexture_, GL_TEXTURE_2D,
                                              cudaGraphicsRegisterFlagsWriteDiscard));
    }

    panelW_ = newW;
    panelH_ = newH;

    if (scratchLatentHost_.size() != size_t(decoder_->latentDim())) {
        scratchLatentHost_.assign(size_t(decoder_->latentDim()), 0.0f);
    }

    return true;
}

bool LatentNavigator_CUDA::computeLatentBlend(float cx, float cy, std::vector<float>& outLatent)
{
    if (!decoder_ || !latents_ || latents_->empty()) return false;

    if (latentCoords2D_.size() != latents_->size()) {
        updateLatentEmbedding();
        if (latentCoords2D_.size() != latents_->size()) return false;
    }

    const int latentDim = decoder_->latentDim();
    if (latentDim <= 0) return false;

    if (static_cast<int>(outLatent.size()) != latentDim) {
        outLatent.assign(size_t(latentDim), 0.0f);
    } else {
        std::fill(outLatent.begin(), outLatent.end(), 0.0f);
    }

    const int count = static_cast<int>(latentCoords2D_.size());
    if (count == 0) return false;

    constexpr int kNearest = 4;
    std::array<int, kNearest> indices{};
    std::array<float, kNearest> distances{};
    indices.fill(-1);
    distances.fill(std::numeric_limits<float>::max());

    int closestIdx = -1;
    float closestDist = std::numeric_limits<float>::max();

    for (int i = 0; i < count; ++i) {
        const Coord2& c = latentCoords2D_[size_t(i)];
        const float dx = cx - c.x;
        const float dy = cy - c.y;
        const float distSq = dx * dx + dy * dy;

        if (distSq < closestDist) {
            closestDist = distSq;
            closestIdx = i;
        }

        for (int k = 0; k < kNearest; ++k) {
            if (distSq < distances[k]) {
                for (int s = kNearest - 1; s > k; --s) {
                    distances[s] = distances[s - 1];
                    indices[s] = indices[s - 1];
                }
                distances[k] = distSq;
                indices[k] = i;
                break;
            }
        }
    }

    if (closestIdx >= 0 && closestDist < 1e-8f) {
        const auto& latent = (*latents_)[size_t(closestIdx)];
        if (static_cast<int>(latent.size()) != latentDim) return false;
        std::copy_n(latent.begin(), latentDim, outLatent.begin());
        return true;
    }

    float weightSum = 0.0f;
    for (int k = 0; k < kNearest; ++k) {
        const int idx = indices[k];
        if (idx < 0) continue;
        float distSq = distances[k];
        float weight = 0.0f;
        if (distSq < 1e-12f) {
            weight = 1.0f;
        } else {
            weight = 1.0f / (distSq + 1e-6f);
        }
        const auto& latent = (*latents_)[size_t(idx)];
        if (static_cast<int>(latent.size()) != latentDim) continue;
        for (int j = 0; j < latentDim; ++j) {
            outLatent[size_t(j)] += weight * latent[size_t(j)];
        }
        weightSum += weight;
    }

    if (weightSum <= 0.0f) {
        if (closestIdx >= 0) {
            const auto& latent = (*latents_)[size_t(closestIdx)];
            if (static_cast<int>(latent.size()) != latentDim) return false;
            std::copy_n(latent.begin(), latentDim, outLatent.begin());
            return true;
        }
        return false;
    }

    const float invTotal = 1.0f / weightSum;
    for (int j = 0; j < latentDim; ++j) {
        outLatent[size_t(j)] *= invTotal;
    }
    return true;
}

bool LatentNavigator_CUDA::rebuildPanel(const FieldRenderConfig& cfg)
{
    if (!latents_ || latents_->empty()) return false;

    updateLatentEmbedding();

    const auto& allLatents = *latents_;
    const int latentCount = static_cast<int>(allLatents.size());

    int requiredPanelN = panelN_;
    if (latentCount > panelN_ * panelN_) {
        requiredPanelN = static_cast<int>(std::ceil(std::sqrt(static_cast<float>(latentCount))));
    }
    if (requiredPanelN != panelN_) {
        panelN_ = std::max(1, requiredPanelN);
        panelDirty_ = true;
    }

    if (!ensurePanelResources()) return false;
    if (decoder_->latentDim() <= 0) return false;

    const int latentDim = decoder_->latentDim();

    if (scratchLatentHost_.size() != size_t(latentDim)) {
        scratchLatentHost_.assign(size_t(latentDim), 0.0f);
    }

    for (int gy = 0; gy < panelN_; ++gy) {
        for (int gx = 0; gx < panelN_; ++gx) {
            const float u = (panelN_ == 1) ? 0.5f : float(gx) / float(panelN_ - 1);
            const float v = (panelN_ == 1) ? 0.5f : float(gy) / float(panelN_ - 1);
            const float cx = latentSampleMin_.x + u * latentSampleRange_.x;
            const float cy = latentSampleMin_.y + v * latentSampleRange_.y;

            const float* latentData = nullptr;
            if (computeLatentBlend(cx, cy, scratchLatentHost_)) {
                latentData = scratchLatentHost_.data();
            } else {
                const int fallbackIdx = std::min(gy * panelN_ + gx, latentCount - 1);
                const auto& latent = allLatents[size_t(fallbackIdx)];
                if (static_cast<int>(latent.size()) == latentDim) {
                    latentData = latent.data();
                } else {
                    std::fill(scratchLatentHost_.begin(), scratchLatentHost_.end(), 0.0f);
                    latentData = scratchLatentHost_.data();
                }
            }

            const int offsetX = gx * tileRes_ + std::max(0, gx) * tileGap_;
            const int offsetY = gy * tileRes_ + std::max(0, gy) * tileGap_;

            decoder_->decodeLatentGridToDevice(latentData,
                                               tileRes_, tileRes_,
                                               domain_->xMin, domain_->xMax,
                                               domain_->yMin, domain_->yMax,
                                               dPanelField_,
                                               panelW_,
                                               offsetX,
                                               offsetY,
                                               dScratchLatent_);
        }
    }

    decoder_->panelToRGBA(dPanelField_, panelW_, panelH_,
                          tileRes_, tileGap_, panelN_,
                          cfg, dTileMin_, dTileMax_, dPanelRGBA_);

    checkCuda("cudaGraphicsMapResources", cudaGraphicsMapResources(1, &panelResource_));
    cudaArray_t array = nullptr;
    checkCuda("cudaGraphicsSubResourceGetMappedArray",
              cudaGraphicsSubResourceGetMappedArray(&array, panelResource_, 0, 0));

    checkCuda("cudaMemcpy2DToArray",
              cudaMemcpy2DToArray(array, 0, 0,
                                  dPanelRGBA_, panelW_ * sizeof(uchar4),
                                  panelW_ * sizeof(uchar4), panelH_,
                                  cudaMemcpyDeviceToDevice));

    checkCuda("cudaGraphicsUnmapResources", cudaGraphicsUnmapResources(1, &panelResource_));

    lastPanelCfg_ = cfg;
    panelDirty_ = false;
    return true;
}

bool LatentNavigator_CUDA::drawPanel(alice2::Renderer& renderer,
                                     float left, float top, float size,
                                     const FieldRenderConfig& cfg)
{
    if (!decoder_ || !domain_ || !latents_ || latents_->empty()) return false;
    if (!ensurePanelResources()) return false;
    if (panelDirty_ || cfg.debugMode != lastPanelCfg_.debugMode ||
        cfg.softMask != lastPanelCfg_.softMask ||
        std::fabs(cfg.tau - lastPanelCfg_.tau) > 1e-6f) {
        if (!rebuildPanel(cfg)) return false;
    }

    int vx, vy, vw, vh;
    renderer.getViewport(vx, vy, vw, vh);

    const GLboolean depthTestWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    GLboolean depthMaskWasEnabled = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMaskWasEnabled);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, vw, vh, 0, -1, 1);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, panelTexture_);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    const float right  = left + size;
    const float bottom = top  + size;

    glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(left,  top);
        glTexCoord2f(1.0f, 0.0f); glVertex2f(right, top);
        glTexCoord2f(1.0f, 1.0f); glVertex2f(right, bottom);
        glTexCoord2f(0.0f, 1.0f); glVertex2f(left,  bottom);
    glEnd();

    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);

    glDepthMask(depthMaskWasEnabled);
    if (depthTestWasEnabled) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    return true;
}

bool LatentNavigator_CUDA::decodeLatent(const std::vector<float>& latent, GridField& out)
{
    if (!decoder_ || !domain_) return false;
    if ((int)latent.size() != decoder_->latentDim()) return false;
    if (!dScratchLatent_) {
        checkCuda("cudaMalloc(scratchLatent)", cudaMalloc(&dScratchLatent_, size_t(decoder_->latentDim()) * sizeof(float)));
    }
    if (!dFieldScratch_ || fieldScratchRes_ != domain_->resX * domain_->resY) {
        if (dFieldScratch_) checkCuda("cudaFree(fieldScratch)", cudaFree(dFieldScratch_));
        fieldScratchRes_ = domain_->resX * domain_->resY;
        checkCuda("cudaMalloc(fieldScratch)", cudaMalloc(&dFieldScratch_, size_t(fieldScratchRes_) * sizeof(float)));
    }

    decoder_->decodeLatentGridToDevice(latent.data(),
                                       domain_->resX,
                                       domain_->resY,
                                       domain_->xMin, domain_->xMax,
                                       domain_->yMin, domain_->yMax,
                                       dFieldScratch_,
                                       domain_->resX,
                                       0, 0,
                                       dScratchLatent_);

    std::vector<float> host(fieldScratchRes_);
    checkCuda("cudaMemcpy(fieldScratch)", cudaMemcpy(host.data(), dFieldScratch_,
                                                    size_t(fieldScratchRes_) * sizeof(float),
                                                    cudaMemcpyDeviceToHost));

    out.values = host;
    out.minValue = *std::min_element(host.begin(), host.end());
    out.maxValue = *std::max_element(host.begin(), host.end());
    return true;
}

bool LatentNavigator_CUDA::getFieldAt(const std::vector<float>& latent, GridField& out)
{
    return decodeLatent(latent, out);
}

bool LatentNavigator_CUDA::getBlendedField(float u, float v, GridField& out)
{
    if (!latents_ || latents_->empty()) return false;
    if (!decoder_) return false;

    updateLatentEmbedding();

    const float eps = std::numeric_limits<float>::epsilon();
    const float uClamped = std::clamp(u, 0.0f, 1.0f - eps);
    const float vClamped = std::clamp(v, 0.0f, 1.0f - eps);

    const float cx = latentSampleMin_.x + uClamped * latentSampleRange_.x;
    const float cy = latentSampleMin_.y + vClamped * latentSampleRange_.y;

    if (!computeLatentBlend(cx, cy, scratchLatentHost_)) {
        if (latents_->empty()) return false;
        const auto& fallback = latents_->front();
        if (static_cast<int>(fallback.size()) != decoder_->latentDim()) return false;
        return decodeLatent(fallback, out);
    }
    return decodeLatent(scratchLatentHost_, out);
}

void LatentNavigator_CUDA::drawField(alice2::Renderer& renderer,
                                     const GridField& field,
                                     float left, float top, float size,
                                     const FieldRenderConfig& cfg) const
{
    if (!domain_) return;
    const int resX = domain_->resX;
    const int resY = domain_->resY;
    if (resX <= 0 || resY <= 0) return;
    if (field.values.size() < size_t(resX) * size_t(resY)) return;

    const float cellW = size / float(resX);
    const float cellH = size / float(resY);

    for (int y = 0; y < resY; ++y) {
        for (int x = 0; x < resX; ++x) {
            const size_t idx = size_t(y) * size_t(resX) + size_t(x);
            const float sdf = field.values[idx];
            float g = 0.5f;
            if (cfg.debugMode == 0) {
                if (cfg.softMask) {
                    const float tau = std::max(cfg.tau, 1e-6f);
                    g = 1.0f / (1.0f + std::exp(-(sdf / tau)));
                } else {
                    g = sdf < 0.0f ? 0.0f : 1.0f;
                }
            } else {
                const float range = (field.maxValue - field.minValue == 0.0f) ? 1.0f : (field.maxValue - field.minValue);
                g = std::clamp((sdf - field.minValue) / range, 0.0f, 1.0f);
            }
            const alice2::Color color(g, g, g);
            const float px = left + (float(x) + 0.5f) * cellW;
            const float py = top  + (float(y) + 0.5f) * cellH;
            const float ps = std::max(cellW, cellH);
            renderer.draw2dPoint(alice2::Vec2(px, py), color, ps);
        }
    }
}

} // namespace DeepSDF

#endif // ALICE2_USE_CUDA

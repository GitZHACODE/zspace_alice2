#include "ML/WaveLatent/WaveLatentNavigator.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace {
    const alice2::Color kIsoColor(0.10f, 0.35f, 0.85f, 1.0f);
    const alice2::Color kFrameColor(0.18f, 0.18f, 0.2f, 1.0f);
}

namespace alice2 {

namespace {
    float clamp01(float v) {
        return std::max(0.0f, std::min(1.0f, v));
    }

    float lerpScalar(float a, float b, float t) {
        return a + (b - a) * t;
    }
}

WaveLatentNavigator::WaveLatentNavigator() = default;

bool WaveLatentNavigator::initialize(const Config& config, const Callbacks& callbacks) {
    config_ = config;
    callbacks_ = callbacks;
    ui_ = config_.ui;

    panelResolution_ = std::clamp(config_.initialPanelResolution, config_.minPanelResolution, config_.maxPanelResolution);
    latentSnapRadius_ = config_.latentSnapRadius;
    genLayersF_ = config_.defaultGenLayers;

    if (!loadModelFile()) {
        ready_ = false;
        return false;
    }

    boundsMin_ = Vec3(waveLatent_.domainXMin(), waveLatent_.domainYMin(), 0.0f);
    boundsMax_ = Vec3(waveLatent_.domainXMax(), waveLatent_.domainYMax(), 0.0f);

    configureFields();
    setupLatentSpace();
    buildPanelTiles();
    updateDetailField(currentUV01_);

    if (ui_) {
        ui_->addSlider("Gen Layers", config_.uiGenSliderPos, config_.uiGenSliderWidth,
                       2.0f, 200.0f, genLayersF_);
    }

    ready_ = true;
    if (callbacks_.onStatusChanged) {
        callbacks_.onStatusChanged("Navigator ready");
    }
    return true;
}

void WaveLatentNavigator::shutdown() {
    panelFields_.clear();
    latents_.clear();
    sampleUV_.clear();
    pcaMean_.clear();
    pcaE1_.clear();
    pcaE2_.clear();
    pickedUVs_.clear();
    pickedScreenPts_.clear();
    hoverUV01_.reset();
    lastDetailLatentIndex_.reset();
    ready_ = false;
}

void WaveLatentNavigator::update() {
    if (!ready_) {
        return;
    }
    genLayersF_ = std::clamp(genLayersF_, 2.0f, 200.0f);
}

void WaveLatentNavigator::draw(Renderer& renderer) {
    if (!ready_) {
        return;
    }
    drawDetail(renderer);
    drawPanel(renderer);
}

bool WaveLatentNavigator::handleMouseMove(int x, int y) {
    if (!ready_) {
        return false;
    }
    auto uv = panelUVFromMouse(float(x), float(y));
    if (!uv) {
        hoverUV01_.reset();
        return false;
    }

    hoverUV01_ = *uv;
    updateDetailField(*uv);
    return true;
}

bool WaveLatentNavigator::handleMousePress(int button, int state, int x, int y) {
    if (!ready_) {
        return false;
    }
    if (state != 0) {
        return false;
    }

    auto uv = panelUVFromMouse(float(x), float(y));
    if (!uv) {
        return false;
    }

    currentUV01_ = *uv;
    hoverUV01_ = *uv;
    updateDetailField(*uv);

    pickedUVs_.push_back(*uv);
    const float px = config_.panelOrigin.x + uv->x * config_.panelSize;
    const float py = config_.panelOrigin.y + uv->y * config_.panelSize;
    pickedScreenPts_.emplace_back(px, py);

    if (!callbacks_.onSliceDecoded) {
        if (callbacks_.onStatusChanged) {
            callbacks_.onStatusChanged("Slice decode callback missing");
        }
        return true;
    }

    auto slice = decodeSliceAtUV(*uv);
    if (!slice) {
        if (callbacks_.onStatusChanged) {
            callbacks_.onStatusChanged("Decode failed");
        }
        return true;
    }

    callbacks_.onSliceDecoded(std::move(*slice));
    if (callbacks_.onStatusChanged) {
        std::ostringstream oss;
        oss << "Slice +" << pickedUVs_.size();
        callbacks_.onStatusChanged(oss.str());
    }
    return true;
}

bool WaveLatentNavigator::handleKeyPress(unsigned char key) {
    if (!ready_) {
        return false;
    }
    switch (key) {
    case '[':
        setPanelResolution(panelResolution_ - 1);
        return true;
    case ']':
        setPanelResolution(panelResolution_ + 1);
        return true;
    case 'g':
    case 'G': {
        if (!callbacks_.onStackGenerated) {
            return false;
        }
        auto generated = generateInterpolatedStack();
        if (generated.empty()) {
            if (callbacks_.onStatusChanged) {
                callbacks_.onStatusChanged("No stack generated");
            }
            return true;
        }
        callbacks_.onStackGenerated(std::move(generated));
        if (callbacks_.onStatusChanged) {
            std::ostringstream oss;
            oss << "Stack " << pickedUVs_.size() << "→" << genLayersF_;
            callbacks_.onStatusChanged(oss.str());
        }
        return true;
    }
    case 'c':
    case 'C':
        clearSelection();
        if (callbacks_.onClearRequested) {
            callbacks_.onClearRequested();
        }
        if (callbacks_.onStatusChanged) {
            callbacks_.onStatusChanged("Cleared");
        }
        return true;
    default:
        return false;
    }
}

void WaveLatentNavigator::setPanelResolution(int resolution) {
    const int clamped = std::clamp(resolution, config_.minPanelResolution, config_.maxPanelResolution);
    if (clamped == panelResolution_) {
        return;
    }
    panelResolution_ = clamped;
    configureFields();
    buildPanelTiles();
    updateDetailField(currentUV01_);
    if (callbacks_.onStatusChanged) {
        std::ostringstream oss;
        oss << "Panel " << panelResolution_ << "x" << panelResolution_;
        callbacks_.onStatusChanged(oss.str());
    }
}

void WaveLatentNavigator::clearSelection() {
    pickedUVs_.clear();
    pickedScreenPts_.clear();
}

void WaveLatentNavigator::configureFields() {
    const int gridW = waveLatent_.gridWidth();
    const int gridH = waveLatent_.gridHeight();
    if (gridW <= 0 || gridH <= 0) {
        ready_ = false;
        if (callbacks_.onStatusChanged) {
            callbacks_.onStatusChanged("Invalid grid size");
        }
        return;
    }

    detailField_.configure(gridW, gridH,
                           waveLatent_.domainXMin(), waveLatent_.domainXMax(),
                           waveLatent_.domainYMin(), waveLatent_.domainYMax());

    panelFields_.assign(panelResolution_ * panelResolution_, {});
    for (auto& field : panelFields_) {
        field.configure(gridW, gridH,
                        waveLatent_.domainXMin(), waveLatent_.domainXMax(),
                        waveLatent_.domainYMin(), waveLatent_.domainYMax());
    }
}

void WaveLatentNavigator::setupLatentSpace() {
    latents_ = waveLatent_.storedLatents();
    if (latents_.empty() && modelJson_.contains("latents")) {
        const auto& arr = modelJson_["latents"];
        if (arr.is_array()) {
            latents_.reserve(arr.size());
            for (const auto& entry : arr) {
                if (!entry.is_array()) {
                    continue;
                }
                std::vector<float> z;
                z.reserve(entry.size());
                for (const auto& v : entry) {
                    z.push_back(v.get<float>());
                }
                latents_.push_back(std::move(z));
            }
        }
    }
    computePCA();
}

void WaveLatentNavigator::computePCA() {
    const int latentDim = waveLatent_.latentDim();
    if (latentDim <= 0) {
        return;
    }
    if (latents_.empty()) {
        pcaMean_.assign(latentDim, 0.0f);
        pcaE1_.assign(latentDim, 0.0f);
        pcaE2_.assign(latentDim, 0.0f);
        if (latentDim > 0) pcaE1_[0] = 1.0f;
        if (latentDim > 1) pcaE2_[1] = 1.0f;
        uMin_ = -2.5f;
        uMax_ =  2.5f;
        vMin_ = -2.5f;
        vMax_ =  2.5f;
        sampleUV_.clear();
        return;
    }

    const int dim = static_cast<int>(latents_.front().size());
    pcaMean_.assign(dim, 0.0f);
    for (const auto& z : latents_) {
        for (int d = 0; d < dim; ++d) {
            pcaMean_[d] += z[d];
        }
    }
    const float invCount = 1.0f / std::max<size_t>(1, latents_.size());
    for (float& v : pcaMean_) {
        v *= invCount;
    }

    std::vector<float> covariance(dim * dim, 0.0f);
    for (const auto& z : latents_) {
        for (int i = 0; i < dim; ++i) {
            const float zi = z[i] - pcaMean_[i];
            for (int j = 0; j < dim; ++j) {
                covariance[i * dim + j] += zi * (z[j] - pcaMean_[j]);
            }
        }
    }
    for (float& c : covariance) {
        c *= invCount;
    }

    pcaE1_.assign(dim, 0.0f);
    pcaE2_.assign(dim, 0.0f);
    float lam1 = 0.0f;
    float lam2 = 0.0f;
    powerEigen(covariance, dim, 48, pcaE1_, lam1);
    deflate(covariance, dim, pcaE1_, lam1);
    powerEigen(covariance, dim, 48, pcaE2_, lam2);

    if (std::all_of(pcaE1_.begin(), pcaE1_.end(), [](float v) { return std::fabs(v) < 1e-5f; }) && dim > 0) {
        pcaE1_.assign(dim, 0.0f);
        pcaE1_[0] = 1.0f;
    }
    if (std::all_of(pcaE2_.begin(), pcaE2_.end(), [](float v) { return std::fabs(v) < 1e-5f; }) && dim > 1) {
        pcaE2_.assign(dim, 0.0f);
        pcaE2_[1] = 1.0f;
    }

    float minU = std::numeric_limits<float>::max();
    float maxU = std::numeric_limits<float>::lowest();
    float minV = std::numeric_limits<float>::max();
    float maxV = std::numeric_limits<float>::lowest();

    sampleUV_.clear();
    sampleUV_.reserve(latents_.size());
    for (const auto& z : latents_) {
        float u = 0.0f;
        float v = 0.0f;
        for (int d = 0; d < dim; ++d) {
            const float centered = z[d] - pcaMean_[d];
            u += centered * pcaE1_[d];
            v += centered * pcaE2_[d];
        }
        sampleUV_.emplace_back(u, v);
        minU = std::min(minU, u);
        maxU = std::max(maxU, u);
        minV = std::min(minV, v);
        maxV = std::max(maxV, v);
    }

    const float padU = 0.1f * std::max(1e-3f, maxU - minU);
    const float padV = 0.1f * std::max(1e-3f, maxV - minV);
    uMin_ = minU - padU;
    uMax_ = maxU + padU;
    vMin_ = minV - padV;
    vMax_ = maxV + padV;
}

void WaveLatentNavigator::buildPanelTiles() {
    const int gridW = waveLatent_.gridWidth();
    const int gridH = waveLatent_.gridHeight();
    if (gridW <= 0 || gridH <= 0) {
        return;
    }

    std::vector<float> decoded;
    decoded.reserve(static_cast<size_t>(gridW) * static_cast<size_t>(gridH));
    for (int row = 0; row < panelResolution_; ++row) {
        for (int col = 0; col < panelResolution_; ++col) {
            const float normU = (col + 0.5f) / std::max(1, panelResolution_);
            const float normV = (row + 0.5f) / std::max(1, panelResolution_);
            const Vec2 plane = planeFrom01(Vec2(normU, normV));
            decodeOnPlane(plane.x, plane.y, decoded);
            const int idx = row * panelResolution_ + col;
            if (idx >= 0 && idx < int(panelFields_.size())) {
                panelFields_[size_t(idx)].updateValues(decoded);
            }
        }
    }
}

void WaveLatentNavigator::updateDetailField(const Vec2& uv01) {
    const int gridW = waveLatent_.gridWidth();
    const int gridH = waveLatent_.gridHeight();
    const size_t expected = static_cast<size_t>(std::max(0, gridW)) * static_cast<size_t>(std::max(0, gridH));

    std::vector<float> decoded;
    decoded.reserve(expected);

    bool usedStoredLatent = false;
    std::optional<size_t> snappedIndex;
    if (auto nearest = nearestLatentIndex(uv01, latentSnapRadius_)) {
        waveLatent_.decodeLatentToGrid(latents_[*nearest], decoded);
        if (decoded.size() == expected && expected > 0) {
            usedStoredLatent = true;
            snappedIndex = *nearest;
        } else {
            decoded.clear();
        }
    }

    if (!usedStoredLatent) {
        const Vec2 plane = planeFrom01(uv01);
        decodeOnPlane(plane.x, plane.y, decoded);
    }

    lastDecoded_ = decoded;
    detailField_.updateValues(decoded);
    lastDetailUV01_ = uv01;
    if (usedStoredLatent) {
        lastDetailLatentIndex_ = snappedIndex;
    } else {
        lastDetailLatentIndex_.reset();
    }
}

void WaveLatentNavigator::decodeOnPlane(float u, float v, std::vector<float>& outValues) const {
    const int dim = waveLatent_.latentDim();
    if (dim <= 0) {
        outValues.clear();
        return;
    }
    std::vector<float> z(dim, 0.0f);
    if (!pcaMean_.empty() && pcaE1_.size() == size_t(dim) && pcaE2_.size() == size_t(dim)) {
        for (int d = 0; d < dim; ++d) {
            z[d] = pcaMean_[d] + u * pcaE1_[d] + v * pcaE2_[d];
        }
    } else {
        const float scale = 2.5f;
        if (dim > 0) z[0] = u * scale;
        if (dim > 1) z[1] = v * scale;
    }
    waveLatent_.decodeLatentToGrid(z, outValues);
}

bool WaveLatentNavigator::loadModelFile() {
    std::ifstream in(config_.modelPath);
    if (!in.is_open()) {
        if (callbacks_.onStatusChanged) {
            callbacks_.onStatusChanged("Model open failed");
        }
        return false;
    }
    modelJson_ = nlohmann::json::parse(in, nullptr, false);
    if (!modelJson_.is_object()) {
        if (callbacks_.onStatusChanged) {
            callbacks_.onStatusChanged("Model json invalid");
        }
        return false;
    }
    if (!waveLatent_.loadModel(config_.modelPath)) {
        if (callbacks_.onStatusChanged) {
            callbacks_.onStatusChanged("WaveLatent load failed");
        }
        return false;
    }
    return waveLatent_.ready();
}

std::optional<ScalarField2D> WaveLatentNavigator::makeSliceFromValues(const std::vector<float>& values) const {
    const int gridW = waveLatent_.gridWidth();
    const int gridH = waveLatent_.gridHeight();
    const size_t expected = static_cast<size_t>(std::max(0, gridW)) * static_cast<size_t>(std::max(0, gridH));
    if (gridW <= 0 || gridH <= 0 || values.size() != expected) {
        return std::nullopt;
    }
    ScalarField2D slice(boundsMin_, boundsMax_, gridW, gridH);
    slice.set_values(values);
    return slice;
}

std::optional<ScalarField2D> WaveLatentNavigator::decodeSliceAtUV(const Vec2& uv01) {
    const int gridW = waveLatent_.gridWidth();
    const int gridH = waveLatent_.gridHeight();
    if (gridW <= 0 || gridH <= 0) {
        return std::nullopt;
    }
    const size_t expected = static_cast<size_t>(gridW) * static_cast<size_t>(gridH);
    const bool sameUV =
        (lastDetailUV01_.x == uv01.x && lastDetailUV01_.y == uv01.y) ||
        (std::fabs(lastDetailUV01_.x - uv01.x) < 1e-6f && std::fabs(lastDetailUV01_.y - uv01.y) < 1e-6f);
    if (lastDecoded_.size() == expected && sameUV) {
        return makeSliceFromValues(lastDecoded_);
    }

    std::vector<float> decoded;
    decoded.reserve(expected);
    const Vec2 plane = planeFrom01(uv01);
    decodeOnPlane(plane.x, plane.y, decoded);
    if (decoded.size() != expected) {
        return std::nullopt;
    }
    lastDecoded_ = decoded;
    lastDetailUV01_ = uv01;
    return makeSliceFromValues(lastDecoded_);
}

void WaveLatentNavigator::drawDetail(Renderer& renderer) {
    const float leftBase = config_.detailOrigin.x;
    const float topBase = config_.detailOrigin.y;

    drawTileFrame(renderer, leftBase, topBase, config_.detailSize, kFrameColor, 1.4f);
    if (detailField_.empty()) {
        renderer.drawString("No decode.", leftBase, topBase + 16.0f);
        return;
    }

    const float cellW = config_.detailSize / std::max(1.0f, float(waveLatent_.gridWidth()));
    const float cellH = config_.detailSize / std::max(1.0f, float(waveLatent_.gridHeight()));
    detailField_.draw(renderer, leftBase, topBase, cellW, cellH, kIsoColor, 2.2f);

    const Vec2 plane = planeFrom01(lastDetailUV01_);
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2)
        << "u=" << plane.x << "  v=" << plane.y;
    if (lastDetailLatentIndex_) {
        oss << "  #" << (*lastDetailLatentIndex_ + 1);
    }
    renderer.drawString(oss.str(), leftBase, topBase - 18.0f);
}

void WaveLatentNavigator::drawPanel(Renderer& renderer) {
    const float leftBase = config_.panelOrigin.x;
    const float topBase = config_.panelOrigin.y;

    const bool hasStoredLatents = !latents_.empty() && sampleUV_.size() == latents_.size();
    if (hasStoredLatents) {
        drawTileFrame(renderer, leftBase, topBase, config_.panelSize, kFrameColor, 1.1f);
        for (size_t i = 0; i < sampleUV_.size(); ++i) {
            const Vec2 uv01 = panel01FromPlane(sampleUV_[i]);
            const float px = leftBase + uv01.x * config_.panelSize;
            const float py = topBase + uv01.y * config_.panelSize;
            renderer.drawString(std::to_string(i + 1), px + 6.0f, py - 6.0f);
        }
    } else {
        if (panelFields_.empty()) {
            renderer.drawString("Panel unavailable.", leftBase, topBase + 12.0f);
            return;
        }

        const float tileSize = (config_.panelSize - config_.tileGap * float(panelResolution_ - 1)) /
                               std::max(1, panelResolution_);
        const float cellW = tileSize / std::max(1.0f, float(waveLatent_.gridWidth()));
        const float cellH = tileSize / std::max(1.0f, float(waveLatent_.gridHeight()));

        for (int row = 0; row < panelResolution_; ++row) {
            for (int col = 0; col < panelResolution_; ++col) {
                const int idx = row * panelResolution_ + col;
                const float left = leftBase + col * (tileSize + config_.tileGap);
                const float top = topBase + row * (tileSize + config_.tileGap);
                drawTileFrame(renderer, left, top, tileSize, kFrameColor, 1.1f);
                panelFields_[idx].draw(renderer, left, top, cellW, cellH, kIsoColor, 1.6f);
            }
        }
    }

    const float denomU = std::max(1e-6f, uMax_ - uMin_);
    const float denomV = std::max(1e-6f, vMax_ - vMin_);
    const Color axisColor(0.35f, 0.35f, 0.38f, 0.9f);
    const float axisU01 = clamp01((0.0f - uMin_) / denomU);
    const float axisX = leftBase + axisU01 * config_.panelSize;
    renderer.draw2dLine(Vec2(axisX, topBase), Vec2(axisX, topBase + config_.panelSize), axisColor, 1.0f);
    const float axisV01 = clamp01((vMax_ - 0.0f) / denomV);
    const float axisY = topBase + axisV01 * config_.panelSize;
    renderer.draw2dLine(Vec2(leftBase, axisY), Vec2(leftBase + config_.panelSize, axisY), axisColor, 1.0f);

    if (!sampleUV_.empty()) {
        for (const Vec2& plane : sampleUV_) {
            const Vec2 uv01 = panel01FromPlane(plane);
            const float px = leftBase + uv01.x * config_.panelSize;
            const float py = topBase + uv01.y * config_.panelSize;
            renderer.draw2dPoint(Vec2(px, py), Color(0.95f, 0.2f, 0.2f, 1.0f), 3.0f);
        }
    }

    if (pickedScreenPts_.size() >= 2) {
        const Color lineColor(0.9f, 0.4f, 0.15f, 1.0f);
        for (size_t i = 1; i < pickedScreenPts_.size(); ++i) {
            renderer.draw2dLine(pickedScreenPts_[i - 1], pickedScreenPts_[i], lineColor, 1.6f);
        }
    }

    if (hoverUV01_) {
        const float px = leftBase + hoverUV01_->x * config_.panelSize;
        const float py = topBase  + hoverUV01_->y * config_.panelSize;
        renderer.draw2dLine(Vec2(px - 6.0f, py), Vec2(px + 6.0f, py), Color(1.0f, 0.3f, 0.3f), 2.0f);
        renderer.draw2dLine(Vec2(px, py - 6.0f), Vec2(px, py + 6.0f), Color(1.0f, 0.3f, 0.3f), 2.0f);
    }
}

void WaveLatentNavigator::drawTileFrame(Renderer& renderer, float left, float top, float size,
                                        const Color& color, float thickness) const {
    const float right = left + size;
    const float bottom = top + size;
    renderer.draw2dLine(Vec2(left, top), Vec2(right, top), color, thickness);
    renderer.draw2dLine(Vec2(right, top), Vec2(right, bottom), color, thickness);
    renderer.draw2dLine(Vec2(right, bottom), Vec2(left, bottom), color, thickness);
    renderer.draw2dLine(Vec2(left, bottom), Vec2(left, top), color, thickness);
}

std::optional<Vec2> WaveLatentNavigator::panelUVFromMouse(float mouseX, float mouseY) const {
    const float left = config_.panelOrigin.x;
    const float top = config_.panelOrigin.y;
    if (mouseX < left || mouseY < top ||
        mouseX > left + config_.panelSize || mouseY > top + config_.panelSize) {
        return std::nullopt;
    }
    const float u = clamp01((mouseX - left) / config_.panelSize);
    const float v = clamp01((mouseY - top) / config_.panelSize);
    return Vec2(u, v);
}

Vec2 WaveLatentNavigator::planeFrom01(const Vec2& uv01) const {
    const float u = lerpScalar(uMin_, uMax_, clamp01(uv01.x));
    const float v = lerpScalar(vMax_, vMin_, clamp01(uv01.y));
    return Vec2(u, v);
}

Vec2 WaveLatentNavigator::panel01FromPlane(const Vec2& plane) const {
    const float denomU = std::max(1e-6f, uMax_ - uMin_);
    const float denomV = std::max(1e-6f, vMax_ - vMin_);
    const float u = clamp01((plane.x - uMin_) / denomU);
    const float v = clamp01((vMax_ - plane.y) / denomV);
    return Vec2(u, v);
}

std::optional<size_t> WaveLatentNavigator::nearestLatentIndex(const Vec2& uv01, float maxDist01) const {
    if (latents_.empty() || sampleUV_.size() != latents_.size()) {
        return std::nullopt;
    }
    const float maxDistSq = maxDist01 * maxDist01;
    float bestDistSq = maxDistSq;
    std::optional<size_t> bestIndex;
    for (size_t i = 0; i < sampleUV_.size(); ++i) {
        const Vec2 uvStored = panel01FromPlane(sampleUV_[i]);
        const float dx = uvStored.x - uv01.x;
        const float dy = uvStored.y - uv01.y;
        const float distSq = dx * dx + dy * dy;
        if (distSq <= bestDistSq) {
            bestDistSq = distSq;
            bestIndex = i;
        }
    }
    return bestIndex;
}

std::vector<ScalarField2D> WaveLatentNavigator::generateInterpolatedStack() const {
    if (pickedUVs_.size() < 2) {
        return {};
    }
    const int layers = std::max(2, int(std::round(genLayersF_)));
    const int segments = int(pickedUVs_.size()) - 1;
    if (segments <= 0) {
        return {};
    }

    std::vector<Vec2> samples;
    samples.reserve(size_t(layers));
    for (int i = 0; i < layers; ++i) {
        const float t = (layers == 1) ? 0.0f : float(i) / float(layers - 1);
        float segF = t * float(segments);
        int seg = std::min(segments - 1, int(std::floor(segF)));
        float lt = std::clamp(segF - float(seg), 0.0f, 1.0f);
        const Vec2& a = pickedUVs_[size_t(seg)];
        const Vec2& b = pickedUVs_[size_t(seg + 1)];
        samples.emplace_back(a.x * (1.0f - lt) + b.x * lt,
                             a.y * (1.0f - lt) + b.y * lt);
    }

    const int gridW = waveLatent_.gridWidth();
    const int gridH = waveLatent_.gridHeight();
    const size_t expected = static_cast<size_t>(gridW) * static_cast<size_t>(gridH);
    if (gridW <= 0 || gridH <= 0) {
        return {};
    }

    std::vector<ScalarField2D> slices;
    slices.reserve(samples.size());
    std::vector<float> decoded;
    decoded.reserve(expected);
    for (const Vec2& uv01 : samples) {
        decoded.clear();
        const Vec2 plane = planeFrom01(uv01);
        decodeOnPlane(plane.x, plane.y, decoded);
        if (decoded.size() != expected) {
            continue;
        }
        ScalarField2D slice(boundsMin_, boundsMax_, gridW, gridH);
        slice.set_values(decoded);
        slices.emplace_back(std::move(slice));
    }
    return slices;
}

void WaveLatentNavigator::powerEigen(const std::vector<float>& cov, int dim, int iterations,
                                     std::vector<float>& outVec, float& outLambda) {
    outVec.assign(dim, 0.0f);
    for (int d = 0; d < dim; ++d) {
        outVec[d] = 0.15f * (float((d % 5) + 1));
    }
    normalize(outVec);

    std::vector<float> tmp(dim, 0.0f);
    for (int it = 0; it < iterations; ++it) {
        std::fill(tmp.begin(), tmp.end(), 0.0f);
        for (int i = 0; i < dim; ++i) {
            float s = 0.0f;
            for (int j = 0; j < dim; ++j) {
                s += cov[i * dim + j] * outVec[j];
            }
            tmp[i] = s;
        }
        normalize(tmp);
        outVec.swap(tmp);
    }

    float numerator = 0.0f;
    float denominator = 0.0f;
    for (int i = 0; i < dim; ++i) {
        float Ci = 0.0f;
        for (int j = 0; j < dim; ++j) {
            Ci += cov[i * dim + j] * outVec[j];
        }
        numerator += outVec[i] * Ci;
        denominator += outVec[i] * outVec[i];
    }
    outLambda = (denominator > 0.0f) ? numerator / denominator : 0.0f;
}

void WaveLatentNavigator::deflate(std::vector<float>& cov, int dim, const std::vector<float>& vec, float lambda) {
    for (int i = 0; i < dim; ++i) {
        for (int j = 0; j < dim; ++j) {
            cov[i * dim + j] -= lambda * vec[i] * vec[j];
        }
    }
}

void WaveLatentNavigator::normalize(std::vector<float>& vec) {
    double norm2 = 0.0;
    for (float v : vec) {
        norm2 += double(v) * double(v);
    }
    const float inv = (norm2 > 0.0) ? float(1.0 / std::sqrt(norm2)) : 1.0f;
    for (float& v : vec) {
        v *= inv;
    }
}

} // namespace alice2

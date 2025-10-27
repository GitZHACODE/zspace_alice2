#define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <computeGeom/scalarField.h>
#include <computeGeom/scalarField3D.h>
#include <objects/GraphObject.h>
#include <objects/MeshObject.h>

#include "ML/WaveLatent/WaveLatent.h"

using namespace alice2;
using nlohmann::json;

namespace {
const Color kIsoColor(0.10f, 0.35f, 0.85f, 1.0f);
const Color kFrameColor(0.18f, 0.18f, 0.2f, 1.0f);

constexpr float kMargin = 20.0f;
constexpr float kDetailSize = 300.0f;
constexpr float kPanelSize = 300.0f;
constexpr float kDetailPanelGap = 16.0f;
constexpr float kTileGap = 3.0f;
constexpr float kVerticalOffset = 0.0f;
constexpr float kUIExtraOffset = 80.0f;
constexpr float kUIContentHeight = 190.0f;
constexpr float kUIToDetailGap = 30.0f;
constexpr float kContourIso = 0.0f;

float clamp01(float v) {
    return std::max(0.0f, std::min(1.0f, v));
}

float lerpScalar(float a, float b, float t) {
    return a + (b - a) * t;
}

std::string f2(float value) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << value;
    return oss.str();
}
} // namespace

class Sketch_WaveLatent_Stack : public ISketch {
public:
    std::string getName() const override        { return "WaveLatent Panel"; }
    std::string getDescription() const override { return "Inspect a saved WaveLatent model through a latent panel."; }
    std::string getAuthor() const override      { return "alice2 User"; }

    void setup() override {
        scene().setBackgroundColor(Color(0.92f, 0.92f, 0.94f));
        scene().setShowAxes(false);
        scene().setShowGrid(false);

        if (!loadModelFile()) {
            std::printf("[WaveLatent][Panel] unable to load model.\n");
            return;
        }

        boundsMin_ = Vec3(domainXMin_, domainYMin_, 0.0f);
        boundsMax_ = Vec3(domainXMax_, domainYMax_, 0.0f);

        meshObject_ = std::make_shared<MeshObject>("WaveStackIsoMesh");
        meshObject_->setVisible(false);
        meshObject_->setRenderMode(MeshRenderMode::NormalShaded);
        meshObject_->setShowEdges(false);
        meshObject_->setShowVertices(false);
        scene().addObject(meshObject_);

        configureFields();
        setupLatentSpace();
        buildPanelTiles();
        updateDetailField(currentUV01_);

        ui_ = std::make_unique<SimpleUI>(input());
        ui_->setTheme(SimpleUI::UITheme::Dark);
        const float uiLeft = kMargin;
        const float uiTop = kMargin + kVerticalOffset + kUIExtraOffset;
        const float sliderWidth = 260.0f;
        ui_->addSlider("Total Height", Vec2{uiLeft, uiTop}, sliderWidth, 0.0f, 500.0f, totalHeight_);
        ui_->addSlider("Iso Level", Vec2{uiLeft, uiTop + 30.0f}, sliderWidth, -0.5f, 0.5f, isoLevel_);
        ui_->addSlider("Gen Layers", Vec2{uiLeft, uiTop + 60.0f}, sliderWidth, 2.0f, 200.0f, genLayersF_);

        const float toggleXLeft = static_cast<int>(uiLeft);
        const float toggleXRight = toggleXLeft + 140;
        const float toggleYRow0 = static_cast<int>(uiTop + 100.0f);
        const float toggleYRow1 = static_cast<int>(uiTop + 130.0f);
        const float toggleYRow2 = static_cast<int>(uiTop + 160.0f);

        ui_->addToggle("Smooth*5", UIRect{toggleXLeft, toggleYRow0, 120, 22}, btnSmooth_);
        ui_->addToggle("Laplacian*5", UIRect{toggleXRight, toggleYRow0, 140, 22}, btnLaplacian_);
        ui_->addToggle("Build Mesh", UIRect{toggleXLeft, toggleYRow1, 120, 22}, btnBuildMesh_);
        ui_->addToggle("Display Mesh", UIRect{toggleXRight, toggleYRow1, 140, 22}, meshVisible_);
        ui_->addToggle("Export", UIRect{toggleXLeft, toggleYRow2, 120, 22}, btnExport_);

        ready_ = true;
        statusMessage_ = "Loaded model '" + modelPath_ + "'. Click panel to add slices; press G to interpolate path.";
    }

    void cleanup() override {
        clearContourObjects();
        stackFields_.clear();
        pickedUVs_.clear();
        invalidateVolumeMesh();
        if (meshObject_) {
            scene().removeObject(meshObject_);
            meshObject_.reset();
        }
        ui_.reset();
        ready_ = false;
    }

    void update(float) override {
        if (!ready_) {
            return;
        }
        postUpdateUI();
    }

    void draw(Renderer& renderer, Camera&) override {
        renderer.setColor(Color(0.1f, 0.1f, 0.1f));
        const float textYOffset = kVerticalOffset;
        renderer.drawString("WaveLatent Stack Viewer", kMargin, 24.0f + textYOffset);
        renderer.drawString("Slices: " + std::to_string(stackFields_.size()) +
                            "   Total Height: " + f2(totalHeight_) +
                            "   Iso: " + f2(isoLevel_) +
                            "   GenLayers(G): " + std::to_string(int(std::round(genLayersF_))),
                            kMargin, 44.0f + textYOffset);
        renderer.drawString(statusMessage_, kMargin, 64.0f + textYOffset);

        if (!ready_) {
            renderer.drawString("Model not ready.", kMargin, 84.0f + textYOffset);
            if (ui_) {
                ui_->draw(renderer);
            }
            return;
        }

        drawDetail(renderer);
        drawPanel(renderer);
        updateContourPlacement();

        if (ui_) {
            ui_->draw(renderer);
        }
    }

    bool onMouseMove(int x, int y) override {
        if (!ready_) {
            return false;
        }
        if (ui_ && ui_->onMouseMove(x, y)) {
            return true;
        }
        auto uv = panelUVFromMouse(float(x), float(y));
        if (!uv) {
            return false;
        }
        hoverUV01_ = *uv;
        updateDetailField(*uv);
        const Vec2 plane = planeFrom01(*uv);
        statusMessage_ = "Hover: u=" + f2(plane.x) + " v=" + f2(plane.y);
        return true;
    }

    bool onMousePress(int button, int state, int x, int y) override {
        if (!ready_) {
            return false;
        }
        if (ui_ && ui_->onMousePress(button, state, x, y)) {
            return true;
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

        if (addSliceAtUV(*uv)) {
            const Vec2 plane = planeFrom01(*uv);
            statusMessage_ = "Added slice at u=" + f2(plane.x) + " v=" + f2(plane.y) +
                              " -> total slices: " + std::to_string(stackFields_.size());
        } else {
            statusMessage_ = "Decode failed for that latent click.";
        }
        return true;
    }

    bool onKeyPress(unsigned char key, int, int) override {
        if (!ready_) {
            return false;
        }
        switch (key) {
        case '[':
            changePanelResolution(-1);
            return true;
        case ']':
            changePanelResolution(+1);
            return true;
        case 'g':
        case 'G':
            generateInterpolatedStack();
            return true;
        case 'c':
        case 'C':
            clearAllSelections();
            return true;
        case 'j':
        case 'J':
            for (int i = 0; i < 5; ++i) {
                smooth();
            }
            return true;
        case 'k':
        case 'K':
            for (int i = 0; i < 5; ++i) {
                applyStackLaplacian();
            }
            return true;
        default:
            return false;
        }
    }

private:
    bool loadModelFile() {
        std::ifstream in(modelPath_);
        if (!in.is_open()) {
            statusMessage_ = "Failed to open '" + modelPath_ + "'";
            return false;
        }
        modelJson_ = json::parse(in, nullptr, false);
        if (!modelJson_.is_object()) {
            statusMessage_ = "Invalid JSON in '" + modelPath_ + "'";
            return false;
        }
        if (!waveLatent_.loadModel(modelPath_)) {
            statusMessage_ = "WaveLatent loadModel failed.";
            return false;
        }

        domainXMin_ = waveLatent_.domainXMin();
        domainXMax_ = waveLatent_.domainXMax();
        domainYMin_ = waveLatent_.domainYMin();
        domainYMax_ = waveLatent_.domainYMax();
        return waveLatent_.ready();
    }

    void configureFields() {
        const int gridW = waveLatent_.gridWidth();
        const int gridH = waveLatent_.gridHeight();
        if (gridW <= 0 || gridH <= 0) {
            statusMessage_ = "Model grid size invalid.";
            ready_ = false;
            return;
        }
        detailField_.configure(gridW, gridH, domainXMin_, domainXMax_, domainYMin_, domainYMax_);
        panelFields_.assign(panelResolution_ * panelResolution_, {});
        for (auto& field : panelFields_) {
            field.configure(gridW, gridH, domainXMin_, domainXMax_, domainYMin_, domainYMax_);
        }
    }

    void setupLatentSpace() {
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

    void computePCA() {
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

        if (!std::isfinite(minU) || !std::isfinite(minV)) {
            minU = -2.5f; maxU = 2.5f;
            minV = -2.5f; maxV = 2.5f;
        }
        if (maxU - minU < 1e-5f) { minU -= 1.0f; maxU += 1.0f; }
        if (maxV - minV < 1e-5f) { minV -= 1.0f; maxV += 1.0f; }
        const float marginU = (maxU - minU) * 0.25f + 1e-3f;
        const float marginV = (maxV - minV) * 0.25f + 1e-3f;
        uMin_ = minU - marginU;
        uMax_ = maxU + marginU;
        vMin_ = minV - marginV;
        vMax_ = maxV + marginV;
    }

    void buildPanelTiles() {
        panelFields_.assign(panelResolution_ * panelResolution_, {});
        const int gridW = waveLatent_.gridWidth();
        const int gridH = waveLatent_.gridHeight();
        for (auto& field : panelFields_) {
            field.configure(gridW, gridH, domainXMin_, domainXMax_, domainYMin_, domainYMax_);
        }

        std::vector<float> decoded;
        decoded.reserve(static_cast<size_t>(gridW) * static_cast<size_t>(gridH));
        for (int row = 0; row < panelResolution_; ++row) {
            for (int col = 0; col < panelResolution_; ++col) {
                const float normU = (col + 0.5f) / std::max(1, panelResolution_);
                const float normV = (row + 0.5f) / std::max(1, panelResolution_);
                const Vec2 plane = planeFrom01(Vec2(normU, normV));
                decodeOnPlane(plane.x, plane.y, decoded);
                panelFields_[row * panelResolution_ + col].updateValues(decoded);
            }
        }
    }

    void decodeOnPlane(float u, float v, std::vector<float>& gridOut) const {
        const int dim = waveLatent_.latentDim();
        if (dim <= 0) {
            gridOut.clear();
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
        waveLatent_.decodeLatentToGrid(z, gridOut);
    }

    void updateDetailField(const Vec2& uv01) {
        std::vector<float> decoded;
        const Vec2 plane = planeFrom01(uv01);
        decodeOnPlane(plane.x, plane.y, decoded);
        lastDecoded_ = decoded;
        detailField_.updateValues(decoded);
    }

    bool addSliceAtUV(const Vec2& uv01) {
        const int gridW = waveLatent_.gridWidth();
        const int gridH = waveLatent_.gridHeight();
        if (gridW <= 0 || gridH <= 0) {
            return false;
        }
        const size_t expected = static_cast<size_t>(gridW) * static_cast<size_t>(gridH);
        if (lastDecoded_.size() == expected) {
            return pushSliceFromValues(lastDecoded_);
        }

        std::vector<float> decoded;
        const Vec2 plane = planeFrom01(uv01);
        decodeOnPlane(plane.x, plane.y, decoded);
        if (decoded.size() != expected) {
            return false;
        }
        lastDecoded_ = decoded;
        return pushSliceFromValues(lastDecoded_);
    }

    bool pushSliceFromValues(const std::vector<float>& values) {
        const int gridW = waveLatent_.gridWidth();
        const int gridH = waveLatent_.gridHeight();
        if (gridW <= 0 || gridH <= 0) {
            return false;
        }
        const size_t expected = static_cast<size_t>(gridW) * static_cast<size_t>(gridH);
        if (values.size() != expected) {
            return false;
        }
        ScalarField2D slice(boundsMin_, boundsMax_, gridW, gridH);
        slice.set_values(values);
        stackFields_.emplace_back(std::move(slice));
        regenerateContours();
        invalidateVolumeMesh();
        return true;
    }

    std::optional<Vec2> panelUVFromMouse(float mouseX, float mouseY) const {
        const float left = panelLeft();
        const float top = panelTop();
        if (mouseX < left || mouseY < top ||
            mouseX > left + kPanelSize || mouseY > top + kPanelSize) {
            return std::nullopt;
        }
        const float u = clamp01((mouseX - left) / kPanelSize);
        const float v = clamp01((mouseY - top) / kPanelSize);
        return Vec2(u, v);
    }

    Vec2 planeFrom01(const Vec2& uv01) const {
        const float u = lerpScalar(uMin_, uMax_, clamp01(uv01.x));
        const float v = lerpScalar(vMax_, vMin_, clamp01(uv01.y));
        return Vec2(u, v);
    }

    Vec2 panel01FromPlane(const Vec2& plane) const {
        const float denomU = std::max(1e-6f, uMax_ - uMin_);
        const float denomV = std::max(1e-6f, vMax_ - vMin_);
        const float u = clamp01((plane.x - uMin_) / denomU);
        const float v = clamp01((vMax_ - plane.y) / denomV);
        return Vec2(u, v);
    }

    void drawTileFrame(Renderer& renderer, float left, float top, float size,
                       const Color& color, float thickness) const {
        const float right = left + size;
        const float bottom = top + size;
        renderer.draw2dLine(Vec2(left, top), Vec2(right, top), color, thickness);
        renderer.draw2dLine(Vec2(right, top), Vec2(right, bottom), color, thickness);
        renderer.draw2dLine(Vec2(right, bottom), Vec2(left, bottom), color, thickness);
        renderer.draw2dLine(Vec2(left, bottom), Vec2(left, top), color, thickness);
    }

    float detailLeft() const { return kMargin; }
    float detailTop() const { return kMargin + kVerticalOffset + kUIExtraOffset + kUIContentHeight + kUIToDetailGap; }
    float panelLeft() const { return kMargin; }
    float panelTop() const { return detailTop() + kDetailSize + kDetailPanelGap; }

    void drawPanel(Renderer& renderer) {
        const float leftBase = panelLeft();
        const float topBase = panelTop();
        // renderer.setColor(Color(0.25f, 0.25f, 0.30f));
        // renderer.drawString("Latent Panel", leftBase, topBase - 18.0f);

        if (panelFields_.empty()) {
            renderer.drawString("Panel unavailable.", leftBase, topBase + 12.0f);
            return;
        }

        const float tileSize = (kPanelSize - kTileGap * float(panelResolution_ - 1)) / std::max(1, panelResolution_);
        const float cellW = tileSize / std::max(1.0f, float(waveLatent_.gridWidth()));
        const float cellH = tileSize / std::max(1.0f, float(waveLatent_.gridHeight()));

        for (int row = 0; row < panelResolution_; ++row) {
            for (int col = 0; col < panelResolution_; ++col) {
                const int idx = row * panelResolution_ + col;
                const float left = leftBase + col * (tileSize + kTileGap);
                const float top = topBase + row * (tileSize + kTileGap);
                drawTileFrame(renderer, left, top, tileSize, kFrameColor, 1.1f);
                panelFields_[idx].draw(renderer, left, top, cellW, cellH, kIsoColor, 1.6f);
            }
        }

        if (!sampleUV_.empty()) {
            for (const Vec2& plane : sampleUV_) {
                const Vec2 uv01 = panel01FromPlane(plane);
                const float px = leftBase + uv01.x * kPanelSize;
                const float py = topBase  + uv01.y * kPanelSize;
                renderer.draw2dPoint(Vec2(px, py), Color(0.95f, 0.2f, 0.2f, 1.0f), 3.0f);
            }
        }

        if (hoverUV01_) {
            const float px = leftBase + hoverUV01_->x * kPanelSize;
            const float py = topBase  + hoverUV01_->y * kPanelSize;
            renderer.draw2dLine(Vec2(px - 6.0f, py), Vec2(px + 6.0f, py), Color(1.0f, 0.3f, 0.3f), 2.0f);
            renderer.draw2dLine(Vec2(px, py - 6.0f), Vec2(px, py + 6.0f), Color(1.0f, 0.3f, 0.3f), 2.0f);
        }
    }

    void drawDetail(Renderer& renderer) {
        const float leftBase = detailLeft();
        const float topBase = detailTop();
        // renderer.setColor(Color(0.25f, 0.25f, 0.30f));
        // renderer.drawString("Detail Preview", leftBase, topBase - 18.0f);

        drawTileFrame(renderer, leftBase, topBase, kDetailSize, kFrameColor, 1.4f);
        if (detailField_.empty()) {
            renderer.drawString("No decode available.", leftBase, topBase + 16.0f);
            return;
        }

        const float cellW = kDetailSize / std::max(1.0f, float(waveLatent_.gridWidth()));
        const float cellH = kDetailSize / std::max(1.0f, float(waveLatent_.gridHeight()));
        detailField_.draw(renderer, leftBase, topBase, cellW, cellH, kIsoColor, 2.2f);

        const Vec2 plane = planeFrom01(currentUV01_);
        // renderer.drawString("u=" + f2(plane.x) + "   v=" + f2(plane.y),
        //                     leftBase, topBase + kDetailSize + 18.0f);
    }

    void regenerateContours() {
        clearContourObjects();
        stackContours_.reserve(stackFields_.size());
        for (size_t i = 0; i < stackFields_.size(); ++i) {
            GraphObject contour = stackFields_[i].get_contours(isoLevel_);
            contour.setShowVertices(false);
            contour.setShowEdges(true);
            contour.setEdgeWidth(1.2f);

            auto contourPtr = std::make_shared<GraphObject>(std::move(contour));
            contourPtr->setName("WaveStackContour_" + std::to_string(i));
            scene().addObject(contourPtr);
            stackContours_.emplace_back(std::move(contourPtr));
        }
        updateContourPlacement();
    }

    void clearContourObjects() {
        for (auto& c : stackContours_) {
            if (c) {
                scene().removeObject(c);
            }
        }
        stackContours_.clear();
    }

    void updateContourPlacement() {
        const size_t count = stackContours_.size();
        if (count == 0) {
            return;
        }
        sliceSpacing_ = (count <= 1) ? totalHeight_ : (totalHeight_ / float(std::max<size_t>(1, count - 1)));
        for (size_t i = 0; i < count; ++i) {
            auto& contour = stackContours_[i];
            if (!contour) {
                continue;
            }
            const float hue = 360.0f * (float(i) / std::max<size_t>(1, count));
            float r, g, b;
            ScalarFieldUtils::get_hsv_color(hue / 360.0f * 2.0f - 1.0f, r, g, b);
            contour->setEdgeColor(Color(r, g, b));
            contour->setColor(Color(r, g, b));
            contour->getTransform().setTranslation(Vec3(0.0f, 0.0f, float(i) * sliceSpacing_));
        }
    }

    void clearAllSelections() {
        pickedUVs_.clear();
        stackFields_.clear();
        clearContourObjects();
        invalidateVolumeMesh();
        statusMessage_ = "Cleared selections and stack.";
    }

    void generateInterpolatedStack() {
        if (pickedUVs_.size() < 2) {
            statusMessage_ = "Need at least 2 latent picks to interpolate.";
            return;
        }
        const int layers = std::max(2, int(std::round(genLayersF_)));
        const int segments = int(pickedUVs_.size()) - 1;
        if (segments <= 0) {
            statusMessage_ = "Insufficient latent path length.";
            return;
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
            statusMessage_ = "Grid resolution unavailable.";
            return;
        }

        std::vector<ScalarField2D> newFields;
        newFields.reserve(samples.size());
        std::vector<float> decoded;
        for (const Vec2& uv01 : samples) {
            decoded.clear();
            const Vec2 plane = planeFrom01(uv01);
            decodeOnPlane(plane.x, plane.y, decoded);
            if (decoded.size() != expected) {
                continue;
            }
            ScalarField2D slice(boundsMin_, boundsMax_, gridW, gridH);
            slice.set_values(decoded);
            newFields.emplace_back(std::move(slice));
        }

        stackFields_.swap(newFields);
        regenerateContours();
        invalidateVolumeMesh();

        statusMessage_ = "Generated interpolated stack: " + std::to_string(stackFields_.size()) + " layers.";
    }

    void applyStackLaplacian() {
        if (stackFields_.size() < 3) {
            statusMessage_ = "Need 3+ slices for Laplacian smoothing.";
            return;
        }
        const size_t n = stackFields_.size();
        std::vector<std::vector<float>> smoothed(n);
        for (size_t i = 0; i < n; ++i) {
            smoothed[i] = stackFields_[i].get_values();
        }
        for (size_t i = 1; i + 1 < n; ++i) {
            const auto& prev = stackFields_[i - 1].get_values();
            const auto& next = stackFields_[i + 1].get_values();
            auto& dest = smoothed[i];
            for (size_t k = 0; k < dest.size(); ++k) {
                dest[k] = 0.5f * (prev[k] + next[k]);
            }
        }
        for (size_t i = 0; i < n; ++i) {
            stackFields_[i].set_values(smoothed[i]);
        }
        regenerateContours();
        invalidateVolumeMesh();
        statusMessage_ = "Stack Laplacian applied.";
    }

    void smooth() {
        if (stackFields_.empty()) {
            statusMessage_ = "No slices to smooth.";
            return;
        }
        for (auto& field : stackFields_) {
            smoothField(field);
        }
        regenerateContours();
        invalidateVolumeMesh();
        statusMessage_ = "In-plane smoothing applied.";
    }

    void smoothField(ScalarField2D& field) {
        const auto& values = field.get_values();
        if (values.empty()) {
            return;
        }
        const auto res = field.get_resolution();
        const int rx = res.first;
        const int ry = res.second;
        std::vector<float> out(values.size(), 0.0f);
        auto idx = [rx](int x, int y) { return y * rx + x; };
        for (int y = 0; y < ry; ++y) {
            for (int x = 0; x < rx; ++x) {
                float sum = 0.0f;
                int count = 0;
                for (int ny = std::max(0, y - 1); ny <= std::min(ry - 1, y + 1); ++ny) {
                    for (int nx = std::max(0, x - 1); nx <= std::min(rx - 1, x + 1); ++nx) {
                        sum += values[idx(nx, ny)];
                        ++count;
                    }
                }
                out[idx(x, y)] = (count > 0) ? sum / float(count) : values[idx(x, y)];
            }
        }
        field.set_values(out);
    }

    bool buildVolumeMeshFromStack() {
        if (stackFields_.empty()) {
            statusMessage_ = "Add or generate slices before building a mesh.";
            return false;
        }
        const auto res = stackFields_.front().get_resolution();
        const int rx = res.first;
        const int ry = res.second;
        const int rz = int(stackFields_.size());
        if (rx <= 0 || ry <= 0 || rz <= 0) {
            statusMessage_ = "Invalid slice resolution.";
            return false;
        }

        sliceSpacing_ = (rz <= 1) ? totalHeight_ : (totalHeight_ / float(std::max(1, rz - 1)));
        Vec3 minB(boundsMin_.x, boundsMin_.y, 0.0f);
        Vec3 maxB(boundsMax_.x, boundsMax_.y, std::max(0.001f, totalHeight_));

        volumeField_ = std::make_unique<ScalarField3D>(minB, maxB, rx, ry, rz);

        const size_t layerSize = size_t(rx) * size_t(ry);
        std::vector<float> volume(layerSize * size_t(rz), 0.0f);
        for (int z = 0; z < rz; ++z) {
            const auto& slice = stackFields_[size_t(z)].get_values();
            if (slice.size() == layerSize) {
                std::copy(slice.begin(), slice.end(), volume.begin() + layerSize * size_t(z));
            }
        }
        volumeField_->set_values(volume);

        auto meshData = volumeField_->generate_mesh(isoLevel_);
        if (!meshData || meshData->vertices.empty()) {
            statusMessage_ = "Mesh build produced no geometry.";
            meshGenerated_ = false;
            meshVisible_ = false;
            lastMeshVisible_ = false;
            if (meshObject_) {
                meshObject_->setVisible(false);
            }
            return false;
        }

        if (!meshObject_) {
            meshObject_ = std::make_shared<MeshObject>("WaveStackIsoMesh");
            meshObject_->setShowEdges(false);
            meshObject_->setShowVertices(false);
            meshObject_->setRenderMode(MeshRenderMode::NormalShaded);
            scene().addObject(meshObject_);
        }

        meshObject_->setMeshData(meshData);
        meshObject_->setRenderMode(MeshRenderMode::NormalShaded);
        meshObject_->setNormalShadingColors(Color(0.1f, 0.1f, 0.1f), Color(1.0f, 1.0f, 1.0f));
        meshGenerated_ = true;
        meshVisible_ = true;
        lastMeshVisible_ = true;
        meshObject_->setVisible(true);
        statusMessage_ = "3D mesh generated.";
        return true;
    }

    void exportAll() {
        if (stackFields_.empty()) {
            statusMessage_ = "Nothing to export.";
            return;
        }
        if ((!meshGenerated_ || !meshObject_) && !buildVolumeMeshFromStack()) {
            statusMessage_ = "Export aborted (mesh failed).";
            return;
        }
        if (meshObject_) {
            meshObject_->writeToObj(outputMeshName_);
        }
        if (!stackContours_.empty()) {
            GraphObject merged("WaveStackContoursExport");
            for (auto& contour : stackContours_) {
                if (contour) {
                    merged.combineWith(*contour);
                }
            }
            merged.weld();
            merged.writeToObj(outputContoursName_);
        }
        statusMessage_ = "Exported mesh -> " + outputMeshName_ + " ; contours -> " + outputContoursName_;
    }

    void toggleMeshVisibleFromUI() {
        if (!meshObject_) {
            return;
        }
        if (!meshGenerated_) {
            meshVisible_ = false;
        }
        meshObject_->setVisible(meshVisible_);
        statusMessage_ = meshVisible_ ? "Mesh visible." : "Mesh hidden.";
    }

    void postUpdateUI() {
        if (!ui_) {
            return;
        }

        if (btnBuildMesh_) {
            buildVolumeMeshFromStack();
            btnBuildMesh_ = false;
        }
        if (btnExport_) {
            exportAll();
            btnExport_ = false;
        }
        if (btnSmooth_) {
            for (int i = 0; i < 5; ++i) {
                smooth();
            }
            btnSmooth_ = false;
        }
        if (btnLaplacian_) {
            for (int i = 0; i < 5; ++i) {
                applyStackLaplacian();
            }
            btnLaplacian_ = false;
        }

        if (meshVisible_ != lastMeshVisible_) {
            toggleMeshVisibleFromUI();
            lastMeshVisible_ = meshVisible_;
        }

        static float lastHeight = totalHeight_;
        static float lastIso = isoLevel_;
        static float lastGen = genLayersF_;
        if (std::fabs(totalHeight_ - lastHeight) > 1e-4f) {
            updateContourPlacement();
            invalidateVolumeMesh();
            lastHeight = totalHeight_;
        }
        if (std::fabs(isoLevel_ - lastIso) > 1e-6f) {
            regenerateContours();
            invalidateVolumeMesh();
            lastIso = isoLevel_;
        }
        if (std::fabs(genLayersF_ - lastGen) > 1e-6f) {
            lastGen = genLayersF_;
        }
    }

    void invalidateVolumeMesh() {
        volumeField_.reset();
        meshGenerated_ = false;
        meshVisible_ = false;
        lastMeshVisible_ = false;
        if (meshObject_) {
            meshObject_->setVisible(false);
        }
    }

    void changePanelResolution(int delta) {
        const int newRes = std::clamp(panelResolution_ + delta, 3, 16);
        if (newRes == panelResolution_) {
            return;
        }
        panelResolution_ = newRes;
        configureFields();
        buildPanelTiles();
        updateDetailField(currentUV01_);
        statusMessage_ = "Panel resolution set to " + std::to_string(panelResolution_) + "x" + std::to_string(panelResolution_);
    }

    static void powerEigen(const std::vector<float>& cov, int dim, int iterations,
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

    static void deflate(std::vector<float>& cov, int dim, const std::vector<float>& vec, float lambda) {
        for (int i = 0; i < dim; ++i) {
            for (int j = 0; j < dim; ++j) {
                cov[i * dim + j] -= lambda * vec[i] * vec[j];
            }
        }
    }

    static void normalize(std::vector<float>& vec) {
        double norm2 = 0.0;
        for (float v : vec) {
            norm2 += double(v) * double(v);
        }
        const float inv = (norm2 > 0.0) ? float(1.0 / std::sqrt(norm2)) : 1.0f;
        for (float& v : vec) {
            v *= inv;
        }
    }

    WaveLatent waveLatent_;
    json modelJson_;

    std::vector<std::vector<float>> latents_;
    std::vector<Vec2> sampleUV_;
    std::vector<float> pcaMean_;
    std::vector<float> pcaE1_;
    std::vector<float> pcaE2_;
    float uMin_ = -2.5f;
    float uMax_ = 2.5f;
    float vMin_ = -2.5f;
    float vMax_ = 2.5f;

    GridField detailField_;
    std::vector<GridField> panelFields_;
    std::vector<float> lastDecoded_;

    std::vector<ScalarField2D> stackFields_;
    std::vector<std::shared_ptr<GraphObject>> stackContours_;
    std::vector<Vec2> pickedUVs_;

    Vec2 currentUV01_{0.5f, 0.5f};
    std::optional<Vec2> hoverUV01_;

    float domainXMin_ = -1.2f;
    float domainXMax_ = 1.2f;
    float domainYMin_ = -1.2f;
    float domainYMax_ = 1.2f;

    Vec3 boundsMin_{-1.0f, -1.0f, 0.0f};
    Vec3 boundsMax_{1.0f, 1.0f, 0.0f};

    float isoLevel_ = kContourIso;
    float totalHeight_ = 200.0f;
    float sliceSpacing_ = 1.0f;
    float genLayersF_ = 60.0f;

    std::unique_ptr<ScalarField3D> volumeField_;
    std::shared_ptr<MeshObject> meshObject_;
    bool meshVisible_ = false;
    bool meshGenerated_ = false;

    std::unique_ptr<SimpleUI> ui_;
    bool btnBuildMesh_ = false;
    bool btnExport_ = false;
    bool btnSmooth_ = false;
    bool btnLaplacian_ = false;
    bool lastMeshVisible_ = false;

    std::string outputMeshName_ = "waveStackMesh.obj";
    std::string outputContoursName_ = "waveStackContours.obj";

    int panelResolution_ = 9;
    bool ready_ = false;
    std::string statusMessage_ = "Idle";
    std::string modelPath_ = "WaveLatentModel.json";
};

ALICE2_REGISTER_SKETCH_AUTO(Sketch_WaveLatent_Stack)

#endif // __MAIN__

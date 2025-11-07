#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <alice2.h>

#include "ML/WaveLatent/WaveLatent.h"
#include <computeGeom/scalarField.h>

namespace alice2 {

class WaveLatentNavigator {
public:
    struct Config {
        std::string modelPath = "WaveLatentModel.json";
        Vec2 detailOrigin{20.0f, 300.0f};
        Vec2 panelOrigin{20.0f, 636.0f};
        float detailSize = 300.0f;
        float panelSize = 300.0f;
        float tileGap = 3.0f;
        float latentSnapRadius = 0.02f;
        int initialPanelResolution = 9;
        int minPanelResolution = 3;
        int maxPanelResolution = 16;
        float defaultGenLayers = 60.0f;

        SimpleUI* ui = nullptr;
        Vec2 uiGenSliderPos{20.0f, 100.0f};
        float uiGenSliderWidth = 260.0f;
    };

    struct Callbacks {
        std::function<void(ScalarField2D&&)> onSliceDecoded;
        std::function<void(std::vector<ScalarField2D>&&)> onStackGenerated;
        std::function<void()> onClearRequested;
        std::function<void(const std::string&)> onStatusChanged;
    };

    WaveLatentNavigator();

    bool initialize(const Config& config, const Callbacks& callbacks);
    void shutdown();

    void update();
    void draw(Renderer& renderer);

    bool handleMouseMove(int x, int y);
    bool handleMousePress(int button, int state, int x, int y);
    bool handleKeyPress(unsigned char key);

    void setPanelResolution(int resolution);
    int panelResolution() const { return panelResolution_; }

    void setLatentSnapRadius(float radius) { latentSnapRadius_ = radius; }
    float latentSnapRadius() const { return latentSnapRadius_; }

    void setGenLayers(float layers) { genLayersF_ = layers; }
    float genLayers() const { return genLayersF_; }

    const Vec3& boundsMin() const { return boundsMin_; }
    const Vec3& boundsMax() const { return boundsMax_; }

    const WaveLatent& model() const { return waveLatent_; }
    WaveLatent& model() { return waveLatent_; }

    bool ready() const { return ready_; }

    void clearSelection();
    std::vector<Vec2> pickedUVs() const { return pickedUVs_; }

private:
    void configureFields();
    void setupLatentSpace();
    void computePCA();
    void buildPanelTiles();
    void updateDetailField(const Vec2& uv01);
    void decodeOnPlane(float u, float v, std::vector<float>& outValues) const;
    bool loadModelFile();
    std::optional<ScalarField2D> makeSliceFromValues(const std::vector<float>& values) const;
    std::optional<ScalarField2D> decodeSliceAtUV(const Vec2& uv01);

    void drawDetail(Renderer& renderer);
    void drawPanel(Renderer& renderer);
    void drawTileFrame(Renderer& renderer, float left, float top, float size,
                       const Color& color, float thickness) const;

    std::optional<Vec2> panelUVFromMouse(float mouseX, float mouseY) const;
    Vec2 planeFrom01(const Vec2& uv01) const;
    Vec2 panel01FromPlane(const Vec2& plane) const;
    std::optional<size_t> nearestLatentIndex(const Vec2& uv01, float maxDist01) const;

    std::vector<ScalarField2D> generateInterpolatedStack() const;

    static void powerEigen(const std::vector<float>& cov, int dim, int iterations,
                           std::vector<float>& outVec, float& outLambda);
    static void deflate(std::vector<float>& cov, int dim, const std::vector<float>& vec, float lambda);
    static void normalize(std::vector<float>& vec);

private:
    Config config_;
    Callbacks callbacks_;
    SimpleUI* ui_ = nullptr;

    WaveLatent waveLatent_;
    nlohmann::json modelJson_;

    Vec3 boundsMin_{-1.0f, -1.0f, 0.0f};
    Vec3 boundsMax_{1.0f, 1.0f, 0.0f};

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

    Vec2 currentUV01_{0.5f, 0.5f};
    Vec2 lastDetailUV01_{0.5f, 0.5f};
    std::optional<Vec2> hoverUV01_;
    std::optional<size_t> lastDetailLatentIndex_;

    std::vector<Vec2> pickedUVs_;
    std::vector<Vec2> pickedScreenPts_;

    float latentSnapRadius_ = 0.02f;
    float genLayersF_ = 60.0f;

    int panelResolution_ = 9;
    bool ready_ = false;
};

} // namespace alice2

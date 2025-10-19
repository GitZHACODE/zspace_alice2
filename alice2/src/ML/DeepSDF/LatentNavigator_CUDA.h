#pragma once

#define ALICE2_USE_CUDA
#ifdef ALICE2_USE_CUDA

#include <vector>
#include <memory>
#include <optional>

#include <alice2.h>
#include <cuda_gl_interop.h>

#include "LatentSDF_CUDA.h"
#include "FieldViewer.h"

namespace DeepSDF {

class LatentNavigator_CUDA {
public:
    LatentNavigator_CUDA();
    ~LatentNavigator_CUDA();

    struct Coord2 {
        float x = 0.0f;
        float y = 0.0f;
    };

    void initialize(TinyAutoDecoderCUDA* decoder,
                    const FieldDomain* domain,
                    const std::vector<std::vector<float>>* latentSource);
    void shutdown();

    void setPanelResolution(int N, int tileRes, int gap);
    void setLatentSource(const std::vector<std::vector<float>>* latentSource);

    void setDetailResolution(int res);

    void markPanelDirty();

    bool drawPanel(alice2::Renderer& renderer,
                   float left, float top, float size,
                   const FieldRenderConfig& cfg);

    bool getFieldAt(const std::vector<float>& latent, GridField& out);
    bool getBlendedField(float u, float v, GridField& out);

    void drawField(alice2::Renderer& renderer,
                   const GridField& field,
                   float left, float top, float size,
                   const FieldRenderConfig& cfg) const;

private:
    bool ensurePanelResources();
    bool rebuildPanel(const FieldRenderConfig& cfg);
    bool decodeLatent(const std::vector<float>& latent, GridField& out);
    void updateLatentEmbedding();
    bool computeLatentBlend(float cx, float cy, std::vector<float>& outLatent);

    TinyAutoDecoderCUDA* decoder_ = nullptr;
    const FieldDomain* domain_ = nullptr;
    const std::vector<std::vector<float>>* latents_ = nullptr;

    // Panel configuration
    int panelN_    = 5;
    int tileRes_   = 56;
    int tileGap_   = 4;

    // Device resources
    float* dPanelField_   = nullptr;
    uchar4* dPanelRGBA_   = nullptr;
    float* dScratchLatent_= nullptr;
    float* dTileMin_      = nullptr;
    float* dTileMax_      = nullptr;

    GLuint panelTexture_ = 0;
    cudaGraphicsResource* panelResource_ = nullptr;

    int   tileCapacity_ = 0;
    int   panelW_ = 0;
    int   panelH_ = 0;
    int   fieldScratchRes_ = 0;

    bool panelDirty_ = true;
    FieldRenderConfig lastPanelCfg_{};

    float* dFieldScratch_ = nullptr;
    std::vector<float> scratchLatentHost_;

    std::vector<Coord2> latentCoords2D_;
    Coord2 latentBBoxMin_{};
    Coord2 latentBBoxMax_{};
    Coord2 latentSampleMin_{-1.0f, -1.0f};
    Coord2 latentSampleMax_{1.0f, 1.0f};
    Coord2 latentSampleRange_{2.0f, 2.0f};
};

} // namespace DeepSDF

#endif // ALICE2_USE_CUDA

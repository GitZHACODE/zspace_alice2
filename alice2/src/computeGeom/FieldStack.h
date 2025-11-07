#pragma once

#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <alice2.h>

#include <objects/MeshObject.h>
#include <objects/GraphObject.h>

#include <computeGeom/scalarField.h>
#include <computeGeom/scalarField3D.h>

namespace alice2 {

class FieldStack {
public:
    struct Config {
        Scene* scene = nullptr;
        Vec3 boundsMin{-1.0f, -1.0f, 0.0f};
        Vec3 boundsMax{ 1.0f,  1.0f, 0.0f};
        float totalHeight = 200.0f;
        float isoLevel = 0.0f;
        bool contoursVisible = true;
        std::string meshObjectName = "WaveStackIsoMesh";
        std::string bboxObjectName = "WaveStackBounds";
        std::string exportMeshPath = "waveStackMesh.obj";
        std::string exportContoursPath = "waveStackContours.obj";
        std::string exportFieldsPath = "waveStackFields.json";
    };

    struct UIConfig {
        SimpleUI* ui = nullptr;
        Vec2 sliderTotalHeightPos{20.0f, 20.0f};
        Vec2 sliderIsoPos{20.0f, 50.0f};
        float sliderWidth = 260.0f;

        Vec2 toggleSmoothPos{20.0f, 100.0f};
        Vec2 toggleLaplacianPos{160.0f, 100.0f};
        Vec2 toggleBuildMeshPos{20.0f, 130.0f};
        Vec2 toggleMeshVisiblePos{160.0f, 130.0f};
        Vec2 toggleContoursPos{300.0f, 130.0f};
        Vec2 toggleExportPos{20.0f, 160.0f};
        Vec2 toggleSize{120.0f, 22.0f};
    };

    struct Callbacks {
        std::function<void(const std::string&)> onStatusChanged;
    };

    FieldStack();
    bool initialize(const Config& config, const Callbacks& callbacks);
    void attachUI(const UIConfig& uiConfig);
    void shutdown();

    void appendSlice(ScalarField2D&& slice);
    void replaceSlices(std::vector<ScalarField2D>&& slices);
    void clearSlices();

    size_t size() const { return stackFields_.size(); }
    bool empty() const { return stackFields_.empty(); }

    void smoothInPlane();
    void applyStackLaplacian(int iterations);

    bool buildVolumeMesh();
    bool exportAll();

    void setTotalHeight(float height);
    float totalHeight() const { return totalHeight_; }

    void setIsoLevel(float iso);
    float isoLevel() const { return isoLevel_; }

    void setMeshVisible(bool visible);
    bool meshVisible() const { return meshVisible_; }

    void setContoursVisible(bool visible);
    bool contoursVisible() const { return contoursVisible_; }
    void setSpineGraph(const std::shared_ptr<GraphObject>& spine);

    void setExportPaths(const std::string& meshPath,
                        const std::string& contoursPath,
                        const std::string& fieldPath);

    void update();

    std::vector<std::shared_ptr<GraphObject>> stackContours_;

private:
    void rebuildBoundingBox();
    void regenerateContours();
    void updateContoursVisibility();
    void updateContourPlacement();
    void alignSlicesToSpine();
    void clearContourObjects();
    void invalidateVolumeMesh();
    static void smoothField(ScalarField2D& field);
    void emitStatus(const std::string& message);

private:
    Config config_;
    UIConfig uiConfig_;
    Callbacks callbacks_;

    Scene* scene_ = nullptr;
    Vec3 boundsMin_{-1.0f, -1.0f, 0.0f};
    Vec3 boundsMax_{ 1.0f,  1.0f, 0.0f};

    std::shared_ptr<MeshObject> bboxObject_;
    std::shared_ptr<MeshObject> meshObject_;

    std::vector<ScalarField2D> stackFields_;
    std::unique_ptr<ScalarField3D> volumeField_;
    std::shared_ptr<GraphObject> spineGraph_;

    float sliceSpacing_ = 1.0f;
    float totalHeight_ = 200.0f;
    float isoLevel_ = 0.0f;
    size_t alignedSliceCount_ = 0;

    bool meshVisible_ = false;
    bool meshGenerated_ = false;
    bool contoursVisible_ = true;

    std::string exportMeshPath_;
    std::string exportContoursPath_;
    std::string exportFieldsPath_;

    bool ready_ = false;

    bool requestBuildMesh_ = false;
    bool requestExport_ = false;
    bool requestSmooth_ = false;
    bool requestLaplacian_ = false;
    bool lastMeshVisible_ = false;
    bool lastContoursVisible_ = true;
    float lastTotalHeight_ = std::numeric_limits<float>::quiet_NaN();
    float lastIsoLevel_ = std::numeric_limits<float>::quiet_NaN();
};

} // namespace alice2

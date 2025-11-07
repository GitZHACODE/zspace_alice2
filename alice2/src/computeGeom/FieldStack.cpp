#include "computeGeom/FieldStack.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <nlohmann/json.hpp>

#include <objects/GraphObject.h>
#include <objects/MeshObject.h>

namespace alice2 {

namespace {
    inline UIRect makeToggleRect(const Vec2& pos, const Vec2& size) {
        return UIRect{pos.x, pos.y, size.x, size.y};
    }
}

FieldStack::FieldStack() = default;

bool FieldStack::initialize(const Config& config, const Callbacks& callbacks) {
    config_ = config;
    callbacks_ = callbacks;
    scene_ = config_.scene;
    boundsMin_ = config_.boundsMin;
    boundsMax_ = config_.boundsMax;
    totalHeight_ = config_.totalHeight;
    isoLevel_ = config_.isoLevel;
    contoursVisible_ = config_.contoursVisible;
    exportMeshPath_ = config_.exportMeshPath;
    exportContoursPath_ = config_.exportContoursPath;
    exportFieldsPath_ = config_.exportFieldsPath;

    rebuildBoundingBox();
    meshObject_ = std::make_shared<MeshObject>(config_.meshObjectName);
    meshObject_->setVisible(false);
    meshObject_->setShowEdges(false);
    meshObject_->setShowVertices(false);
    meshObject_->setRenderMode(MeshRenderMode::NormalShaded);
    if (scene_) {
        scene_->addObject(meshObject_);
    }

    lastTotalHeight_ = totalHeight_;
    lastIsoLevel_ = isoLevel_;
    lastMeshVisible_ = meshVisible_;
    lastContoursVisible_ = contoursVisible_;

    ready_ = true;
    emitStatus("Field stack ready");
    return true;
}

void FieldStack::attachUI(const UIConfig& uiConfig) {
    if (!ready_ || !uiConfig.ui) {
        return;
    }
    uiConfig_ = uiConfig;

    uiConfig_.ui->addSlider("Total Height", uiConfig_.sliderTotalHeightPos,
                            uiConfig_.sliderWidth, 0.0f, 500.0f, totalHeight_);
    uiConfig_.ui->addSlider("Iso Level", uiConfig_.sliderIsoPos,
                            uiConfig_.sliderWidth, -0.5f, 0.5f, isoLevel_);

    uiConfig_.ui->addToggle("Smooth*1",
                            makeToggleRect(uiConfig_.toggleSmoothPos, uiConfig_.toggleSize),
                            requestSmooth_);
    uiConfig_.ui->addToggle("Laplacian*5",
                            makeToggleRect(uiConfig_.toggleLaplacianPos, uiConfig_.toggleSize),
                            requestLaplacian_);
    uiConfig_.ui->addToggle("Build Mesh",
                            makeToggleRect(uiConfig_.toggleBuildMeshPos, uiConfig_.toggleSize),
                            requestBuildMesh_);
    uiConfig_.ui->addToggle("Display Mesh",
                            makeToggleRect(uiConfig_.toggleMeshVisiblePos, uiConfig_.toggleSize),
                            meshVisible_);
    uiConfig_.ui->addToggle("Display Contours",
                            makeToggleRect(uiConfig_.toggleContoursPos, uiConfig_.toggleSize),
                            contoursVisible_);
    uiConfig_.ui->addToggle("Export",
                            makeToggleRect(uiConfig_.toggleExportPos, uiConfig_.toggleSize),
                            requestExport_);
}

void FieldStack::shutdown() {
    clearContourObjects();
    if (scene_ && meshObject_) {
        scene_->removeObject(meshObject_);
    }
    if (scene_ && bboxObject_) {
        scene_->removeObject(bboxObject_);
    }
    meshObject_.reset();
    bboxObject_.reset();
    stackFields_.clear();
    volumeField_.reset();
    ready_ = false;
}

void FieldStack::appendSlice(ScalarField2D&& slice) {
    if (!ready_) {
        return;
    }
    stackFields_.emplace_back(std::move(slice));
    regenerateContours();
    invalidateVolumeMesh();
    emitStatus("Slice stored");
}

void FieldStack::replaceSlices(std::vector<ScalarField2D>&& slices) {
    if (!ready_) {
        return;
    }
    stackFields_ = std::move(slices);
    alignedSliceCount_ = 0;
    regenerateContours();
    invalidateVolumeMesh();
    std::ostringstream oss;
    oss << "Stack=" << stackFields_.size();
    emitStatus(oss.str());
}

void FieldStack::clearSlices() {
    if (!ready_) {
        return;
    }
    alignedSliceCount_ = 0;
    stackFields_.clear();
    clearContourObjects();
    invalidateVolumeMesh();
    emitStatus("Stack cleared");
}

void FieldStack::smoothInPlane() {
    if (stackFields_.empty()) {
        emitStatus("No slices");
        return;
    }
    for (auto& field : stackFields_) {
        smoothField(field);
    }
    regenerateContours();
    invalidateVolumeMesh();
    emitStatus("Smoothed");
}

void FieldStack::applyStackLaplacian(int iterations) {
    if (stackFields_.size() < 3) {
        emitStatus("Need >=3 slices");
        return;
    }
    iterations = std::max(1, iterations);
    const size_t n = stackFields_.size();
    std::vector<std::vector<float>> buffers(n);
    for (int iter = 0; iter < iterations; ++iter) {
        for (size_t i = 0; i < n; ++i) {
            buffers[i] = stackFields_[i].get_values();
        }
        for (size_t i = 1; i + 1 < n; ++i) {
            const auto& prev = stackFields_[i - 1].get_values();
            const auto& next = stackFields_[i + 1].get_values();
            auto& dest = buffers[i];
            for (size_t k = 0; k < dest.size(); ++k) {
                dest[k] = 0.5f * (prev[k] + next[k]);
            }
        }
        for (size_t i = 0; i < n; ++i) {
            stackFields_[i].set_values(buffers[i]);
        }
    }
    regenerateContours();
    invalidateVolumeMesh();
    emitStatus("Stack Laplacian");
}

bool FieldStack::buildVolumeMesh() {
    if (stackFields_.empty()) {
        emitStatus("No slices");
        return false;
    }
    const auto res = stackFields_.front().get_resolution();
    const int rx = res.first;
    const int ry = res.second;
    const int rz = int(stackFields_.size());
    if (rx <= 0 || ry <= 0 || rz <= 0) {
        emitStatus("Invalid grid");
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

    if (!stackFields_.empty()) {
        std::vector<Vec3> warpedPoints;
        warpedPoints.reserve(volume.size());
        for (int z = 0; z < rz; ++z) {
            for (int y = 0; y < ry; ++y) {
                for (int x = 0; x < rx; ++x) {
                    warpedPoints.push_back(stackFields_[size_t(z)].cellPosition(x, y));
                }
            }
        }
        volumeField_->set_points(warpedPoints);
    }

    auto meshData = volumeField_->generate_mesh(isoLevel_);
    if (!meshData || meshData->vertices.empty()) {
        meshGenerated_ = false;
        meshVisible_ = false;
        if (meshObject_) {
            meshObject_->setVisible(false);
        }
        rebuildBoundingBox();
        emitStatus("Mesh empty");
        return false;
    }

    if (!meshObject_) {
        meshObject_ = std::make_shared<MeshObject>(config_.meshObjectName);
        meshObject_->setShowEdges(false);
        meshObject_->setShowVertices(false);
        meshObject_->setRenderMode(MeshRenderMode::NormalShaded);
        if (scene_) {
            scene_->addObject(meshObject_);
        }
    }

    meshObject_->setMeshData(meshData);
    meshObject_->setRenderMode(MeshRenderMode::NormalShaded);
    meshObject_->setNormalShadingColors(Color(0.1f, 0.1f, 0.1f), Color(1.0f, 1.0f, 1.0f));
    meshGenerated_ = true;
    meshVisible_ = true;
    meshObject_->setVisible(true);
    rebuildBoundingBox();
    emitStatus("Mesh built");
    return true;
}

bool FieldStack::exportAll() {
    if (stackFields_.empty()) {
        emitStatus("Nothing to export");
        return false;
    }
    if ((!meshGenerated_ || !meshObject_) && !buildVolumeMesh()) {
        emitStatus("Mesh build failed");
        return false;
    }

    if (meshObject_) {
        meshObject_->writeToObj(exportMeshPath_);
    }

    if (!stackContours_.empty()) {
        GraphObject merged("WaveStackContoursExport");
        for (const auto& contour : stackContours_) {
            if (contour) {
                merged.combineWith(*contour);
            }
        }
        merged.weld();
        merged.writeToObj(exportContoursPath_);
    }

    nlohmann::json stackJson;
    stackJson["slice_count"] = stackFields_.size();
    stackJson["iso_level"] = isoLevel_;
    stackJson["total_height"] = totalHeight_;
    stackJson["bounds_min"] = {boundsMin_.x, boundsMin_.y, boundsMin_.z};
    stackJson["bounds_max"] = {boundsMax_.x, boundsMax_.y, boundsMax_.z};

    size_t fieldIdx = 0;
    for (const auto& field : stackFields_) {
        stackJson["scalar_field_values_" + std::to_string(fieldIdx++)] = field.get_values();
    }

    std::ofstream fieldFile(exportFieldsPath_, std::ios::out | std::ios::trunc);
    if (fieldFile.is_open()) {
        fieldFile << stackJson.dump(2);
        fieldFile.close();
    }

    emitStatus("Exported");
    return true;
}

void FieldStack::setTotalHeight(float height) {
    totalHeight_ = std::max(0.0f, height);
}

void FieldStack::setIsoLevel(float iso) {
    isoLevel_ = iso;
}

void FieldStack::setMeshVisible(bool visible) {
    meshVisible_ = visible;
}

void FieldStack::setContoursVisible(bool visible) {
    contoursVisible_ = visible;
}

void FieldStack::setSpineGraph(const std::shared_ptr<GraphObject>& spine) {
    spineGraph_ = spine;
    alignedSliceCount_ = 0;
    alignSlicesToSpine();
    rebuildBoundingBox();
}

void FieldStack::setExportPaths(const std::string& meshPath,
                                const std::string& contoursPath,
                                const std::string& fieldPath) {
    exportMeshPath_ = meshPath;
    exportContoursPath_ = contoursPath;
    exportFieldsPath_ = fieldPath;
}

void FieldStack::update() {
    if (!ready_) {
        return;
    }

    if (requestSmooth_) {
        smoothInPlane();
        requestSmooth_ = false;
    }
    if (requestLaplacian_) {
        applyStackLaplacian(5);
        requestLaplacian_ = false;
    }
    if (requestBuildMesh_) {
        buildVolumeMesh();
        requestBuildMesh_ = false;
    }
    if (requestExport_) {
        exportAll();
        requestExport_ = false;
    }

    if (meshVisible_ != lastMeshVisible_) {
        if (!meshGenerated_) {
            meshVisible_ = false;
        }
        if (meshObject_) {
            meshObject_->setVisible(meshVisible_);
        }
        lastMeshVisible_ = meshVisible_;
    }

    if (contoursVisible_ != lastContoursVisible_) {
        updateContoursVisibility();
        lastContoursVisible_ = contoursVisible_;
    }

    if (std::fabs(totalHeight_ - lastTotalHeight_) > 1e-4f) {
        rebuildBoundingBox();
        updateContourPlacement();
        invalidateVolumeMesh();
        lastTotalHeight_ = totalHeight_;
    }

    if (std::fabs(isoLevel_ - lastIsoLevel_) > 1e-6f) {
        regenerateContours();
        invalidateVolumeMesh();
        lastIsoLevel_ = isoLevel_;
    }
}

void FieldStack::rebuildBoundingBox() {
    if (!scene_) {
        return;
    }
    if (!bboxObject_) {
        bboxObject_ = std::make_shared<MeshObject>(config_.bboxObjectName);
        bboxObject_->setShowFaces(false);
        bboxObject_->setShowEdges(true);
        bboxObject_->setShowVertices(false);
        scene_->addObject(bboxObject_);
    }
    Vec3 minBounds(boundsMin_.x, boundsMin_.y, 0.0f);
    Vec3 maxBounds(boundsMax_.x, boundsMax_.y, std::max(0.001f, totalHeight_));

    if (meshObject_ && meshGenerated_) {
        minBounds = meshObject_->getBoundsMin();
        maxBounds = meshObject_->getBoundsMax();
    } else if (spineGraph_) {
        minBounds = spineGraph_->getBoundsMin();
        maxBounds = spineGraph_->getBoundsMax();
    }

    Vec3 size = maxBounds - minBounds;
    size.x = std::max(size.x, 1e-3f);
    size.y = std::max(size.y, 1e-3f);
    size.z = std::max(size.z, 1e-3f);
    Vec3 center = (minBounds + maxBounds) * 0.5f;

    bboxObject_->createCube(1.0f);
    bboxObject_->scaleMesh(size);
    bboxObject_->translateMesh(center);
    bboxObject_->setShowFaces(false);
}

void FieldStack::regenerateContours() {
    clearContourObjects();
    if (!scene_ || stackFields_.empty()) {
        return;
    }
    stackContours_.reserve(stackFields_.size());
    for (size_t i = 0; i < stackFields_.size(); ++i) {
        GraphObject contour = stackFields_[i].get_contours(isoLevel_);
        contour.setShowVertices(false);
        contour.setShowEdges(true);
        contour.setEdgeWidth(1.2f);

        auto contourPtr = std::make_shared<GraphObject>(std::move(contour));
        contourPtr->setName("WaveStackContour_" + std::to_string(i));
        contourPtr->setVisible(contoursVisible_);
        scene_->addObject(contourPtr);
        stackContours_.emplace_back(std::move(contourPtr));
    }
    updateContoursVisibility();
    updateContourPlacement();
    alignSlicesToSpine();
}

void FieldStack::updateContoursVisibility() {
    for (auto& contour : stackContours_) {
        if (contour) {
            contour->setVisible(contoursVisible_);
        }
    }
}

void FieldStack::updateContourPlacement() {
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
        // contour->getTransform().setTranslation(Vec3(0.0f, 0.0f, float(i) * sliceSpacing_));
    }
}

void FieldStack::alignSlicesToSpine() {
    if (!spineGraph_ || stackContours_.empty() || stackContours_.size() != stackFields_.size()) {
        return;
    }

    const size_t count = stackContours_.size();
    if (alignedSliceCount_ > count) {
        alignedSliceCount_ = count;
    }
    if (alignedSliceCount_ >= count) {
        return;
    }

    GraphObject spineCopy = spineGraph_->duplicate();
    spineCopy.resampleByCount(static_cast<int>(count));
    auto graphData = spineCopy.getGraphData();
    if (!graphData || graphData->vertices.size() < count) {
        return;
    }

    for (size_t i = alignedSliceCount_; i < count; ++i) {
        Vec3 pos_curr = graphData->vertices[i].position;
        Vec3 zAxis;
        if (i < count - 1) {
            Vec3 pos_next = graphData->vertices[i + 1].position;
            zAxis = pos_next - pos_curr;
        } else {
            Vec3 pos_prev = graphData->vertices[i - 1].position;
            zAxis = pos_curr - pos_prev;
        }
        if (zAxis.lengthSquared() < 1e-6f) {
            zAxis = Vec3(0, 0, 1);
        }
        zAxis.normalize();

        Vec3 xAxis(1, 0, 0);
        if (std::abs(xAxis.dot(zAxis)) > 0.99f) {
            xAxis = Vec3(0, 1, 0);
        }
        xAxis.normalize();

        Vec3 yAxis = zAxis.cross(xAxis);
        if (yAxis.lengthSquared() < 1e-6f) {
            yAxis = Vec3(0, 0, 1);
        }
        yAxis.normalize();
        xAxis = yAxis.cross(zAxis).normalized();

        Quaternion q = Quaternion::quatFromBasis(xAxis, yAxis, zAxis);
        Mat4 sliceMatrix = Mat4::translation(pos_curr) * q.toMatrix();

        if (stackContours_[i]) {
            stackContours_[i]->getTransform().setMatrix(sliceMatrix);
            stackContours_[i]->applyTransform();
        }

        stackFields_[i].applyTransform(sliceMatrix);
    }

    alignedSliceCount_ = count;
    invalidateVolumeMesh();
}

void FieldStack::clearContourObjects() {
    if (!scene_) {
        stackContours_.clear();
        return;
    }
    for (auto& contour : stackContours_) {
        if (contour) {
            scene_->removeObject(contour);
        }
    }
    stackContours_.clear();
}

void FieldStack::invalidateVolumeMesh() {
    volumeField_.reset();
    meshGenerated_ = false;
    meshVisible_ = false;
    lastMeshVisible_ = false;
    if (meshObject_) {
        meshObject_->setVisible(false);
    }
    rebuildBoundingBox();
}

void FieldStack::smoothField(ScalarField2D& field) {
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

void FieldStack::emitStatus(const std::string& message) {
    if (callbacks_.onStatusChanged) {
        callbacks_.onStatusChanged(message);
    }
}

} // namespace alice2

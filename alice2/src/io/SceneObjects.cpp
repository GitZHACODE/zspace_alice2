#include "SceneIO.h"
#include "../core/Scene.h"
#include "../objects/MeshObject.h"
#include "../objects/GraphObject.h"
#include <cmath>
#include <stdexcept>

namespace alice2 {

std::shared_ptr<SceneObject> makeSceneObject(SceneItem item) {
    // Transform currently stores TRS only. Bake shear/reflection into geometry
    // rather than silently losing it when flattening a USD hierarchy.
    Transform transform;
    transform.setMatrix(item.transform);
    bool bake = false;
    for (int i = 0; i < 16; ++i)
        bake |= std::abs(transform.getMatrix().m[i] - item.transform.m[i]) > 1e-5f;
    std::shared_ptr<SceneObject> object;
    if (auto* mesh = std::get_if<MeshData>(&item.geometry)) {
        if (bake) {
            for (auto& vertex : mesh->vertices) vertex.position = item.transform.transformPoint(vertex.position);
            mesh->calculateNormals();
        }
        auto result = std::make_shared<MeshObject>(item.name);
        result->setMeshData(std::make_shared<MeshData>(std::move(*mesh)));
        result->setUseFaceColors(item.useFaceColors);
        result->setMaterial(item.material);
        object = result;
    } else {
        auto& graph = std::get<GraphData>(item.geometry);
        if (bake)
            for (auto& vertex : graph.vertices) vertex.position = item.transform.transformPoint(vertex.position);
        auto result = std::make_shared<GraphObject>(item.name);
        result->setGraphData(std::make_shared<GraphData>(std::move(graph)));
        result->setEdgeColor(item.edgeColor);
        object = result;
    }
    if (!bake) object->getTransform().setMatrix(item.transform);
    object->setVisible(item.visible);
    object->setColor(item.color);
    return object;
}

std::size_t loadScene(Scene& scene, const std::filesystem::path& path, bool replace) {
    auto items = readSceneFile(path);
    std::vector<std::shared_ptr<SceneObject>> objects;
    objects.reserve(items.size());
    for (auto& item : items) objects.push_back(makeSceneObject(std::move(item)));
    if (replace) scene.clear();
    for (const auto& object : objects) scene.addObject(object);
    scene.calculateBounds();
    return objects.size();
}

void saveScene(const Scene& scene, const std::filesystem::path& path) {
    std::vector<SceneItem> items;
    for (const auto& object : scene.getObjects()) {
        SceneItem item;
        item.name = object->getName();
        item.transform = object->getTransform().getWorldMatrix();
        item.color = object->getColor();
        item.visible = object->isVisible();
        if (const auto mesh = std::dynamic_pointer_cast<MeshObject>(object)) {
            if (!mesh->getMeshData()) throw std::runtime_error("Cannot export mesh without geometry: " + item.name);
            item.geometry = *mesh->getMeshData();
            item.useFaceColors = mesh->getUseFaceColors();
            item.material = mesh->getMaterial();
        } else if (const auto graph = std::dynamic_pointer_cast<GraphObject>(object)) {
            if (!graph->getGraphData()) throw std::runtime_error("Cannot export graph without geometry: " + item.name);
            item.geometry = *graph->getGraphData();
            item.edgeColor = graph->getEdgeColor();
        } else {
            throw std::runtime_error("Scene export supports meshes and graphs: " + item.name);
        }
        items.push_back(std::move(item));
    }
    writeSceneFile(path, items);
}
} // namespace alice2

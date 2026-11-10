#pragma once

#include "../geometry/MeshData.h"
#include "../geometry/GraphData.h"
#include <filesystem>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace alice2 {

class Scene;
class SceneObject;

// Temporary file-transfer records, using alice2 geometry. No USD types or stage
// survive the read. Coordinates are metres, Z up; transforms are world matrices.
struct SceneItem {
    std::string name;
    Mat4 transform;
    Color color{1, 1, 1, 1};
    Color edgeColor{1, 1, 1, 1}; // GraphObject's uniform edge color.
    bool visible = true;
    bool useFaceColors = false;
    std::variant<MeshData, GraphData> geometry;
};

std::vector<SceneItem> readSceneFile(const std::filesystem::path& path);
void writeSceneFile(const std::filesystem::path& path, const std::vector<SceneItem>& items);

// Viewer bridge. Import appends by default and commits only after a complete read.
std::size_t loadScene(Scene& scene, const std::filesystem::path& path, bool replace = false);
void saveScene(const Scene& scene, const std::filesystem::path& path);
std::shared_ptr<SceneObject> makeSceneObject(SceneItem item);

} // namespace alice2

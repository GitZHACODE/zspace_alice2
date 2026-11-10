#include <alice2/viewer.h>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
bool near(float a, float b) { return std::abs(a - b) < 1e-5f; }
}

int main() {
    const auto directory = std::filesystem::temp_directory_path() /
        ("alice2-objects-check-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    try {
        alice2::Scene scene;
        auto parent = std::make_shared<alice2::MeshObject>("parent");
        parent->createCube();
        parent->getTransform().setTranslation({10, 0, 0});
        auto child = std::make_shared<alice2::MeshObject>("child");
        child->createCube();
        child->getTransform().setTranslation({0, 2, 0});
        child->getTransform().setParent(&parent->getTransform());
        scene.addObject(parent); scene.addObject(child);
        const auto path = directory / "scene.usdc";
        alice2::saveScene(scene, path);
        alice2::Scene restored;
        require(alice2::loadScene(restored, path) == 2, "Scene import count changed");
        const auto object = restored.findObject("child");
        const auto world = object->getTransform().getWorldPosition();
        require(near(world.x, 10) && near(world.y, 2), "Parent world transform lost");
        require(restored.getBoundsMin().x > 9 && restored.getBoundsMax().y > 2,
                "Scene bounds ignore imported transforms");
        require(alice2::loadScene(restored, path) == 2 && restored.getObjectCount() == 4, "Append failed");
        require(alice2::loadScene(restored, path, true) == 2 && restored.getObjectCount() == 2, "Replace failed");
        const auto broken = directory / "broken.usda";
        std::ofstream(broken) << R"USD(#usda 1.0
def Mesh "Bad" {
    point3f[] points = [(0,0,0)]
    int[] faceVertexCounts = [3]
    int[] faceVertexIndices = [0,8,9]
}
)USD";
        bool rejected = false;
        try { alice2::loadScene(restored, broken, true); }
        catch (const std::runtime_error&) { rejected = true; }
        require(rejected && restored.getObjectCount() == 2, "Failed import mutated scene");
        alice2::SceneItem sheared;
        alice2::GraphData graph;
        graph.addVertex({1, 2, 3});
        sheared.geometry = graph;
        sheared.transform.m[4] = 0.5f;
        const auto converted = std::dynamic_pointer_cast<alice2::GraphObject>(alice2::makeSceneObject(sheared));
        const auto actual = converted->getTransform().getWorldMatrix().transformPoint(converted->getGraphData()->vertices[0].position);
        const auto expected = sheared.transform.transformPoint(graph.vertices[0].position);
        require(near(actual.x, expected.x) && near(actual.y, expected.y), "Shear lost during viewer conversion");
        child->getTransform().setParent(nullptr);
        std::filesystem::remove_all(directory);
        std::cout << "Scene object conversion, bounds and transactional import passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove_all(directory);
        std::cerr << error.what() << '\n'; return 1;
    }
}

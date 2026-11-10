#include <alice2/scene_io.h>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool near(float a, float b) { return std::abs(a - b) < 1e-5f; }
}

int main() {
    const auto directory = std::filesystem::temp_directory_path() /
        ("alice2-scene-check-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    try {
        alice2::SceneItem meshItem;
        meshItem.name = "part with spaces / unicode";
        meshItem.transform = alice2::Mat4::translation({2, 3, 4}) * alice2::Mat4::rotation({0, 0, 1}, 0.4f);
        meshItem.color = {0.2f, 0.4f, 0.6f, 0.5f};
        meshItem.visible = false;
        meshItem.useFaceColors = true;
        auto& mesh = std::get<alice2::MeshData>(meshItem.geometry);
        for (auto p : {alice2::Vec3(0, 0, 0), alice2::Vec3(1, 0, 0), alice2::Vec3(1, 1, 0), alice2::Vec3(0, 1, 0)})
            mesh.vertices.emplace_back(p);
        mesh.vertices[1].color = {0.1f, 0.7f, 0.8f, 0.3f};
        mesh.faces.emplace_back(std::vector<int>{0, 1, 2, 3});
        mesh.faces[0].color = {1, 0, 0, 0.7f};
        mesh.calculateNormals();
        alice2::SceneItem graphItem;
        graphItem.name = "branched graph";
        graphItem.edgeColor = {0.2f, 0.4f, 0.6f, 0.8f};
        alice2::GraphData graph;
        graph.addVertex({0, 0, 0}); graph.addVertex({1, 0, 0});
        graph.addVertex({0, 1, 0}); graph.addVertex({9, 9, 9}); // isolated
        graph.vertices[3].color = {0, 0.5f, 1, 0.2f};
        graph.addEdge(0, 1); graph.addEdge(0, 2); graph.addEdge(0, 1); // duplicate edge
        graphItem.geometry = graph;
        const std::vector<alice2::SceneItem> items{meshItem, graphItem};
        for (const auto* extension : {"scene.usda", "scene.usdc"}) {
            const auto path = directory / extension;
            alice2::writeSceneFile(path, items);
            const auto restored = alice2::readSceneFile(path);
            require(restored.size() == 2, "Object count changed");
            require(restored[0].name == meshItem.name && !restored[0].visible, "Object identity/visibility changed");
            require(restored[0].useFaceColors, "Mesh color mode changed");
            for (int i = 0; i < 16; ++i)
                require(near(restored[0].transform.m[i], meshItem.transform.m[i]), "Transform changed");
            require(near(restored[0].color.a, 0.5f), "Object opacity changed");
            const auto& m = std::get<alice2::MeshData>(restored[0].geometry);
            require(m.faces.size() == 1 && m.faces[0].vertices == mesh.faces[0].vertices, "N-gon topology changed");
            require(m.edges.size() == 4, "Mesh boundary edges missing");
            require(near(m.vertices[1].color.a, 0.3f) && near(m.faces[0].color.a, 0.7f), "Mesh colors changed");
            const auto& g = std::get<alice2::GraphData>(restored[1].geometry);
            require(near(restored[1].edgeColor.a, 0.8f), "Graph edge color changed");
            require(g.vertices.size() == 4 && g.edges.size() == 3, "Graph topology changed");
            require(g.edges[2].vertexA == 0 && g.edges[2].vertexB == 1, "Duplicate graph edge changed");
            require(near(g.vertices[3].color.a, 0.2f), "Isolated vertex color changed");
        }
        const auto fixture = directory / "external.usda";
        std::ofstream(fixture) << R"USD(#usda 1.0
(
    metersPerUnit = 0.01
    upAxis = "Y"
)
def Xform "Parent" {
    double3 xformOp:translate = (100, 200, 300)
    uniform token[] xformOpOrder = ["xformOp:translate"]
    def Mesh "Quad" (
        prepend apiSchemas = ["MaterialBindingAPI"]
    ) {
        point3f[] points = [(0,0,0), (100,0,0), (100,100,0), (0,100,0)]
        int[] faceVertexCounts = [4]
        int[] faceVertexIndices = [0,1,2,3]
        rel material:binding = </Paint>
    }
    def BasisCurves "Polyline" {
        uniform token type = "linear"
        uniform token wrap = "periodic"
        int[] curveVertexCounts = [3]
        point3f[] points = [(0,0,0), (1,0,0), (1,1,0)]
    }
}
def Material "Paint" {
    token outputs:surface.connect = </Paint/Surface.outputs:surface>
    def Shader "Surface" {
        uniform token info:id = "UsdPreviewSurface"
        color3f inputs:diffuseColor = (0.2, 0.3, 0.4)
        float inputs:opacity = 0.6
        token outputs:surface
    }
}
)USD";
        auto external = alice2::readSceneFile(fixture);
        require(external.size() == 2, "External objects missing");
        auto p = external[0].transform.transformPoint({0, 0, 0});
        require(near(p.x, 1) && near(p.y, -3) && near(p.z, 2), "Units, up axis or parent transform wrong");
        require(near(std::get<alice2::MeshData>(external[0].geometry).vertices[0].color.r, 0.2f), "Basic material color lost");
        require(std::get<alice2::GraphData>(external[1].geometry).edges.size() == 3, "Periodic curve not closed");
        auto invalid = items;
        std::get<alice2::MeshData>(invalid[0].geometry).faces[0].vertices[0] = 999;
        bool rejected = false;
        try { alice2::writeSceneFile(directory / "invalid.usda", invalid); }
        catch (const std::runtime_error&) { rejected = true; }
        require(rejected && !std::filesystem::exists(directory / "invalid.usda"), "Invalid topology accepted");
        std::filesystem::remove_all(directory);
        std::cout << "USD scene round trips and external transforms passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove_all(directory);
        std::cerr << error.what() << '\n';
        return 1;
    }
}

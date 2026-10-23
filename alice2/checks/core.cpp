#include <alice2/core.h>
#include "utils/Runtime.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

int main(int, char** argv) {
    try {
        alice2::initializeRuntime(argv[0]);
        const auto require = [](bool condition, const char* message) {
            if (!condition) throw std::runtime_error(message);
        };
        alice2::MeshData mesh;
        mesh.vertices.emplace_back(alice2::Vec3(0, 0, 0));
        mesh.vertices.emplace_back(alice2::Vec3(1, 0, 0));
        mesh.vertices.emplace_back(alice2::Vec3(1, 1, 0));
        mesh.vertices.emplace_back(alice2::Vec3(0, 1, 0));
        mesh.faces.emplace_back(std::vector<int>{0, 1, 2, 3});
        mesh.calculateNormals();
        mesh.triangulate();
        require(mesh.triangleIndices.size() == 6, "Quad triangulation failed");
        require(std::abs(mesh.faces[0].normal.z - 1) < 1e-6f, "Face normal changed");
        mesh.catmullClarkSmooth(1);
        require(mesh.faces.size() == 4 && mesh.vertices.size() == 9, "Subdivision failed");
        alice2::Vec3 low, high;
        mesh.updateBounds(low, high);
        require(low.x >= 0 && high.x <= 1, "Subdivision bounds changed");
        alice2::GraphData graph;
        graph.addVertex({0, 0, 0});
        graph.addVertex({1, 2, 3});
        require(graph.addEdge(0, 1) == 0, "Graph edge creation failed");
        require(graph.addEdge(0, 9) == -1, "Invalid graph edge accepted");
        graph.updateBounds(low, high);
        require(high.x == 1 && high.y == 2 && high.z == 3, "Graph bounds changed");
        std::cout << "Geometry core checks passed without a viewer\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

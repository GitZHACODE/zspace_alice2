#include "GraphData.h"
#include <algorithm>
#include <iostream>
#include <limits>

namespace alice2 {

    GraphVertex::GraphVertex()
        : position(0.0f, 0.0f, 0.0f)
        , color(1.0f, 1.0f, 1.0f) {
    }

    GraphVertex::GraphVertex(const Vec3& pos, const Color& col)
        : position(pos)
        , color(col) {
    }

    GraphEdge::GraphEdge()
        : vertexA(-1)
        , vertexB(-1) {
    }

    GraphEdge::GraphEdge(int a, int b)
        : vertexA(a)
        , vertexB(b) {
    }

    void GraphData::clear() {
        vertices.clear();
        edges.clear();
    }

    int GraphData::addVertex(const Vec3& position, const Color& color) {
        vertices.emplace_back(position, color);
        return static_cast<int>(vertices.size()) - 1;
    }

    int GraphData::addEdge(int vertexA, int vertexB) {
        if (vertexA < 0 || vertexB < 0 ||
            vertexA >= static_cast<int>(vertices.size()) ||
            vertexB >= static_cast<int>(vertices.size())) {
            std::cout << "GraphData::addEdge: Invalid vertex indices (" << vertexA << ", " << vertexB << ")" << std::endl;
            return -1;
        }

        edges.emplace_back(vertexA, vertexB);
        return static_cast<int>(edges.size()) - 1;
    }

    void GraphData::updateBounds(Vec3& minBounds, Vec3& maxBounds) const {
        if (vertices.empty()) {
            minBounds = Vec3(-0.5f, -0.5f, -0.5f);
            maxBounds = Vec3(0.5f, 0.5f, 0.5f);
            return;
        }

        Vec3 minPoint(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
        Vec3 maxPoint(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());

        for (const auto& vertex : vertices) {
            minPoint.x = std::min(minPoint.x, vertex.position.x);
            minPoint.y = std::min(minPoint.y, vertex.position.y);
            minPoint.z = std::min(minPoint.z, vertex.position.z);

            maxPoint.x = std::max(maxPoint.x, vertex.position.x);
            maxPoint.y = std::max(maxPoint.y, vertex.position.y);
            maxPoint.z = std::max(maxPoint.z, vertex.position.z);
        }

        minBounds = minPoint;
        maxBounds = maxPoint;
    }

} // namespace alice2

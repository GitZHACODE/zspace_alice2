#pragma once

#include "../utils/Math.h"
#include <vector>

namespace alice2 {

    struct GraphVertex {
        Vec3 position;
        Color color;

        GraphVertex();
        GraphVertex(const Vec3& pos, const Color& col = Color(1.0f, 1.0f, 1.0f));
    };

    struct GraphEdge {
        int vertexA;
        int vertexB;

        GraphEdge();
        GraphEdge(int a, int b);
        bool isValid() const { return vertexA >= 0 && vertexB >= 0; }
    };

    struct GraphData {
        std::vector<GraphVertex> vertices;
        std::vector<GraphEdge> edges;

        void clear();
        int addVertex(const Vec3& position, const Color& color = Color(1.0f, 1.0f, 1.0f));
        int addEdge(int vertexA, int vertexB);
        void updateBounds(Vec3& minBounds, Vec3& maxBounds) const;
    };

} // namespace alice2

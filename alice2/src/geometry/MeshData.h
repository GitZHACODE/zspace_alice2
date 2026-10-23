#pragma once

#include "../utils/Math.h"
#include <vector>

namespace alice2 {

    // Vertex data structure
    struct MeshVertex {
        Vec3 position;
        Vec3 normal;
        Color color;
        
        MeshVertex() : position(0, 0, 0), normal(0, 0, 1), color(1, 1, 1) {}
        MeshVertex(const Vec3& pos, const Vec3& norm = Vec3(0, 0, 1), const Color& col = Color(1, 1, 1))
            : position(pos), normal(norm), color(col) {}
    };

    // Edge data structure
    struct MeshEdge {
        int vertexA, vertexB;
        Color color;
        
        MeshEdge() : vertexA(0), vertexB(0), color(1, 1, 1) {}
        MeshEdge(int a, int b, const Color& col = Color(1, 1, 1))
            : vertexA(a), vertexB(b), color(col) {}
    };

    // Face data structure (supports n-gons)
    struct MeshFace {
        std::vector<int> vertices;  // Vertex indices for this face
        Vec3 normal;
        Color color;
        
        MeshFace() : normal(0, 0, 1), color(1, 1, 1) {}
        MeshFace(const std::vector<int>& verts, const Vec3& norm = Vec3(0, 0, 1), const Color& col = Color(1, 1, 1))
            : vertices(verts), normal(norm), color(col) {}
    };

    // Main mesh data structure
    struct MeshData {
        std::vector<MeshVertex> vertices;
        std::vector<MeshEdge> edges;
        std::vector<MeshFace> faces;
        
        // Triangulated data for rendering (generated from n-gon faces)
        std::vector<int> triangleIndices;
        bool triangulationDirty = true;
        
        // Methods
        void clear();
        void calculateNormals();
        void triangulate();
        Vec3 calculateFaceNormal(const MeshFace& face) const;
        void updateBounds(Vec3& minBounds, Vec3& maxBounds) const;
        void chamferVertices(const std::vector<int>& vertexIds,
                             float edgePercentage,
                             bool removeInternalEdges = true);
        void catmullClarkSmooth(int levels,
                                const std::vector<int>& fixedVertices = {},
                                bool fixBoundaryCorners = true);
    };

    MeshData chamferMeshVertices(const MeshData& mesh,
                                 const std::vector<int>& vertexIds,
                                 float edgePercentage,
                                 bool removeInternalEdges = true);
    MeshData catmullClarkSmoothMesh(const MeshData& mesh,
                                    int levels,
                                    const std::vector<int>& fixedVertices = {},
                                    bool fixBoundaryCorners = true);

} // namespace alice2

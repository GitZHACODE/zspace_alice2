#pragma once

#ifndef ALICE2_MESH_OBJECT_H
#define ALICE2_MESH_OBJECT_H

#include "SceneObject.h"
#include "../utils/Math.h"
#include <vector>
#include <memory>

namespace alice2 {

    class Renderer;
    class Camera;

    // Forward declaration for mesh data
    struct MeshData;

    // Mesh rendering modes (simplified)
    enum class MeshRenderMode {
        Wireframe,  // Show mesh edges only
        Lit         // Display using vertex/face colors without lighting
    };



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
    };

    // Main MeshObject class
    class MeshObject : public SceneObject {
    public:
        MeshObject(const std::string& name = "MeshObject");
        virtual ~MeshObject() = default;

        // Type
        ObjectType getType() const override { return ObjectType::Mesh; }

        // Mesh data management
        void setMeshData(std::shared_ptr<MeshData> meshData);
        std::shared_ptr<MeshData> getMeshData() const { return m_meshData; }
        
        // Create simple mesh shapes for testing
        void createCube(float size = 1.0f);
        void createPlane(float width = 1.0f, float height = 1.0f, int subdivisionsX = 1, int subdivisionsY = 1);
        void createSphere(float radius = 1.0f, int segments = 16, int rings = 8);

        // Rendering mode
        void setRenderMode(MeshRenderMode mode) { m_renderMode = mode; }
        MeshRenderMode getRenderMode() const { return m_renderMode; }





        // SceneObject overrides
        void renderImpl(Renderer& renderer, Camera& camera) override;
        void calculateBounds() override;

    private:
        std::shared_ptr<MeshData> m_meshData;
        MeshRenderMode m_renderMode;

        // Rendering methods
        void renderMesh(Renderer& renderer, Camera& camera);
        void renderWireframe(Renderer& renderer);
        void renderLit(Renderer& renderer);

        // Helper methods
        void ensureTriangulation();
    };

} // namespace alice2

#endif // ALICE2_MESH_OBJECT_H

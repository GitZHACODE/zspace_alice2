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

    // Mesh rendering modes
    enum class MeshRenderMode {
        Wireframe,  // Show mesh edges only
        Lit,        // Display using vertex/face colors without lighting
        Shaded      // Basic lighting with shadows using scene lighting
    };

    // Mesh overlay options (can be combined)
    enum class MeshOverlay {
        None = 0,
        Vertices = 1 << 0,  // Overlay point rendering with vertex colors
        Edges = 1 << 1,     // Overlay line rendering with edge colors
        Faces = 1 << 2      // Overlay polygon rendering with face colors
    };

    // Bitwise operations for MeshOverlay
    inline MeshOverlay operator|(MeshOverlay a, MeshOverlay b) {
        return static_cast<MeshOverlay>(static_cast<int>(a) | static_cast<int>(b));
    }

    inline MeshOverlay operator&(MeshOverlay a, MeshOverlay b) {
        return static_cast<MeshOverlay>(static_cast<int>(a) & static_cast<int>(b));
    }

    inline bool hasOverlay(MeshOverlay flags, MeshOverlay overlay) {
        return (flags & overlay) != MeshOverlay::None;
    }

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

        // Overlay options
        void setOverlayOptions(MeshOverlay overlays) { m_overlayOptions = overlays; }
        MeshOverlay getOverlayOptions() const { return m_overlayOptions; }
        void enableOverlay(MeshOverlay overlay) { m_overlayOptions = m_overlayOptions | overlay; }
        void disableOverlay(MeshOverlay overlay) { m_overlayOptions = static_cast<MeshOverlay>(static_cast<int>(m_overlayOptions) & ~static_cast<int>(overlay)); }
        bool hasOverlay(MeshOverlay overlay) const { return alice2::hasOverlay(m_overlayOptions, overlay); }

        // Rendering properties
        void setVertexSize(float size) { m_vertexSize = size; }
        float getVertexSize() const { return m_vertexSize; }
        
        void setEdgeWidth(float width) { m_edgeWidth = width; }
        float getEdgeWidth() const { return m_edgeWidth; }

        // SceneObject overrides
        void renderImpl(Renderer& renderer, Camera& camera) override;
        void calculateBounds() override;

    private:
        std::shared_ptr<MeshData> m_meshData;
        MeshRenderMode m_renderMode;
        MeshOverlay m_overlayOptions;
        
        // Rendering properties
        float m_vertexSize;
        float m_edgeWidth;

        // Rendering methods
        void renderMesh(Renderer& renderer, Camera& camera);
        void renderWireframe(Renderer& renderer);
        void renderLit(Renderer& renderer);
        void renderShaded(Renderer& renderer, Camera& camera);
        
        // Overlay rendering methods
        void renderVertexOverlay(Renderer& renderer);
        void renderEdgeOverlay(Renderer& renderer);
        void renderFaceOverlay(Renderer& renderer);
        
        // Helper methods
        void ensureTriangulation();
    };

} // namespace alice2

#endif // ALICE2_MESH_OBJECT_H

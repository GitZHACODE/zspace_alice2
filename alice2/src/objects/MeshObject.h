#pragma once

#ifndef ALICE2_MESH_OBJECT_H
#define ALICE2_MESH_OBJECT_H

#include "SceneObject.h"
#include "../utils/Math.h"
#include <vector>
#include <memory>
#include <optional>

namespace alice2 {

    class Renderer;
    class Camera;

    // Forward declaration for mesh data
    struct MeshData;

    // Mesh rendering modes
    enum class MeshRenderMode {
        Lit,            // Display using vertex/face colors without lighting
        NormalShaded    // Conceptual lighting based on normal-camera dot product
    };

    enum class MeshExtrudeMode {
        SmoothSurface,
        SmoothSolid,
        DiscreteSolid,
        Stereotomy
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
        MeshObject duplicate() const;
        
        // Create simple mesh shapes for testing
        void createCube(float size = 1.0f);
        void createPlane(float width = 1.0f, float height = 1.0f, int subdivisionsX = 1, int subdivisionsY = 1);
        void createSphere(float radius = 1.0f, int segments = 16, int rings = 8);

        // Create mesh from custom data (for marching cubes and other procedural generation)
        void createFromVerticesAndFaces(const std::vector<Vec3>& positions,
                                       const std::vector<std::vector<int>>& faceIndices,
                                       const std::vector<Vec3>& normals = {},
                                       const std::vector<Color>& colors = {});
        void createFromTriangles(const std::vector<Vec3>& vertices,
                                const std::vector<Vec3>& normals = {},
                                const std::vector<Color>& colors = {});

        // Utility methods for mesh data manipulation
        void generateEdgesFromFaces();
        void recalculateNormals();
        void centerMesh();
        void scaleMesh(const Vec3& scale);
        void translateMesh(const Vec3& offset);
        void applyTransform();

        // Mesh operations
        void weld(float epsilon = 1e-6f);
        void chamferVertices(const std::vector<int>& vertexIds,
                             float edgePercentage,
                             bool removeInternalEdges = true);
        void smoothMesh(int levels,
                                const std::vector<int>& fixedVertices = {},
                                bool fixBoundaryCorners = true);
        void combineWith(const MeshObject &other);
        MeshObject extrudeMesh(float dist,
                               MeshExtrudeMode mode = MeshExtrudeMode::SmoothSurface,
                               const std::vector<Vec3>& vertexDirs = {}) const;
        MeshObject extrudeEdges(float dist,
                                const std::vector<int>& edgeIds,
                                const std::vector<Vec3>& vertexDirs = {}) const;

        // Read & Write
        void readFromObj(const std::string& filename);
        void writeToObj(const std::string& filename);

        // Rendering mode
        void setRenderMode(MeshRenderMode mode) { m_renderMode = mode; }
        MeshRenderMode getRenderMode() const { return m_renderMode; }

        // Normal shading colors
        void setNormalShadingColors(const Color& frontColor, const Color& backColor) {
            m_frontColor = frontColor;
            m_backColor = backColor;
        }
        const Color& getFrontColor() const { return m_frontColor; }
        const Color& getBackColor() const { return m_backColor; }

        // Overlay controls
        void setShowVertices(bool show) { m_showVertices = show; }
        bool getShowVertices() const { return m_showVertices; }

        void setShowEdges(bool show) { m_showEdges = show; }
        bool getShowEdges() const { return m_showEdges; }

        void setShowFaces(bool show) { m_showFaces = show; }
        bool getShowFaces() const { return m_showFaces; }

        // Rendering properties
        void setVertexSize(float size) { m_vertexSize = size; }
        float getVertexSize() const { return m_vertexSize; }

        void setEdgeWidth(float width) { m_edgeWidth = width; }
        float getEdgeWidth() const { return m_edgeWidth; }



        // Analysis
        struct MeshScalarAnalysisResult {
            std::vector<float> vertexValues;
            float minValue = 0.0f;
            float maxValue = 0.0f;
        };

        struct MeshPrincipalCurvatureResult {
            std::vector<float> k1;
            std::vector<float> k2;
            std::vector<Vec3> principalDirections;
            std::vector<Vec3> otherDirections;
            float minK1 = 0.0f;
            float maxK1 = 0.0f;
            float minK2 = 0.0f;
            float maxK2 = 0.0f;
        };

        MeshScalarAnalysisResult gaussianCurvature(bool updateMeshColors = false,
                                                   std::optional<float> remapMin = std::nullopt,
                                                   std::optional<float> remapMax = std::nullopt);
        MeshScalarAnalysisResult meanCurvature(bool updateMeshColors = false,
                                               std::optional<float> remapMin = std::nullopt,
                                               std::optional<float> remapMax = std::nullopt);
        MeshPrincipalCurvatureResult principleCurvature(bool updateMeshColors = false,
                                                        std::optional<float> remapMin = std::nullopt,
                                                        std::optional<float> remapMax = std::nullopt);


        // SceneObject overrides
        void renderImpl(Renderer& renderer, Camera& camera) override;
        void calculateBounds() override;

    private:
        std::shared_ptr<MeshData> m_meshData;
        MeshRenderMode m_renderMode;

        // Normal shading colors
        Color m_frontColor;
        Color m_backColor;

        // Overlay controls
        bool m_showVertices;
        bool m_showEdges;
        bool m_showFaces;

        // Rendering properties
        float m_vertexSize;
        float m_edgeWidth;

        // Rendering methods
        void renderMesh(Renderer& renderer, Camera& camera);
        void renderSceneModeOverride(Renderer& renderer, Camera& camera);
        void renderWireframe(Renderer& renderer);  // Keep for backward compatibility
        void renderLit(Renderer& renderer);
        void renderNormalShaded(Renderer& renderer, Camera& camera);

        // Overlay rendering methods
        void renderVertexOverlay(Renderer& renderer);
        void renderEdgeOverlay(Renderer& renderer);

        // Helper methods
        void ensureTriangulation();
        void printMeshInfo();
    };

} // namespace alice2

#endif // ALICE2_MESH_OBJECT_H

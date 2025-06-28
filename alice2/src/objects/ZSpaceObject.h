#pragma once

#ifndef ALICE2_ZSPACE_OBJECT_H
#define ALICE2_ZSPACE_OBJECT_H

#include "SceneObject.h"

namespace alice2 {

    // Forward declarations for zSpace types (to be implemented later)
    namespace zSpace {
        class zObj;
        class zObjMesh;
        class zObjGraph;
        class zObjPointCloud;
    }

    enum class ZSpaceObjectType {
        Unknown,
        Mesh,
        Graph,
        PointCloud,
        Generic
    };

    class ZSpaceObject : public SceneObject {
    public:
        ZSpaceObject(const std::string& name = "ZSpaceObject");
        ZSpaceObject(void* zspaceObj, ZSpaceObjectType type, const std::string& name = "ZSpaceObject");
        virtual ~ZSpaceObject() = default;

        // Type
        ObjectType getType() const override { return ObjectType::ZSpaceObject; }

        // zSpace object management (placeholder for future implementation)
        void setZSpaceObject(void* zspaceObj);
        void setZSpaceMesh(void* zspaceMesh);
        void setZSpaceGraph(void* zspaceGraph);
        void setZSpacePointCloud(void* zspacePointCloud);

        void* getZSpaceObject() const { return m_zspaceObject; }
        ZSpaceObjectType getZSpaceType() const { return m_zspaceType; }

        // Rendering
        void renderImpl(Renderer& renderer, Camera& camera) override;

        // Update
        void update(float deltaTime) override;

        // Bounds calculation
        void calculateBounds() override;

        // zSpace-specific properties
        void setDisplayVertices(bool display) { m_displayVertices = display; }
        bool getDisplayVertices() const { return m_displayVertices; }

        void setDisplayEdges(bool display) { m_displayEdges = display; }
        bool getDisplayEdges() const { return m_displayEdges; }

        void setDisplayFaces(bool display) { m_displayFaces = display; }
        bool getDisplayFaces() const { return m_displayFaces; }

        void setVertexSize(float size) { m_vertexSize = size; }
        float getVertexSize() const { return m_vertexSize; }

        void setEdgeWidth(float width) { m_edgeWidth = width; }
        float getEdgeWidth() const { return m_edgeWidth; }

    private:
        void* m_zspaceObject;  // Pointer to actual zSpace object (placeholder)
        ZSpaceObjectType m_zspaceType;

        // Display properties
        bool m_displayVertices;
        bool m_displayEdges;
        bool m_displayFaces;
        float m_vertexSize;
        float m_edgeWidth;

        // Helper methods for rendering different zSpace object types
        void renderMesh(Renderer& renderer);
        void renderGraph(Renderer& renderer);
        void renderPointCloud(Renderer& renderer);
        void renderGeneric(Renderer& renderer);

        // Helper methods for bounds calculation
        void calculateMeshBounds();
        void calculateGraphBounds();
        void calculatePointCloudBounds();
    };

} // namespace alice2

#endif // ALICE2_ZSPACE_OBJECT_H

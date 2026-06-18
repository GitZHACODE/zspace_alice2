#include "zSpaceObject.h"
#include "../core/Renderer.h"
#include "../core/Camera.h"

namespace alice2 {

    zSpaceObject::zSpaceObject(const std::string& name)
        : SceneObject(name)
        , m_zspaceObject(nullptr)
        , m_zspaceType(zSpaceObjectType::Unknown)
        , m_displayVertices(true)
        , m_displayEdges(true)
        , m_displayFaces(true)
        , m_vertexSize(3.0f)
        , m_edgeWidth(1.0f)
    {
    }

    zSpaceObject::zSpaceObject(void* zspaceObj, zSpaceObjectType type, const std::string& name)
        : SceneObject(name)
        , m_zspaceObject(zspaceObj)
        , m_zspaceType(type)
        , m_displayVertices(true)
        , m_displayEdges(true)
        , m_displayFaces(type == zSpaceObjectType::Mesh)
        , m_vertexSize(3.0f)
        , m_edgeWidth(1.0f)
    {
        calculateBounds();
    }

    void zSpaceObject::setZSpaceObject(void* zspaceObj) {
        m_zspaceObject = zspaceObj;
        m_zspaceType = zSpaceObjectType::Generic;
        calculateBounds();
    }

    void zSpaceObject::setZSpaceMesh(void* zspaceMesh) {
        m_zspaceObject = zspaceMesh;
        m_zspaceType = zSpaceObjectType::Mesh;
        calculateBounds();
    }

    void zSpaceObject::setZSpaceGraph(void* zspaceGraph) {
        m_zspaceObject = zspaceGraph;
        m_zspaceType = zSpaceObjectType::Graph;
        calculateBounds();
    }

    void zSpaceObject::setZSpacePointCloud(void* zspacePointCloud) {
        m_zspaceObject = zspacePointCloud;
        m_zspaceType = zSpaceObjectType::PointCloud;
        calculateBounds();
    }

    void zSpaceObject::renderImpl(Renderer& renderer, Camera& /*camera*/) {
        if (!m_zspaceObject) {
            // Render a placeholder cube when no zSpace object is attached
            renderer.setColor(Color(0.5f, 0.5f, 0.5f));
            renderer.drawCube(1.0f);
            return;
        }

        renderer.setColor(m_color);

        switch (m_zspaceType) {
            case zSpaceObjectType::Mesh:
                renderMesh(renderer);
                break;
            case zSpaceObjectType::Graph:
                renderGraph(renderer);
                break;
            case zSpaceObjectType::PointCloud:
                renderPointCloud(renderer);
                break;
            case zSpaceObjectType::Generic:
            default:
                renderGeneric(renderer);
                break;
        }
    }

    void zSpaceObject::update(float /*deltaTime*/) {
        // TODO: Update zSpace object if needed
        // For now, just update bounds
        calculateBounds();
    }

    void zSpaceObject::calculateBounds() {
        if (!m_zspaceObject) {
            // Default bounds for placeholder
            setBounds(Vec3(-0.5f, -0.5f, -0.5f), Vec3(0.5f, 0.5f, 0.5f));
            return;
        }

        switch (m_zspaceType) {
            case zSpaceObjectType::Mesh:
                calculateMeshBounds();
                break;
            case zSpaceObjectType::Graph:
                calculateGraphBounds();
                break;
            case zSpaceObjectType::PointCloud:
                calculatePointCloudBounds();
                break;
            default:
                // TODO: Use zSpace object's getBounds method when properly integrated
                // For now, use default bounds
                setBounds(Vec3(-1.0f, -1.0f, -1.0f), Vec3(1.0f, 1.0f, 1.0f));
                break;
        }
    }

    void zSpaceObject::renderMesh(Renderer& renderer) {
        if (m_zspaceType != zSpaceObjectType::Mesh) return;

        // For now, render a placeholder until we implement proper zSpace mesh rendering
        // TODO: Implement actual zSpace mesh rendering using zSpace mesh data
        if (m_displayFaces) {
            renderer.setWireframe(false);
            renderer.drawCube(1.0f);
        }
        if (m_displayEdges) {
            renderer.setWireframe(true);
            renderer.setLineWidth(m_edgeWidth);
            renderer.drawCube(1.0f);
        }
    }

    void zSpaceObject::renderGraph(Renderer& renderer) {
        if (m_zspaceType != zSpaceObjectType::Graph) return;

        // For now, render placeholder lines
        // TODO: Implement actual zSpace graph rendering using zSpace graph data
        renderer.setColor(Color(0.8f, 0.8f, 0.8f));
        renderer.setLineWidth(m_edgeWidth);
        renderer.drawLine(Vec3(-1, 0, 0), Vec3(1, 0, 0));
        renderer.drawLine(Vec3(0, -1, 0), Vec3(0, 1, 0));
        renderer.drawLine(Vec3(0, 0, -1), Vec3(0, 0, 1));
    }

    void zSpaceObject::renderPointCloud(Renderer& renderer) {
        if (m_zspaceType != zSpaceObjectType::PointCloud) return;

        // For now, render placeholder points
        // TODO: Implement actual zSpace point cloud rendering using zSpace point cloud data
        renderer.setPointSize(m_vertexSize);
        for (int i = 0; i < 8; i++) {
            float angle = i * 2.0f * 3.14159f / 8.0f;
            Vec3 pos(std::cos(angle), std::sin(angle), 0);
            renderer.drawPoint(pos);
        }
    }

    void zSpaceObject::renderGeneric(Renderer& renderer) {
        // For generic zSpace objects, try to call their draw method
        // TODO: Implement proper zSpace object drawing
        renderer.drawCube(1.0f);
    }

    void zSpaceObject::calculateMeshBounds() {
        // TODO: Calculate bounds from zSpace mesh vertices
        setBounds(Vec3(-1.0f, -1.0f, -1.0f), Vec3(1.0f, 1.0f, 1.0f));
    }

    void zSpaceObject::calculateGraphBounds() {
        // TODO: Calculate bounds from zSpace graph vertices
        setBounds(Vec3(-1.0f, -1.0f, -1.0f), Vec3(1.0f, 1.0f, 1.0f));
    }

    void zSpaceObject::calculatePointCloudBounds() {
        // TODO: Calculate bounds from zSpace point cloud points
        setBounds(Vec3(-1.0f, -1.0f, -1.0f), Vec3(1.0f, 1.0f, 1.0f));
    }

} // namespace alice2

#include "ZSpaceObject.h"
#include "../core/Renderer.h"
#include "../core/Camera.h"

namespace alice2 {

    ZSpaceObject::ZSpaceObject(const std::string& name)
        : SceneObject(name)
        , m_zspaceObject(nullptr)
        , m_zspaceType(ZSpaceObjectType::Unknown)
        , m_displayVertices(true)
        , m_displayEdges(true)
        , m_displayFaces(true)
        , m_vertexSize(3.0f)
        , m_edgeWidth(1.0f)
    {
    }

    ZSpaceObject::ZSpaceObject(void* zspaceObj, ZSpaceObjectType type, const std::string& name)
        : SceneObject(name)
        , m_zspaceObject(zspaceObj)
        , m_zspaceType(type)
        , m_displayVertices(true)
        , m_displayEdges(true)
        , m_displayFaces(type == ZSpaceObjectType::Mesh)
        , m_vertexSize(3.0f)
        , m_edgeWidth(1.0f)
    {
        calculateBounds();
    }

    void ZSpaceObject::setZSpaceObject(void* zspaceObj) {
        m_zspaceObject = zspaceObj;
        m_zspaceType = ZSpaceObjectType::Generic;
        calculateBounds();
    }

    void ZSpaceObject::setZSpaceMesh(void* zspaceMesh) {
        m_zspaceObject = zspaceMesh;
        m_zspaceType = ZSpaceObjectType::Mesh;
        calculateBounds();
    }

    void ZSpaceObject::setZSpaceGraph(void* zspaceGraph) {
        m_zspaceObject = zspaceGraph;
        m_zspaceType = ZSpaceObjectType::Graph;
        calculateBounds();
    }

    void ZSpaceObject::setZSpacePointCloud(void* zspacePointCloud) {
        m_zspaceObject = zspacePointCloud;
        m_zspaceType = ZSpaceObjectType::PointCloud;
        calculateBounds();
    }

    void ZSpaceObject::renderImpl(Renderer& renderer, Camera& /*camera*/) {
        if (!m_zspaceObject) {
            // Render a placeholder cube when no zSpace object is attached
            renderer.setColor(Vec3(0.5f, 0.5f, 0.5f));
            renderer.drawCube(1.0f);
            return;
        }

        renderer.setColor(m_color);

        switch (m_zspaceType) {
            case ZSpaceObjectType::Mesh:
                renderMesh(renderer);
                break;
            case ZSpaceObjectType::Graph:
                renderGraph(renderer);
                break;
            case ZSpaceObjectType::PointCloud:
                renderPointCloud(renderer);
                break;
            case ZSpaceObjectType::Generic:
            default:
                renderGeneric(renderer);
                break;
        }
    }

    void ZSpaceObject::update(float /*deltaTime*/) {
        // TODO: Update zSpace object if needed
        // For now, just update bounds
        calculateBounds();
    }

    void ZSpaceObject::calculateBounds() {
        if (!m_zspaceObject) {
            // Default bounds for placeholder
            setBounds(Vec3(-0.5f, -0.5f, -0.5f), Vec3(0.5f, 0.5f, 0.5f));
            return;
        }

        switch (m_zspaceType) {
            case ZSpaceObjectType::Mesh:
                calculateMeshBounds();
                break;
            case ZSpaceObjectType::Graph:
                calculateGraphBounds();
                break;
            case ZSpaceObjectType::PointCloud:
                calculatePointCloudBounds();
                break;
            default:
                // TODO: Use zSpace object's getBounds method when properly integrated
                // For now, use default bounds
                setBounds(Vec3(-1.0f, -1.0f, -1.0f), Vec3(1.0f, 1.0f, 1.0f));
                break;
        }
    }

    void ZSpaceObject::renderMesh(Renderer& renderer) {
        if (m_zspaceType != ZSpaceObjectType::Mesh) return;

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

    void ZSpaceObject::renderGraph(Renderer& renderer) {
        if (m_zspaceType != ZSpaceObjectType::Graph) return;

        // For now, render placeholder lines
        // TODO: Implement actual zSpace graph rendering using zSpace graph data
        renderer.setColor(Vec3(0.8f, 0.8f, 0.8f));
        renderer.setLineWidth(m_edgeWidth);
        renderer.drawLine(Vec3(-1, 0, 0), Vec3(1, 0, 0));
        renderer.drawLine(Vec3(0, -1, 0), Vec3(0, 1, 0));
        renderer.drawLine(Vec3(0, 0, -1), Vec3(0, 0, 1));
    }

    void ZSpaceObject::renderPointCloud(Renderer& renderer) {
        if (m_zspaceType != ZSpaceObjectType::PointCloud) return;

        // For now, render placeholder points
        // TODO: Implement actual zSpace point cloud rendering using zSpace point cloud data
        renderer.setPointSize(m_vertexSize);
        for (int i = 0; i < 8; i++) {
            float angle = i * 2.0f * 3.14159f / 8.0f;
            Vec3 pos(std::cos(angle), std::sin(angle), 0);
            renderer.drawPoint(pos);
        }
    }

    void ZSpaceObject::renderGeneric(Renderer& renderer) {
        // For generic zSpace objects, try to call their draw method
        // TODO: Implement proper zSpace object drawing
        renderer.drawCube(1.0f);
    }

    void ZSpaceObject::calculateMeshBounds() {
        // TODO: Calculate bounds from zSpace mesh vertices
        setBounds(Vec3(-1.0f, -1.0f, -1.0f), Vec3(1.0f, 1.0f, 1.0f));
    }

    void ZSpaceObject::calculateGraphBounds() {
        // TODO: Calculate bounds from zSpace graph vertices
        setBounds(Vec3(-1.0f, -1.0f, -1.0f), Vec3(1.0f, 1.0f, 1.0f));
    }

    void ZSpaceObject::calculatePointCloudBounds() {
        // TODO: Calculate bounds from zSpace point cloud points
        setBounds(Vec3(-1.0f, -1.0f, -1.0f), Vec3(1.0f, 1.0f, 1.0f));
    }

} // namespace alice2

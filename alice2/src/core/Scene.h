#pragma once

#ifndef ALICE2_SCENE_H
#define ALICE2_SCENE_H

#include "../utils/Math.h"
#include "../objects/SceneObject.h"
#include "../zspace/zDisplaySettings.h"
#include <vector>
#include <memory>
#include <string>

#if ALICE2_WITH_ZSPACE_CORE
namespace zSpace {
    class zObjectGraph;
    class zObjectMesh;
    class zObjectPointCloud;
}
#endif

namespace alice2 {

    class Camera;
    class Renderer;

    class Scene {
    public:
        Scene();
        ~Scene();

        // Object management
        void addObject(std::shared_ptr<SceneObject> object);
        void removeObject(std::shared_ptr<SceneObject> object);
        void removeObject(const std::string& name);
        std::shared_ptr<SceneObject> findObject(const std::string& name) const;
        void clear();

        // Rendering
        void render(Renderer& renderer, Camera& camera);
        void update(float deltaTime);

#if ALICE2_WITH_ZSPACE_CORE
        void draw(zSpace::zObjectMesh& mesh);
        void draw(zSpace::zObjectMesh& mesh, const zDisplayMeshSetting& display);
        void draw(zSpace::zObjectGraph& graph);
        void draw(zSpace::zObjectGraph& graph, const zDisplayGraphSetting& display);
        void draw(zSpace::zObjectPointCloud& points);
        void draw(zSpace::zObjectPointCloud& points, const zDisplayPointCloudSetting& display);
#endif

        // Scene properties
        void setBackgroundColor(const Color& color) { m_backgroundColor = color; }
        const Color& getBackgroundColor() const { return m_backgroundColor; }

        void setAmbientLight(const Color& color) { m_ambientLight = color; }
        const Color& getAmbientLight() const { return m_ambientLight; }



        // Grid
        void setShowGrid(bool show) { m_showGrid = show; }
        bool getShowGrid() const { return m_showGrid; }
        
        void setGridSize(float size) { m_gridSize = size; }
        float getGridSize() const { return m_gridSize; }
        
        void setGridDivisions(int divisions) { m_gridDivisions = divisions; }
        int getGridDivisions() const { return m_gridDivisions; }

        // Axes
        void setShowAxes(bool show) { m_showAxes = show; }
        bool getShowAxes() const { return m_showAxes; }
        
        void setAxesLength(float length) { m_axesLength = length; }
        float getAxesLength() const { return m_axesLength; }

        // Object access
        const std::vector<std::shared_ptr<SceneObject>>& getObjects() const { return m_objects; }
        size_t getObjectCount() const { return m_objects.size(); }

        // Bounds
        void calculateBounds();
        const Vec3& getBoundsMin() const { return m_boundsMin; }
        const Vec3& getBoundsMax() const { return m_boundsMax; }
        Vec3 getBoundsCenter() const { return (m_boundsMin + m_boundsMax) * 0.5f; }
        Vec3 getBoundsSize() const { return m_boundsMax - m_boundsMin; }

        // Picking/Selection
        std::shared_ptr<SceneObject> pick(const Vec3& rayOrigin, const Vec3& rayDirection) const;
        std::vector<std::shared_ptr<SceneObject>> pickMultiple(const Vec3& rayOrigin, const Vec3& rayDirection) const;

    private:
        std::vector<std::shared_ptr<SceneObject>> m_objects;
        
        // Scene properties
        Color m_backgroundColor;
        Color m_ambientLight;
        
        // Grid settings
        bool m_showGrid;
        float m_gridSize;
        int m_gridDivisions;
        
        // Axes settings
        bool m_showAxes;
        float m_axesLength;
        
        // Bounds
        Vec3 m_boundsMin;
        Vec3 m_boundsMax;
        bool m_boundsDirty;
        Renderer* m_activeRenderer;

        void renderGrid(Renderer& renderer);
        void renderAxes(Renderer& renderer);
    };

} // namespace alice2

#endif // ALICE2_SCENE_H

#pragma once

#ifndef ALICE2_SCENE_OBJECT_H
#define ALICE2_SCENE_OBJECT_H

#include "../core/Transform.h"
#include "../utils/Math.h"
#include <string>
#include <memory>

namespace alice2 {

    class Renderer;
    class Camera;

    enum class ObjectType {
        Unknown,
        Primitive,
        ZSpaceObject,
        Mesh,
        PointCloud,
        Graph
    };

    class SceneObject : public std::enable_shared_from_this<SceneObject> {
    public:
        SceneObject(const std::string& name = "SceneObject");
        virtual ~SceneObject() = default;

        // Identity
        void setName(const std::string& name) { m_name = name; }
        const std::string& getName() const { return m_name; }

        void setId(int id) { m_id = id; }
        int getId() const { return m_id; }

        // Type
        virtual ObjectType getType() const { return ObjectType::Unknown; }

        // Transform
        Transform& getTransform() { return m_transform; }
        const Transform& getTransform() const { return m_transform; }

        // Visibility
        void setVisible(bool visible) { m_visible = visible; }
        bool isVisible() const { return m_visible; }

        // Selection
        void setSelected(bool selected) { m_selected = selected; }
        bool isSelected() const { return m_selected; }

        // Color and material
        void setColor(const Color& color) { m_color = color; }
        const Color& getColor() const { return m_color; }

        void setWireframe(bool wireframe) { m_wireframe = wireframe; }
        bool isWireframe() const { return m_wireframe; }

        void setOpacity(float opacity) { m_color.a = clamp(opacity, 0.0f, 1.0f); }
        float getOpacity() const { return m_color.a; }

        // Rendering
        virtual void render(Renderer& renderer, Camera& camera);
        virtual void update(float /*deltaTime*/) {}

        // Bounds
        virtual void calculateBounds() {}
        virtual Vec3 getBoundsMin() const { return m_boundsMin; }
        virtual Vec3 getBoundsMax() const { return m_boundsMax; }
        Vec3 getBoundsCenter() const { return (m_boundsMin + m_boundsMax) * 0.5f; }
        Vec3 getBoundsSize() const { return m_boundsMax - m_boundsMin; }

        // Ray intersection
        virtual bool intersectRay(const Vec3& rayOrigin, const Vec3& rayDirection, float& distance) const;

    protected:
        std::string m_name;
        int m_id;
        Transform m_transform;
        
        bool m_visible;
        bool m_selected;
        
        Color m_color;
        bool m_wireframe;
        
        Vec3 m_boundsMin;
        Vec3 m_boundsMax;

        // Override this for custom rendering
        virtual void renderImpl(Renderer& /*renderer*/, Camera& /*camera*/) {}

        // Helper for bounds calculation
        void setBounds(const Vec3& min, const Vec3& max) {
            m_boundsMin = min;
            m_boundsMax = max;
        }

    private:
        static int s_nextId;
    };

} // namespace alice2

#endif // ALICE2_SCENE_OBJECT_H

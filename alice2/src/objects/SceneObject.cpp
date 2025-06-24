#include "SceneObject.h"
#include "../core/Renderer.h"
#include <algorithm>
#include <limits>

namespace alice2 {

    int SceneObject::s_nextId = 1;

    SceneObject::SceneObject(const std::string& name)
        : m_name(name)
        , m_id(s_nextId++)
        , m_visible(true)
        , m_selected(false)
        , m_color(1.0f, 1.0f, 1.0f)
        , m_wireframe(false)
        , m_opacity(1.0f)
        , m_boundsMin(-1, -1, -1)
        , m_boundsMax(1, 1, 1)
        , m_userData(nullptr)
    {
    }

    void SceneObject::render(Renderer& renderer, Camera& camera) {
        if (!m_visible) return;

        // Apply transform
        renderer.pushMatrix();
        renderer.multMatrix(m_transform.getWorldMatrix());

        // Set material properties
        renderer.setColor(m_color, m_opacity);
        renderer.setWireframe(m_wireframe);

        // Highlight if selected
        if (m_selected) {
            renderer.setColor(Vec3(1.0f, 0.5f, 0.0f), m_opacity); // Orange highlight
        }

        // Call derived class implementation
        renderImpl(renderer, camera);

        // Render children
        for (auto& child : m_children) {
            if (child) {
                child->render(renderer, camera);
            }
        }

        renderer.popMatrix();
    }

    bool SceneObject::intersectRay(const Vec3& rayOrigin, const Vec3& rayDirection, float& distance) const {
        // Default implementation: ray-AABB intersection
        Vec3 worldMin = m_transform.transformPoint(m_boundsMin);
        Vec3 worldMax = m_transform.transformPoint(m_boundsMax);

        // Ensure min/max are correct
        if (worldMin.x > worldMax.x) std::swap(worldMin.x, worldMax.x);
        if (worldMin.y > worldMax.y) std::swap(worldMin.y, worldMax.y);
        if (worldMin.z > worldMax.z) std::swap(worldMin.z, worldMax.z);

        float tmin = 0.0f;
        float tmax = std::numeric_limits<float>::max();

        // X slab
        if (std::abs(rayDirection.x) < 1e-6f) {
            if (rayOrigin.x < worldMin.x || rayOrigin.x > worldMax.x) return false;
        } else {
            float t1 = (worldMin.x - rayOrigin.x) / rayDirection.x;
            float t2 = (worldMax.x - rayOrigin.x) / rayDirection.x;
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return false;
        }

        // Y slab
        if (std::abs(rayDirection.y) < 1e-6f) {
            if (rayOrigin.y < worldMin.y || rayOrigin.y > worldMax.y) return false;
        } else {
            float t1 = (worldMin.y - rayOrigin.y) / rayDirection.y;
            float t2 = (worldMax.y - rayOrigin.y) / rayDirection.y;
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return false;
        }

        // Z slab
        if (std::abs(rayDirection.z) < 1e-6f) {
            if (rayOrigin.z < worldMin.z || rayOrigin.z > worldMax.z) return false;
        } else {
            float t1 = (worldMin.z - rayOrigin.z) / rayDirection.z;
            float t2 = (worldMax.z - rayOrigin.z) / rayDirection.z;
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return false;
        }

        distance = tmin > 0 ? tmin : tmax;
        return distance > 0;
    }

    void SceneObject::setParent(std::shared_ptr<SceneObject> parent) {
        auto currentParent = m_parent.lock();
        if (currentParent == parent) return;

        if (currentParent) {
            currentParent->removeChild(shared_from_this());
        }

        m_parent = parent;
        if (parent) {
            parent->addChild(shared_from_this());
        }

        // Update transform hierarchy
        if (parent) {
            m_transform.setParent(&parent->m_transform);
        } else {
            m_transform.setParent(nullptr);
        }
    }

    void SceneObject::addChild(std::shared_ptr<SceneObject> child) {
        if (!child || child.get() == this) return;

        auto it = std::find(m_children.begin(), m_children.end(), child);
        if (it == m_children.end()) {
            m_children.push_back(child);
            child->m_parent = shared_from_this();
            child->m_transform.setParent(&m_transform);
        }
    }

    void SceneObject::removeChild(std::shared_ptr<SceneObject> child) {
        auto it = std::find(m_children.begin(), m_children.end(), child);
        if (it != m_children.end()) {
            (*it)->m_parent.reset();
            (*it)->m_transform.setParent(nullptr);
            m_children.erase(it);
        }
    }

} // namespace alice2

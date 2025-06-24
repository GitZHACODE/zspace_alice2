#include "Scene.h"
#include "Renderer.h"
#include "Camera.h"
#include <algorithm>
#include <limits>

namespace alice2 {

    Scene::Scene()
        : m_backgroundColor(0.2f, 0.2f, 0.3f)
        , m_ambientLight(0.2f, 0.2f, 0.2f)
        , m_showGrid(true)
        , m_gridSize(10.0f)
        , m_gridDivisions(10)
        , m_showAxes(true)
        , m_axesLength(1.0f)
        , m_boundsMin(-1, -1, -1)
        , m_boundsMax(1, 1, 1)
        , m_boundsDirty(true)
    {
    }

    Scene::~Scene() {
        clear();
    }

    void Scene::addObject(std::shared_ptr<SceneObject> object) {
        if (!object) return;

        auto it = std::find(m_objects.begin(), m_objects.end(), object);
        if (it == m_objects.end()) {
            m_objects.push_back(object);
            m_boundsDirty = true;
        }
    }

    void Scene::removeObject(std::shared_ptr<SceneObject> object) {
        auto it = std::find(m_objects.begin(), m_objects.end(), object);
        if (it != m_objects.end()) {
            m_objects.erase(it);
            m_boundsDirty = true;
        }
    }

    void Scene::removeObject(const std::string& name) {
        auto it = std::find_if(m_objects.begin(), m_objects.end(),
            [&name](const std::shared_ptr<SceneObject>& obj) {
                return obj && obj->getName() == name;
            });
        
        if (it != m_objects.end()) {
            m_objects.erase(it);
            m_boundsDirty = true;
        }
    }

    std::shared_ptr<SceneObject> Scene::findObject(const std::string& name) const {
        auto it = std::find_if(m_objects.begin(), m_objects.end(),
            [&name](const std::shared_ptr<SceneObject>& obj) {
                return obj && obj->getName() == name;
            });
        
        return (it != m_objects.end()) ? *it : nullptr;
    }

    void Scene::clear() {
        m_objects.clear();
        m_boundsDirty = true;
    }

    void Scene::render(Renderer& renderer, Camera& camera) {
        // Set background color
        renderer.clear();
        
        // Set ambient lighting
        renderer.setAmbientLight(m_ambientLight);
        
        // Render grid if enabled
        if (m_showGrid) {
            renderGrid(renderer);
        }
        
        // Render axes if enabled
        if (m_showAxes) {
            renderAxes(renderer);
        }
        
        // Render all objects
        for (auto& object : m_objects) {
            if (object && object->isVisible()) {
                object->render(renderer, camera);
            }
        }
    }

    void Scene::update(float deltaTime) {
        // Update all objects
        for (auto& object : m_objects) {
            if (object) {
                object->update(deltaTime);
            }
        }
    }

    void Scene::calculateBounds() {
        if (m_objects.empty()) {
            m_boundsMin = Vec3(-1, -1, -1);
            m_boundsMax = Vec3(1, 1, 1);
            m_boundsDirty = false;
            return;
        }

        bool first = true;
        for (auto& object : m_objects) {
            if (!object) continue;

            object->calculateBounds();
            Vec3 objMin = object->getBoundsMin();
            Vec3 objMax = object->getBoundsMax();

            if (first) {
                m_boundsMin = objMin;
                m_boundsMax = objMax;
                first = false;
            } else {
                m_boundsMin.x = std::min(m_boundsMin.x, objMin.x);
                m_boundsMin.y = std::min(m_boundsMin.y, objMin.y);
                m_boundsMin.z = std::min(m_boundsMin.z, objMin.z);
                
                m_boundsMax.x = std::max(m_boundsMax.x, objMax.x);
                m_boundsMax.y = std::max(m_boundsMax.y, objMax.y);
                m_boundsMax.z = std::max(m_boundsMax.z, objMax.z);
            }
        }

        m_boundsDirty = false;
    }

    std::shared_ptr<SceneObject> Scene::pick(const Vec3& rayOrigin, const Vec3& rayDirection) const {
        float closestDistance = std::numeric_limits<float>::max();
        std::shared_ptr<SceneObject> closestObject = nullptr;

        for (auto& object : m_objects) {
            if (!object || !object->isVisible()) continue;

            float distance;
            if (object->intersectRay(rayOrigin, rayDirection, distance)) {
                if (distance < closestDistance) {
                    closestDistance = distance;
                    closestObject = object;
                }
            }
        }

        return closestObject;
    }

    std::vector<std::shared_ptr<SceneObject>> Scene::pickMultiple(const Vec3& rayOrigin, const Vec3& rayDirection) const {
        std::vector<std::pair<float, std::shared_ptr<SceneObject>>> hits;

        for (auto& object : m_objects) {
            if (!object || !object->isVisible()) continue;

            float distance;
            if (object->intersectRay(rayOrigin, rayDirection, distance)) {
                hits.emplace_back(distance, object);
            }
        }

        // Sort by distance
        std::sort(hits.begin(), hits.end(),
            [](const auto& a, const auto& b) {
                return a.first < b.first;
            });

        // Extract objects
        std::vector<std::shared_ptr<SceneObject>> result;
        result.reserve(hits.size());
        for (const auto& hit : hits) {
            result.push_back(hit.second);
        }

        return result;
    }

    void Scene::renderGrid(Renderer& renderer) {
        renderer.setColor(Vec3(0.5f, 0.5f, 0.5f));
        renderer.drawGrid(m_gridSize, m_gridDivisions);
    }

    void Scene::renderAxes(Renderer& renderer) {
        renderer.drawAxes(m_axesLength);
    }

} // namespace alice2

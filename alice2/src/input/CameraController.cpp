#include "CameraController.h"
#include "../core/Camera.h"
#include "InputManager.h"
#include <algorithm>
#include <iostream>

namespace alice2 {

    CameraController::CameraController(Camera& camera, InputManager& inputManager)
        : m_camera(camera)
        , m_inputManager(inputManager)
        , m_mode(CameraMode::Orbit)
        , m_orbitCenter(0, 0, 0)
        , m_orbitDistance(15.0f)
        , m_orbitSpeed(2.0f)
        , m_panSpeed(0.2f)
        , m_zoomSpeed(1.0f)
        , m_flySpeed(5.0f)
        , m_mouseSensitivity(0.1f)
        , m_flyPitch(0.0f)
        , m_flyYaw(0.0f)
        , m_invertY(false)
        , m_isDragging(false)
        , m_lastMousePos(0, 0, 0)
    {
        // Initialize orbit parameters based on current camera position
        // This preserves any camera setup done before the controller is created
        initializeFromCurrentCamera();
    }

    void CameraController::update(float deltaTime) {
        switch (m_mode) {
            case CameraMode::Orbit:
                handleOrbitMode(deltaTime);
                break;
            case CameraMode::Fly:
                handleFlyMode(deltaTime);
                break;
            case CameraMode::Pan:
                handlePanMode(deltaTime);
                break;
        }
    }

    void CameraController::setOrbitCenter(const Vec3& center) {
        m_orbitCenter = center;
        updateOrbitCamera();
    }

    void CameraController::setOrbitDistance(float distance) {
        m_orbitDistance = std::max(0.1f, distance);
        updateOrbitCamera();
    }

    void CameraController::orbit(float deltaX, float deltaY) {
        // Use the Camera's orbit method directly
        float adjustedDeltaY = deltaY * (m_invertY ? -1.0f : 1.0f);
        m_camera.orbit(m_orbitCenter, deltaX * m_orbitSpeed, adjustedDeltaY * m_orbitSpeed, m_orbitDistance);
    }

    void CameraController::pan(float deltaX, float deltaY) {
        Vec3 right = m_camera.getRight();
        Vec3 up = m_camera.getUp();

        Vec3 offset = right * deltaX * m_panSpeed + up * deltaY * m_panSpeed;

        if (m_mode == CameraMode::Orbit) {
            m_orbitCenter += offset;
            // Update camera with new orbit center
            m_camera.orbit(m_orbitCenter, 0, 0, m_orbitDistance);
        } else {
            m_camera.getTransform().translate(offset);
        }
    }

    void CameraController::zoom(float delta) {
        if (m_mode == CameraMode::Orbit) {
            dolly(delta * m_zoomSpeed);
        } else {
            m_camera.zoom(delta * m_zoomSpeed);
        }
    }

    void CameraController::dolly(float delta) {
        m_orbitDistance = std::max(0.1f, m_orbitDistance + delta);

        // Update camera with new distance
        m_camera.orbit(m_orbitCenter, 0, 0, m_orbitDistance);
    }

    void CameraController::focusOnBounds(const Vec3& boundsMin, const Vec3& boundsMax) {
        Vec3 center = (boundsMin + boundsMax) * 0.5f;
        Vec3 size = boundsMax - boundsMin;
        float maxSize = std::max({size.x, size.y, size.z});
        
        setOrbitCenter(center);
        setOrbitDistance(maxSize * 2.0f);
    }

    void CameraController::resetToDefault() {
        m_orbitCenter = Vec3(0, 0, 0);
        m_orbitDistance = 15.0f;
        // Reset camera to default Z-up view
        m_camera.orbit(m_orbitCenter, 0, 0, m_orbitDistance);
    }

    void CameraController::handleOrbitMode(float /*deltaTime*/) {
        const MouseState& mouse = m_inputManager.getMouseState();

        // Handle mouse dragging for orbit
        if (m_inputManager.isMouseButtonDown(MouseButton::Left)) {
            if (!m_isDragging) {
                m_isDragging = true;
                m_lastMousePos = mouse.position;
            } else {
                Vec3 delta = mouse.position - m_lastMousePos;
                orbit(delta.x * m_mouseSensitivity * 0.5f, delta.y * m_mouseSensitivity * 0.5f);
                m_lastMousePos = mouse.position;
            }
        } else {
            m_isDragging = false;
        }

        // Handle middle mouse for panning
        if (m_inputManager.isMouseButtonDown(MouseButton::Middle)) {
            Vec3 delta = mouse.delta;
            pan(-delta.x * m_panSpeed * 0.1f, delta.y * m_panSpeed * 0.1f);
        }

        // Handle right mouse for panning as well
        if (m_inputManager.isMouseButtonDown(MouseButton::Right)) {
            Vec3 delta = mouse.delta;
            pan(-delta.x * m_panSpeed * 0.1f, delta.y * m_panSpeed * 0.1f);
        }

        // Handle mouse wheel for zooming
        float wheelDelta = mouse.wheelDelta;
        if (wheelDelta != 0.0f) {
            dolly(-wheelDelta * m_zoomSpeed);
        }
    }

    void CameraController::handleFlyMode(float /*deltaTime*/) {
        // Simplified fly mode - just basic orbit for now
        handleOrbitMode(0.0f);
    }

    void CameraController::handlePanMode(float /*deltaTime*/) {
        const MouseState& mouse = m_inputManager.getMouseState();
        
        // Handle mouse dragging for panning
        if (m_inputManager.isMouseButtonDown(MouseButton::Left)) {
            Vec3 delta = mouse.delta;
            pan(-delta.x * m_panSpeed, delta.y * m_panSpeed);
        }
        
        // Handle mouse wheel for zooming
        float wheelDelta = mouse.wheelDelta;
        if (wheelDelta != 0.0f) {
            zoom(-wheelDelta * m_zoomSpeed);
        }
    }

    void CameraController::updateOrbitCamera() {
        // This method is now simplified - the Camera class handles orbit calculations
        m_camera.orbit(m_orbitCenter, 0, 0, m_orbitDistance);
    }

    void CameraController::updateFlyCamera() {
        // Simplified - not implemented yet
    }

    void CameraController::initializeFromCurrentCamera() {
        // Calculate orbit parameters from current camera position
        Vec3 currentPos = m_camera.getPosition();
        m_orbitCenter = Vec3(0, 0, 0);  // Default to origin
        m_orbitDistance = (currentPos - m_orbitCenter).length();

        if (m_orbitDistance < 0.1f) {
            m_orbitDistance = 15.0f;  // Default distance
        }
    }

} // namespace alice2

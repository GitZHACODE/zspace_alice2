#pragma once

#ifndef ALICE2_CAMERA_CONTROLLER_H
#define ALICE2_CAMERA_CONTROLLER_H

#include "../utils/Math.h"

namespace alice2 {

    class Camera;
    class InputManager;

    enum class CameraMode {
        Orbit,      // Orbit around a center point
        Fly,        // Free-flying camera
        Pan         // Pan and zoom only
    };

    class CameraController {
    public:
        CameraController(Camera& camera, InputManager& inputManager);
        ~CameraController() = default;

        // Update (call once per frame)
        void update(float deltaTime);

        // Camera mode
        void setCameraMode(CameraMode mode) { m_mode = mode; }
        CameraMode getCameraMode() const { return m_mode; }

        // Orbit mode settings
        void setOrbitCenter(const Vec3& center);
        const Vec3& getOrbitCenter() const { return m_orbitCenter; }
        
        void setOrbitDistance(float distance);
        float getOrbitDistance() const { return m_orbitDistance; }
        
        void setOrbitSpeed(float speed) { m_orbitSpeed = speed; }
        float getOrbitSpeed() const { return m_orbitSpeed; }

        // Pan settings
        void setPanSpeed(float speed) { m_panSpeed = speed; }
        float getPanSpeed() const { return m_panSpeed; }

        // Zoom settings
        void setZoomSpeed(float speed) { m_zoomSpeed = speed; }
        float getZoomSpeed() const { return m_zoomSpeed; }

        // Fly mode settings
        void setFlySpeed(float speed) { m_flySpeed = speed; }
        float getFlySpeed() const { return m_flySpeed; }
        
        void setMouseSensitivity(float sensitivity) { m_mouseSensitivity = sensitivity; }
        float getMouseSensitivity() const { return m_mouseSensitivity; }

        // Control settings
        void setInvertY(bool invert) { m_invertY = invert; }
        bool getInvertY() const { return m_invertY; }

        // Manual control
        void orbit(float deltaX, float deltaY);
        void pan(float deltaX, float deltaY);
        void zoom(float delta);
        void dolly(float delta);

        // Utility
        void focusOnBounds(const Vec3& boundsMin, const Vec3& boundsMax);
        void resetToDefault();

    private:
        Camera& m_camera;
        InputManager& m_inputManager;

        CameraMode m_mode;

        // Orbit mode (simplified - Camera class handles the calculations)
        Vec3 m_orbitCenter;
        float m_orbitDistance;
        float m_orbitSpeed;

        // Pan and zoom
        float m_panSpeed;
        float m_zoomSpeed;

        // Fly mode
        float m_flySpeed;
        float m_mouseSensitivity;
        float m_flyPitch;
        float m_flyYaw;

        // Settings
        bool m_invertY;
        bool m_isDragging;
        Vec3 m_lastMousePos;

        // Input handling
        void handleOrbitMode(float deltaTime);
        void handleFlyMode(float deltaTime);
        void handlePanMode(float deltaTime);
        
        void updateOrbitCamera();
        void updateFlyCamera();
        void initializeFromCurrentCamera();
    };

} // namespace alice2

#endif // ALICE2_CAMERA_CONTROLLER_H

#include "CameraController.h"
#include "../core/Camera.h"
#include "InputManager.h"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

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
        , m_cameraFilePath("src/cameras/camera_saves.json")
    {
        // Initialize camera slot tracking
        for (int i = 0; i < 8; ++i) {
            m_cameraSlotUsed[i] = false;
        }

        // Initialize orbit parameters based on current camera position
        // This preserves any camera setup done before the controller is created
        initializeFromCurrentCamera();

        // Load saved cameras from file
        loadCamerasFromFile();
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

    // Camera save/load functionality
    void CameraController::saveCamera(int slot) {
        if (slot < 0 || slot >= 8) {
            std::cerr << "[CAMERA] Invalid camera slot: " << slot << std::endl;
            return;
        }

        m_savedCameras[slot] = getCurrentCameraState();
        m_cameraSlotUsed[slot] = true;

        // Save to file immediately for persistence
        saveCamerasToFile();

        std::cout << "[CAMERA] Camera saved to slot " << (slot + 1) << " (F" << (slot + 1) << ")" << std::endl;
    }

    void CameraController::loadCamera(int slot) {
        if (slot < 0 || slot >= 8) {
            std::cerr << "[CAMERA] Invalid camera slot: " << slot << std::endl;
            return;
        }

        if (!m_cameraSlotUsed[slot]) {
            std::cout << "[CAMERA] No camera saved in slot " << (slot + 1) << " (F" << (slot + 1) << ")" << std::endl;
            return;
        }

        setCameraState(m_savedCameras[slot]);
        std::cout << "[CAMERA] Camera loaded from slot " << (slot + 1) << " (F" << (slot + 1) << ")" << std::endl;
    }

    bool CameraController::hasSavedCamera(int slot) const {
        if (slot < 0 || slot >= 8) {
            return false;
        }
        return m_cameraSlotUsed[slot];
    }

    CameraState CameraController::getCurrentCameraState() const {
        CameraState state;

        // Get camera transform
        state.position = m_camera.getPosition();
        state.rotation = m_camera.getTransform().getRotation();

        // Get camera mode and parameters
        state.mode = m_mode;
        state.orbitCenter = m_orbitCenter;
        state.orbitDistance = m_orbitDistance;

        // Get projection settings
        state.fov = m_camera.getFieldOfView();
        state.nearPlane = m_camera.getNearPlane();
        state.farPlane = m_camera.getFarPlane();

        return state;
    }

    void CameraController::setCameraState(const CameraState& state) {
        // Apply camera mode
        m_mode = state.mode;

        // Apply orbit parameters
        m_orbitCenter = state.orbitCenter;
        m_orbitDistance = state.orbitDistance;

        // Apply camera transform directly - don't let orbit mode override this
        m_camera.setPosition(state.position);
        m_camera.getTransform().setRotation(state.rotation);

        // Apply projection settings (these automatically update the projection matrix)
        m_camera.setFieldOfView(state.fov);
        m_camera.setNearPlane(state.nearPlane);
        m_camera.setFarPlane(state.farPlane);

        // For orbit mode, we need to sync the camera's internal orbit state
        // but NOT recalculate position - the saved state is already correct
        if (m_mode == CameraMode::Orbit) {
            // Update the camera's internal orbit parameters without changing position
            m_camera.setOrbitCenter(state.orbitCenter);
            m_camera.setOrbitDistance(state.orbitDistance);
            // CRITICAL: Sync the camera's internal orbit rotation with the saved rotation
            m_camera.setOrbitRotation(state.rotation);
            // Don't call updateOrbitCamera() as it would override our saved position!
        }

        // Force the view matrix to update immediately by accessing it
        m_camera.pan(0.0f, 0.0f);
    }

    void CameraController::saveCamerasToFile() {
        try {
            // Create directory if it doesn't exist
            std::string directory = m_cameraFilePath.substr(0, m_cameraFilePath.find_last_of("/\\"));

            // Create directory using system call (cross-platform)
            #ifdef _WIN32
                std::string createDirCmd = "mkdir \"" + directory + "\" 2>nul";
                system(createDirCmd.c_str());
            #else
                std::string createDirCmd = "mkdir -p \"" + directory + "\"";
                system(createDirCmd.c_str());
            #endif

            json j;
            j["cameras"] = json::array();

            for (int i = 0; i < 8; ++i) {
                if (m_cameraSlotUsed[i]) {
                    json cameraJson;
                    const CameraState& state = m_savedCameras[i];

                    cameraJson["slot"] = i;
                    cameraJson["position"] = {state.position.x, state.position.y, state.position.z};
                    cameraJson["rotation"] = {state.rotation.x, state.rotation.y, state.rotation.z, state.rotation.w};
                    cameraJson["mode"] = static_cast<int>(state.mode);
                    cameraJson["orbitCenter"] = {state.orbitCenter.x, state.orbitCenter.y, state.orbitCenter.z};
                    cameraJson["orbitDistance"] = state.orbitDistance;
                    cameraJson["fov"] = state.fov;
                    cameraJson["nearPlane"] = state.nearPlane;
                    cameraJson["farPlane"] = state.farPlane;

                    j["cameras"].push_back(cameraJson);
                }
            }

            std::ofstream file(m_cameraFilePath);
            if (file.is_open()) {
                file << j.dump(4); // Pretty print with 4 spaces
                // std::cout << "[CAMERA] Cameras saved to " << m_cameraFilePath << std::endl;
            } else {
                std::cerr << "[CAMERA] Failed to open file for writing: " << m_cameraFilePath << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "[CAMERA] Error saving cameras: " << e.what() << std::endl;
        }
    }

    void CameraController::loadCamerasFromFile() {
        try {
            std::ifstream file(m_cameraFilePath);
            if (!file.is_open()) {
                // File doesn't exist yet, which is fine for first run
                std::cout << "[CAMERA] No saved cameras file found, starting fresh" << std::endl;
                return;
            }

            json j;
            file >> j;

            // Reset all slots
            for (int i = 0; i < 8; ++i) {
                m_cameraSlotUsed[i] = false;
            }

            if (j.contains("cameras") && j["cameras"].is_array()) {
                for (const auto& cameraJson : j["cameras"]) {
                    int slot = cameraJson["slot"];
                    if (slot >= 0 && slot < 8) {
                        CameraState& state = m_savedCameras[slot];

                        // Load position
                        auto pos = cameraJson["position"];
                        state.position = Vec3(pos[0], pos[1], pos[2]);

                        // Load rotation
                        auto rot = cameraJson["rotation"];
                        state.rotation = Quaternion(rot[0], rot[1], rot[2], rot[3]);

                        // Load mode
                        state.mode = static_cast<CameraMode>(cameraJson["mode"]);

                        // Load orbit parameters
                        auto center = cameraJson["orbitCenter"];
                        state.orbitCenter = Vec3(center[0], center[1], center[2]);
                        state.orbitDistance = cameraJson["orbitDistance"];

                        // Load projection settings
                        state.fov = cameraJson["fov"];
                        state.nearPlane = cameraJson["nearPlane"];
                        state.farPlane = cameraJson["farPlane"];

                        m_cameraSlotUsed[slot] = true;
                    }
                }
                std::cout << "[CAMERA] Cameras loaded from " << m_cameraFilePath << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "[CAMERA] Error loading cameras: " << e.what() << std::endl;
        }
    }

} // namespace alice2

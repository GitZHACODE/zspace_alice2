#include "Camera.h"
#include "../utils/OpenGL.h"
#include <cmath>
#include <iostream>

// Debug logging flag - set to true to enable detailed camera matrix logging
#define DEBUG_CAMERA_MATRIX_LOGGING false

namespace alice2 {

    Camera::Camera()
        : m_projectionType(ProjectionType::Perspective)
        , m_fov(45.0f)
        , m_aspectRatio(16.0f / 9.0f)
        , m_nearPlane(0.1f)
        , m_farPlane(1000.0f)
        , m_orthoLeft(-10.0f)
        , m_orthoRight(10.0f)
        , m_orthoBottom(-10.0f)
        , m_orthoTop(10.0f)
        , m_viewDirty(true)
        , m_orbitCenter(0, 0, 0)
        , m_orbitDistance(15.0f)
        , m_orbitRotation(0, 0, 0, 1)  // Identity quaternion
    {
        //std::cout << "[CAMERA] Initializing camera with Z-up coordinate system" << std::endl;

        // Set initial orbit rotation for a good default view
        // Looking down at origin from a 45-degree angle, slightly elevated
        Quaternion yawRotation = Quaternion::fromAxisAngle(ZUp::UP, 45.0f * DEG_TO_RAD);
        Quaternion pitchRotation = Quaternion::fromAxisAngle(ZUp::RIGHT, -25.0f * DEG_TO_RAD);
        m_orbitRotation = yawRotation * pitchRotation;

        //std::cout << "[CAMERA] Initial orbit rotation set: yaw=-45°, pitch=25°" << std::endl;

        updateOrbitPosition();
        updateProjection();

        //std::cout << "[CAMERA] Camera initialization complete" << std::endl;
    }

    void Camera::setPerspective(float fov, float aspect, float nearPlane, float farPlane) {
        m_projectionType = ProjectionType::Perspective;
        m_fov = fov;
        m_aspectRatio = aspect;
        m_nearPlane = nearPlane;
        m_farPlane = farPlane;
        updateProjection();
    }

    void Camera::setOrthographic(float left, float right, float bottom, float top, float nearPlane, float farPlane) {
        m_projectionType = ProjectionType::Orthographic;
        m_orthoLeft = left;
        m_orthoRight = right;
        m_orthoBottom = bottom;
        m_orthoTop = top;
        m_nearPlane = nearPlane;
        m_farPlane = farPlane;
        updateProjection();
    }

    const Mat4& Camera::getViewMatrix() const {
        if (m_viewDirty) {
            if (DEBUG_CAMERA_MATRIX_LOGGING) {
                std::cout << "[CAMERA] View matrix dirty, updating..." << std::endl;
            }
            updateViewMatrix();
        }
        return m_viewMatrix;
    }

    void Camera::orbit(const Vec3& center, float deltaX, float deltaY, float distance) {
        if (DEBUG_CAMERA_MATRIX_LOGGING) {
            std::cout << "[CAMERA] Orbit: deltaX=" << deltaX << ", deltaY=" << deltaY << ", distance=" << distance << std::endl;
        }

        m_orbitCenter = center;
        m_orbitDistance = distance;

        // Yaw: world Z
        Quaternion yawRotation = Quaternion::fromAxisAngle(ZUp::UP, -deltaX * DEG_TO_RAD);

        // Pitch: camera's right
        Vec3 currentRight = m_orbitRotation.rotate(ZUp::RIGHT);
        Quaternion pitchRotation = Quaternion::fromAxisAngle(currentRight, -deltaY * DEG_TO_RAD); // Note the sign

        // Apply as: yaw, then pitch, then current
        m_orbitRotation = yawRotation * pitchRotation * m_orbitRotation;
        m_orbitRotation = m_orbitRotation.normalized();

        if (DEBUG_CAMERA_MATRIX_LOGGING) {
            std::cout << "[CAMERA] New orbit rotation: (" << m_orbitRotation.x << ", " << m_orbitRotation.y << ", " << m_orbitRotation.z << ", " << m_orbitRotation.w << ")" << std::endl;
        }

        updateOrbitPosition();
    }

    void Camera::pan(float deltaX, float deltaY) {
        if (DEBUG_CAMERA_MATRIX_LOGGING) {
            std::cout << "[CAMERA] Pan: deltaX=" << deltaX << ", deltaY=" << deltaY << std::endl;
        }

        // Get camera's right and up vectors for screen-space panning
        Vec3 right = m_transform.right();
        Vec3 up = m_transform.up();

        // Scale pan speed based on distance from target and field of view
        // This provides more intuitive panning that scales with zoom level
        float fovScale = std::tan(m_fov * DEG_TO_RAD * 0.5f);
        float panScale = m_orbitDistance * fovScale * 0.002f;
        Vec3 offset = (right * deltaX + up * deltaY) * panScale;

        // Move both camera and orbit center
        m_orbitCenter += offset;

        if (DEBUG_CAMERA_MATRIX_LOGGING) {
            std::cout << "[CAMERA] Pan offset: (" << offset.x << ", " << offset.y << ", " << offset.z << ")" << std::endl;
            std::cout << "[CAMERA] New orbit center: (" << m_orbitCenter.x << ", " << m_orbitCenter.y << ", " << m_orbitCenter.z << ")" << std::endl;
        }

        updateOrbitPosition();
    }

    void Camera::zoom(float delta) {
        if (m_projectionType == ProjectionType::Perspective) {
            m_fov = clamp(m_fov + delta, 1.0f, 179.0f);
            updateProjection();
        } else {
            float scale = 1.0f + delta * 0.1f;
            m_orthoLeft *= scale;
            m_orthoRight *= scale;
            m_orthoBottom *= scale;
            m_orthoTop *= scale;
            updateProjection();
        }
    }

    void Camera::dolly(float delta) {
        if (DEBUG_CAMERA_MATRIX_LOGGING) {
            std::cout << "[CAMERA] Dolly: delta=" << delta << ", distance before=" << m_orbitDistance << std::endl;
        }

        m_orbitDistance = std::max(0.1f, m_orbitDistance + delta);

        if (DEBUG_CAMERA_MATRIX_LOGGING) {
            std::cout << "[CAMERA] Distance after=" << m_orbitDistance << std::endl;
        }

        updateOrbitPosition();
    }

    Vec3 Camera::screenToWorldRay(float screenX, float screenY, int screenWidth, int screenHeight) const {
        // Convert screen coordinates to normalized device coordinates
        float x = (2.0f * screenX) / screenWidth - 1.0f;
        float y = 1.0f - (2.0f * screenY) / screenHeight;
        
        // Create ray in clip space
        Vec3 rayClip(x, y, -1.0f);
        
        // Transform to view space (simplified - would need proper inverse)
        Vec3 rayView = rayClip;
        rayView.z = -1.0f;
        
        // Transform to world space (simplified)
        Vec3 forward = getForward();
        Vec3 right = getRight();
        Vec3 up = getUp();
        
        Vec3 rayWorld = forward + right * rayView.x + up * rayView.y;
        return rayWorld.normalized();
    }

    Vec3 Camera::worldToScreen(const Vec3& worldPos, int screenWidth, int screenHeight) const {
        Mat4 mvp = getViewProjectionMatrix();
        Vec3 clipPos = mvp.transformPoint(worldPos);
        
        // Convert to screen coordinates
        float x = (clipPos.x + 1.0f) * 0.5f * screenWidth;
        float y = (1.0f - clipPos.y) * 0.5f * screenHeight;
        
        return Vec3(x, y, clipPos.z);
    }

    void Camera::updateProjection() {
        if (m_projectionType == ProjectionType::Perspective) {
            m_projectionMatrix = GLMatrix::perspective(m_fov, m_aspectRatio, m_nearPlane, m_farPlane);
        } else {
            // Orthographic projection matrix
            float rl = m_orthoRight - m_orthoLeft;
            float tb = m_orthoTop - m_orthoBottom;
            float fn = m_farPlane - m_nearPlane;
            
            m_projectionMatrix = Mat4();
            m_projectionMatrix.m[0] = 2.0f / rl;
            m_projectionMatrix.m[5] = 2.0f / tb;
            m_projectionMatrix.m[10] = -2.0f / fn;
            m_projectionMatrix.m[12] = -(m_orthoRight + m_orthoLeft) / rl;
            m_projectionMatrix.m[13] = -(m_orthoTop + m_orthoBottom) / tb;
            m_projectionMatrix.m[14] = -(m_farPlane + m_nearPlane) / fn;
        }
    }

    void Camera::updateOrbitPosition() {
        if (DEBUG_CAMERA_MATRIX_LOGGING) {
            std::cout << "[CAMERA] updateOrbitPosition:" << std::endl;
            std::cout << "  Orbit center: (" << m_orbitCenter.x << ", " << m_orbitCenter.y << ", " << m_orbitCenter.z << ")" << std::endl;
            std::cout << "  Orbit distance: " << m_orbitDistance << std::endl;
            std::cout << "  Orbit rotation: (" << m_orbitRotation.x << ", " << m_orbitRotation.y << ", " << m_orbitRotation.z << ", " << m_orbitRotation.w << ")" << std::endl;
        }

        // Calculate offset from center using quaternion (Z-up: start looking along -Y)
        Vec3 baseOffset(0, -m_orbitDistance, 0);
        Vec3 offset = m_orbitRotation.rotate(baseOffset);

        Vec3 position = m_orbitCenter + offset;
        m_transform.setPosition(position);
        // m_transform.lookAt(m_orbitCenter, ZUp::UP);
        m_transform.setRotation(m_orbitRotation);

        m_viewDirty = true;

        if (DEBUG_CAMERA_MATRIX_LOGGING) {
            std::cout << "  Base offset: (" << baseOffset.x << ", " << baseOffset.y << ", " << baseOffset.z << ")" << std::endl;
            std::cout << "  Rotated offset: (" << offset.x << ", " << offset.y << ", " << offset.z << ")" << std::endl;
            std::cout << "  Camera position: (" << position.x << ", " << position.y << ", " << position.z << ")" << std::endl;
            std::cout << "  View matrix marked dirty" << std::endl;
        }
    }

    void Camera::smoothOrbitTo(const Vec3& center, const Quaternion& targetRotation, float distance, float t) {
        m_orbitCenter = Vec3::lerp(m_orbitCenter, center, t);
        m_orbitDistance = lerp(m_orbitDistance, distance, t);
        m_orbitRotation = Quaternion::slerp(m_orbitRotation, targetRotation, t);

        updateOrbitPosition();
    }

    void Camera::updateViewMatrix() const {
        Vec3 pos = m_transform.getPosition();
        Vec3 forward = m_transform.forward();
        Vec3 right = m_transform.right();
        Vec3 up = m_transform.up();

        if (DEBUG_CAMERA_MATRIX_LOGGING) {
            std::cout << "[CAMERA] updateViewMatrix:" << std::endl;
            std::cout << "  Position: (" << pos.x << ", " << pos.y << ", " << pos.z << ")" << std::endl;
            std::cout << "  Forward: (" << forward.x << ", " << forward.y << ", " << forward.z << ")" << std::endl;
            std::cout << "  Right: (" << right.x << ", " << right.y << ", " << right.z << ")" << std::endl;
            std::cout << "  Up: (" << up.x << ", " << up.y << ", " << up.z << ")" << std::endl;

            // Print transform rotation quaternion
            Quaternion rot = m_transform.getRotation();
            std::cout << "  Transform rotation: (" << rot.x << ", " << rot.y << ", " << rot.z << ", " << rot.w << ")" << std::endl;
        }

        m_viewMatrix = GLMatrix::lookAt(pos, pos + forward, up);
        m_viewDirty = false;

        if (DEBUG_CAMERA_MATRIX_LOGGING) {
            std::cout << "  View matrix updated, dirty flag cleared" << std::endl;
            // Print view matrix elements
            std::cout << "  View matrix:" << std::endl;
            for (int i = 0; i < 4; i++) {
                std::cout << "    [" << m_viewMatrix.m[i*4] << ", " << m_viewMatrix.m[i*4+1] << ", " << m_viewMatrix.m[i*4+2] << ", " << m_viewMatrix.m[i*4+3] << "]" << std::endl;
            }

            // Print projection matrix elements
            std::cout << "  Projection matrix:" << std::endl;
            for (int i = 0; i < 4; i++) {
                std::cout << "    [" << m_projectionMatrix.m[i*4] << ", " << m_projectionMatrix.m[i*4+1] << ", " << m_projectionMatrix.m[i*4+2] << ", " << m_projectionMatrix.m[i*4+3] << "]" << std::endl;
            }

            // Print camera parameters
            std::cout << "  Camera parameters:" << std::endl;
            std::cout << "    FOV: " << m_fov << "°" << std::endl;
            std::cout << "    Aspect ratio: " << m_aspectRatio << std::endl;
            std::cout << "    Near plane: " << m_nearPlane << std::endl;
            std::cout << "    Far plane: " << m_farPlane << std::endl;
            std::cout << "    Projection type: " << (m_projectionType == ProjectionType::Perspective ? "Perspective" : "Orthographic") << std::endl;
        }
    }

} // namespace alice2

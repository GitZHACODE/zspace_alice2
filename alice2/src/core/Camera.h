#pragma once

#ifndef ALICE2_CAMERA_H
#define ALICE2_CAMERA_H

#include "../utils/Math.h"
#include "Transform.h"

namespace alice2 {

    enum class ProjectionType {
        Perspective,
        Orthographic
    };

    class Camera {
    public:
        Camera();
        ~Camera() = default;

        // Transform
        Transform& getTransform() { return m_transform; }
        const Transform& getTransform() const { return m_transform; }

        // Position and orientation
        void setPosition(const Vec3& position) { m_transform.setTranslation(position);}
        Vec3 getPosition() const { return m_transform.getTranslation(); }

        void lookAt(const Vec3 &target, const Vec3 &up = Vec3(0, 0, 1))
        {
            Vec3 upVector = up;
            if (upVector.length() == 0.0f)
            {
                upVector = Vec3(0, 0, 1);
            }
            Vec3 forward = (target - m_transform.getTranslation()).normalized();
            Quaternion lookRot = Quaternion::lookAt(forward, up);
            m_transform.setRotation(lookRot);
            m_viewDirty = true;
        }

        // Projection settings
        void setPerspective(float fov, float aspect, float nearPlane, float farPlane);
        void setOrthographic(float left, float right, float bottom, float top, float nearPlane, float farPlane);
        
        void setFieldOfView(float fov) { m_fov = fov; updateProjection();}
        void setAspectRatio(float aspect) { m_aspectRatio = aspect; updateProjection();}
        void setNearPlane(float nearPlane) { m_nearPlane = nearPlane; updateProjection();}
        void setFarPlane(float farPlane) { m_farPlane = farPlane; updateProjection();}

        float getFieldOfView() const { return m_fov; }
        float getAspectRatio() const { return m_aspectRatio; }
        float getNearPlane() const { return m_nearPlane; }
        float getFarPlane() const { return m_farPlane; }

        // Projection type
        void setProjectionType(ProjectionType type) { m_projectionType = type; updateProjection();}
        ProjectionType getProjectionType() const { return m_projectionType; }

        // Matrices
        const Mat4& getViewMatrix() const;
        const Mat4& getProjectionMatrix() const { return m_projectionMatrix; }
        Mat4 getViewProjectionMatrix() const { return getProjectionMatrix() * getViewMatrix(); }

        // Camera movement
        void orbit(const Vec3& center, float deltaX, float deltaY, float distance);
        void pan(float deltaX, float deltaY);
        void zoom(float delta);
        void dolly(float delta);

        // Ray casting
        Vec3 screenToWorldRay(float screenX, float screenY, int screenWidth, int screenHeight) const;
        Vec3 worldToScreen(const Vec3& worldPos, int screenWidth, int screenHeight) const;

        // Camera controls
        void setOrbitCenter(const Vec3& center) { m_orbitCenter = center;}
        const Vec3& getOrbitCenter() const { return m_orbitCenter; }

        void setOrbitDistance(float distance) { m_orbitDistance = distance; updateOrbitPosition();}
        float getOrbitDistance() const { return m_orbitDistance; }

        void setOrbitRotation(const Quaternion& rotation) { m_orbitRotation = rotation.normalized();}
        const Quaternion& getOrbitRotation() const { return m_orbitRotation; }

        // Smooth camera interpolation
        void smoothOrbitTo(const Vec3& center, const Quaternion& targetRotation, float distance, float t);

        // Utility
        Vec3 getForward() const { return m_transform.forward(); }
        Vec3 getRight() const { return m_transform.right(); }
        Vec3 getUp() const { return m_transform.up(); }
        void updateCamera();

    private:
        Transform m_transform;
        
        // Projection parameters
        ProjectionType m_projectionType;
        float m_fov;           // Field of view in degrees (for perspective)
        float m_aspectRatio;   // Width / Height
        float m_nearPlane;
        float m_farPlane;
        
        // Orthographic parameters
        float m_orthoLeft, m_orthoRight, m_orthoBottom, m_orthoTop;
        
        // Matrices
        mutable Mat4 m_viewMatrix;
        Mat4 m_projectionMatrix;
        mutable bool m_viewDirty;

        // Orbit controls (quaternion-based for Z-up)
        Vec3 m_orbitCenter;       // Target point (look-at)
        float m_orbitDistance;    // Distance from target
        Quaternion m_orbitRotation; // Quaternion for orbit rotation

        void updateProjection();
        void updateOrbitPosition();
        void updateViewMatrix() const;
    };

} // namespace alice2

#endif // ALICE2_CAMERA_H

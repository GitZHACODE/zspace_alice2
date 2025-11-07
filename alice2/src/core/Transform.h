#pragma once

#ifndef ALICE2_TRANSFORM_H
#define ALICE2_TRANSFORM_H

#include "../utils/Math.h"
#include <vector>

namespace alice2 {

    class Transform {
    public:
        Transform();
        Transform(const Vec3& translation, const Quaternion& rotation = Quaternion(), const Vec3& scale = Vec3(1, 1, 1));

        // Translation
        void setTranslation(const Vec3& translation) { m_translation = translation; markDirty(); }
        const Vec3& getTranslation() const { return m_translation; }

        // Rotation (now using quaternions)
        void setRotation(const Quaternion& rotation) { m_rotation = rotation.normalized(); markDirty(); }
        const Quaternion& getRotation() const { return m_rotation; }

        // Euler angle convenience methods (for backward compatibility)
        void setRotationEuler(const Vec3& euler) {
            m_rotation = Quaternion::fromEuler(euler.x * DEG_TO_RAD, euler.y * DEG_TO_RAD, euler.z * DEG_TO_RAD);
            markDirty();
        }

        // Matrix
        void setMatrix(const Mat4& m);

        Vec3 getRotationEuler() const {
            // Convert quaternion back to Euler angles if needed
            // This is complex and can have gimbal lock issues
            // Better to work with quaternions directly
            return Vec3(0, 0, 0); // Placeholder - implement if needed
        }

        // Scale
        void setScale(const Vec3& scale) { m_scale = scale; markDirty(); }
        void setScale(float uniformScale) { setScale(Vec3(uniformScale, uniformScale, uniformScale)); }
        const Vec3& getScale() const { return m_scale; }

        // Transform operations
        void translate(const Vec3& translation) { m_translation += translation; markDirty(); }
        void rotate(const Quaternion& rotation) { m_rotation = rotation * m_rotation; markDirty(); }
        void rotateAxis(const Vec3& axis, float angle) {
            rotate(Quaternion::fromAxisAngle(axis, angle));
        }
        void scaleBy(const Vec3& scale) {
            m_scale.x *= scale.x;
            m_scale.y *= scale.y;
            m_scale.z *= scale.z;
            markDirty();
        }

        // Matrix operations
        const Mat4& getMatrix() const;
        const Mat4& getWorldMatrix() const;
        Mat4 getInverseMatrix() const;

        // Hierarchy
        void setParent(Transform* parent);
        Transform* getParent() const { return m_parent; }
        void addChild(Transform* child);
        void removeChild(Transform* child);
        const std::vector<Transform*>& getChildren() const { return m_children; }

        // World space operations
        Vec3 getWorldPosition() const;
        Vec3 getWorldScale() const;
        Vec3 transformPoint(const Vec3& point) const;
        Vec3 transformDirection(const Vec3& direction) const;
        Vec3 inverseTransformPoint(const Vec3& point) const;
        Vec3 inverseTransformDirection(const Vec3& direction) const;

        // Utility
        void lookAt(const Vec3& target, const Vec3& up = Vec3(0, 0, 1));
        Vec3 forward() const;
        Vec3 right() const;
        Vec3 up() const;

    private:
        Vec3 m_translation;
        Quaternion m_rotation;  // Now using quaternion instead of Euler angles
        Vec3 m_scale;

        mutable Mat4 m_localMatrix;
        mutable Mat4 m_worldMatrix;
        mutable bool m_dirty;
        mutable bool m_worldDirty;

        Transform* m_parent;
        std::vector<Transform*> m_children;

        void updateMatrix() const;
        void updateWorldMatrix() const;
        void markDirty() {
            m_dirty = true;
            markWorldDirty();
        }
        void markWorldDirty();
    };

} // namespace alice2

#endif // ALICE2_TRANSFORM_H

#include "Transform.h"
#include <algorithm>

namespace alice2 {

    Transform::Transform()
        : m_position(0, 0, 0)
        , m_rotation(0, 0, 0, 1)  // Identity quaternion
        , m_scale(1, 1, 1)
        , m_dirty(true)
        , m_worldDirty(true)
        , m_parent(nullptr)
    {
    }

    Transform::Transform(const Vec3& position, const Quaternion& rotation, const Vec3& scale)
        : m_position(position)
        , m_rotation(rotation)
        , m_scale(scale)
        , m_dirty(true)
        , m_worldDirty(true)
        , m_parent(nullptr)
    {
    }

    const Mat4& Transform::getMatrix() const {
        if (m_dirty) {
            updateMatrix();
        }
        return m_localMatrix;
    }

    const Mat4& Transform::getWorldMatrix() const {
        if (m_worldDirty) {
            updateWorldMatrix();
        }
        return m_worldMatrix;
    }

    Mat4 Transform::getInverseMatrix() const {
        // For TRS matrices: inverse = S^-1 * R^-1 * T^-1
        Mat4 invScale = Mat4::scale(Vec3(1.0f / m_scale.x, 1.0f / m_scale.y, 1.0f / m_scale.z));
        Mat4 invRotation = m_rotation.conjugate().toMatrix();  // Quaternion conjugate is the inverse rotation
        Mat4 invTranslation = Mat4::translation(-m_position);

        return invScale * invRotation * invTranslation;
    }

    void Transform::setParent(Transform* parent) {
        if (m_parent == parent) return;

        if (m_parent) {
            m_parent->removeChild(this);
        }

        m_parent = parent;
        if (m_parent) {
            m_parent->addChild(this);
        }
        markWorldDirty();
    }

    void Transform::addChild(Transform* child) {
        if (!child || child == this) return;
        
        auto it = std::find(m_children.begin(), m_children.end(), child);
        if (it == m_children.end()) {
            m_children.push_back(child);
            child->m_parent = this;
            child->markWorldDirty();
        }
    }

    void Transform::removeChild(Transform* child) {
        auto it = std::find(m_children.begin(), m_children.end(), child);
        if (it != m_children.end()) {
            (*it)->m_parent = nullptr;
            (*it)->markWorldDirty();
            m_children.erase(it);
        }
    }

    Vec3 Transform::getWorldPosition() const {
        if (m_parent) {
            return m_parent->getWorldMatrix().transformPoint(m_position);
        }
        return m_position;
    }

    Vec3 Transform::getWorldScale() const {
        if (m_parent) {
            Vec3 parentScale = m_parent->getWorldScale();
            return Vec3(m_scale.x * parentScale.x, m_scale.y * parentScale.y, m_scale.z * parentScale.z);
        }
        return m_scale;
    }

    Vec3 Transform::transformPoint(const Vec3& point) const {
        return getMatrix().transformPoint(point);
    }

    Vec3 Transform::transformDirection(const Vec3& direction) const {
        // Transform direction using quaternion rotation and scale
        Vec3 scaledDirection = Vec3(direction.x * m_scale.x, direction.y * m_scale.y, direction.z * m_scale.z);
        return m_rotation.rotate(scaledDirection);
    }

    Vec3 Transform::inverseTransformPoint(const Vec3& point) const {
        return getInverseMatrix().transformPoint(point);
    }

    Vec3 Transform::inverseTransformDirection(const Vec3& direction) const {
        Mat4 invMatrix = getInverseMatrix();
        return Vec3(
            invMatrix.m[0] * direction.x + invMatrix.m[4] * direction.y + invMatrix.m[8] * direction.z,
            invMatrix.m[1] * direction.x + invMatrix.m[5] * direction.y + invMatrix.m[9] * direction.z,
            invMatrix.m[2] * direction.x + invMatrix.m[6] * direction.y + invMatrix.m[10] * direction.z
        );
    }

    void Transform::lookAt(const Vec3& target, const Vec3& up) {
        Vec3 forward = (target - m_position).normalized();
        m_rotation = Quaternion::lookAt(forward, up);
        markDirty();
    }

    Vec3 Transform::forward() const {
        // Z-up convention: +Y forward
        return m_rotation.rotate(Vec3(0, 1, 0));
    }

    Vec3 Transform::right() const {
        // Z-up convention: +X right
        return m_rotation.rotate(Vec3(1, 0, 0));
    }

    Vec3 Transform::up() const {
        // Z-up convention: +Z up
        return m_rotation.rotate(Vec3(0, 0, 1));
    }

    void Transform::updateMatrix() const {
        Mat4 translation = Mat4::translation(m_position);
        Mat4 rotation = m_rotation.toMatrix();
        Mat4 scale = Mat4::scale(m_scale);

        m_localMatrix = translation * rotation * scale;
        m_dirty = false;
    }

    void Transform::updateWorldMatrix() const {
        if (m_parent) {
            m_worldMatrix = m_parent->getWorldMatrix() * getMatrix();
        } else {
            m_worldMatrix = getMatrix();
        }
        m_worldDirty = false;
    }

    void Transform::markWorldDirty() {
        m_worldDirty = true;
        for (Transform* child : m_children) {
            child->markWorldDirty();
        }
    }

} // namespace alice2

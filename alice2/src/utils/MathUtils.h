#pragma once

#ifndef ALICE2_MATH_UTILS_H
#define ALICE2_MATH_UTILS_H

#include "Vector.h"
#include "Quaternion.h"
#include <algorithm>
#include <cmath>

namespace alice2 {

    // Utility functions
    inline float clamp(float value, float min, float max) {
        return std::max(min, std::min(max, value));
    }

    inline float lerp(float a, float b, float t) {
        return a + t * (b - a);
    }

    // Z-up coordinate system utility functions
    namespace ZUpUtils {
        // Create a look-at quaternion for Z-up coordinate system
        inline Quaternion lookAtZUp(const Vec3& from, const Vec3& to, const Vec3& up = ZUp::UP) {
            Vec3 forward = (to - from).normalized();
            return Quaternion::lookAt(forward, up);
        }

        // Convert Y-up vector to Z-up vector
        inline Vec3 yUpToZUp(const Vec3& yUpVector) {
            return Vec3(yUpVector.x, yUpVector.z, -yUpVector.y);
        }

        // Convert Z-up vector to Y-up vector
        inline Vec3 zUpToYUp(const Vec3& zUpVector) {
            return Vec3(zUpVector.x, -zUpVector.z, zUpVector.y);
        }

        // Get default camera position for Z-up system
        inline Vec3 getDefaultCameraPosition(float distance = 15.0f) {
            // Position camera at 45-degree angle looking down at origin
            return Vec3(distance * 0.707f, -distance * 0.707f, distance * 0.5f);
        }

        // Create default orbit quaternion for Z-up system
        inline Quaternion getDefaultOrbitRotation() {
            Quaternion yawRotation = Quaternion::fromAxisAngle(ZUp::UP, -45.0f * DEG_TO_RAD);
            Quaternion pitchRotation = Quaternion::fromAxisAngle(ZUp::RIGHT, 25.0f * DEG_TO_RAD);
            return yawRotation * pitchRotation;
        }

        // Ensure a vector is valid for Z-up system (not zero, not parallel to Z)
        inline Vec3 ensureValidUpVector(const Vec3& up) {
            if (up.length() < 1e-6f) {
                return ZUp::UP;
            }
            Vec3 normalized = up.normalized();
            // If too close to Z-axis, use a different up vector
            if (std::abs(normalized.dot(ZUp::UP)) > 0.99f) {
                return ZUp::FORWARD;
            }
            return normalized;
        }
    }

} // namespace alice2

#endif // ALICE2_MATH_UTILS_H

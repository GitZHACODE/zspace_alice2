#pragma once

#ifndef ALICE2_QUATERNION_H
#define ALICE2_QUATERNION_H

#include "Vector.h"
#include "Matrix.h"
#include <cmath>

namespace alice2 {

    // Constants
    const float PI = 3.14159265359f;
    const float DEG_TO_RAD = PI / 180.0f;
    const float RAD_TO_DEG = 180.0f / PI;

    // Quaternion class for Z-up coordinate system
    struct Quaternion {
        float x, y, z, w;

        Quaternion() : x(0), y(0), z(0), w(1) {}
        Quaternion(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}

        // Create from axis-angle
        static Quaternion fromAxisAngle(const Vec3& axis, float angle) {
            float halfAngle = angle * 0.5f;
            float s = std::sin(halfAngle);
            Vec3 normalizedAxis = axis.normalized();
            return Quaternion(
                normalizedAxis.x * s,
                normalizedAxis.y * s,
                normalizedAxis.z * s,
                std::cos(halfAngle)
            );
        }

        // Create from Euler angles (Z-up convention: Z-Y-X order)
        static Quaternion fromEuler(float pitch, float yaw, float roll) {
            float cx = std::cos(pitch * 0.5f);
            float sx = std::sin(pitch * 0.5f);
            float cy = std::cos(yaw * 0.5f);
            float sy = std::sin(yaw * 0.5f);
            float cz = std::cos(roll * 0.5f);
            float sz = std::sin(roll * 0.5f);

            return Quaternion(
                sx * cy * cz - cx * sy * sz,  // x
                cx * sy * cz + sx * cy * sz,  // y
                cx * cy * sz - sx * sy * cz,  // z
                cx * cy * cz + sx * sy * sz   // w
            );
        }

        // Create lookAt quaternion (Z-up)
        static Quaternion lookAt(const Vec3& forward, const Vec3& up = Vec3(0, 0, 1));

        // Convert rotation matrix to quaternion
        static Quaternion fromMatrix(const Mat4& m);

        // Convert to rotation matrix
        Mat4 toMatrix() const;

        // Basic operations
        Quaternion operator*(const Quaternion& q) const {
            return Quaternion(
                w * q.x + x * q.w + y * q.z - z * q.y,
                w * q.y + y * q.w + z * q.x - x * q.z,
                w * q.z + z * q.w + x * q.y - y * q.x,
                w * q.w - x * q.x - y * q.y - z * q.z
            );
        }

        Quaternion normalized() const {
            float length = std::sqrt(x*x + y*y + z*z + w*w);
            if (length < 1e-6f) return Quaternion(0, 0, 0, 1);
            return Quaternion(x/length, y/length, z/length, w/length);
        }

        Quaternion conjugate() const {
            return Quaternion(-x, -y, -z, w);
        }

        // Rotate a vector
        Vec3 rotate(const Vec3& v) const {
            Quaternion q_v(v.x, v.y, v.z, 0);
            Quaternion result = (*this) * q_v * conjugate();
            return Vec3(result.x, result.y, result.z);
        }

        // Spherical linear interpolation
        static Quaternion slerp(const Quaternion& a, const Quaternion& b, float t) {
            Quaternion qa = a.normalized();
            Quaternion qb = b.normalized();

            float dot = qa.x * qb.x + qa.y * qb.y + qa.z * qb.z + qa.w * qb.w;

            // If the dot product is negative, slerp won't take the shorter path.
            if (dot < 0.0f) {
                qb = Quaternion(-qb.x, -qb.y, -qb.z, -qb.w);
                dot = -dot;
            }

            if (dot > 0.9995f) {
                // Linear interpolation for very close quaternions
                return Quaternion(
                    qa.x + t * (qb.x - qa.x),
                    qa.y + t * (qb.y - qa.y),
                    qa.z + t * (qb.z - qa.z),
                    qa.w + t * (qb.w - qa.w)
                ).normalized();
            }

            float theta = std::acos(dot);
            float sinTheta = std::sin(theta);
            float wa = std::sin((1.0f - t) * theta) / sinTheta;
            float wb = std::sin(t * theta) / sinTheta;

            return Quaternion(
                wa * qa.x + wb * qb.x,
                wa * qa.y + wb * qb.y,
                wa * qa.z + wb * qb.z,
                wa * qa.w + wb * qb.w
            );
        }
    };

} // namespace alice2

#endif // ALICE2_QUATERNION_H

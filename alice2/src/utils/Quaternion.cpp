#include "Quaternion.h"

namespace alice2 {

    // Quaternion lookAt implementation (Z-up)
    Quaternion Quaternion::lookAt(const Vec3& forward, const Vec3& up) {
        Vec3 f = forward.normalized();
        Vec3 r = f.cross(up).normalized();
        Vec3 u = r.cross(f).normalized();

        // Build rotation matrix
        Mat4 rotMatrix;
        rotMatrix.m[0] = r.x;  rotMatrix.m[4] = f.x;  rotMatrix.m[8] = u.x;   rotMatrix.m[12] = 0;
        rotMatrix.m[1] = r.y;  rotMatrix.m[5] = f.y;  rotMatrix.m[9] = u.y;   rotMatrix.m[13] = 0;
        rotMatrix.m[2] = r.z;  rotMatrix.m[6] = f.z;  rotMatrix.m[10] = u.z;  rotMatrix.m[14] = 0;
        rotMatrix.m[3] = 0;    rotMatrix.m[7] = 0;    rotMatrix.m[11] = 0;    rotMatrix.m[15] = 1;

        return fromMatrix(rotMatrix);
    }

    // Convert rotation matrix to quaternion
    Quaternion Quaternion::fromMatrix(const Mat4& m) {
        float trace = m.m[0] + m.m[5] + m.m[10];

        if (trace > 0) {
            float s = std::sqrt(trace + 1.0f) * 2.0f;
            return Quaternion(
                (m.m[9] - m.m[6]) / s,
                (m.m[2] - m.m[8]) / s,
                (m.m[4] - m.m[1]) / s,
                0.25f * s
            );
        } else if (m.m[0] > m.m[5] && m.m[0] > m.m[10]) {
            float s = std::sqrt(1.0f + m.m[0] - m.m[5] - m.m[10]) * 2.0f;
            return Quaternion(
                0.25f * s,
                (m.m[1] + m.m[4]) / s,
                (m.m[2] + m.m[8]) / s,
                (m.m[9] - m.m[6]) / s
            );
        } else if (m.m[5] > m.m[10]) {
            float s = std::sqrt(1.0f + m.m[5] - m.m[0] - m.m[10]) * 2.0f;
            return Quaternion(
                (m.m[1] + m.m[4]) / s,
                0.25f * s,
                (m.m[6] + m.m[9]) / s,
                (m.m[2] - m.m[8]) / s
            );
        } else {
            float s = std::sqrt(1.0f + m.m[10] - m.m[0] - m.m[5]) * 2.0f;
            return Quaternion(
                (m.m[2] + m.m[8]) / s,
                (m.m[6] + m.m[9]) / s,
                0.25f * s,
                (m.m[4] - m.m[1]) / s
            );
        }
    }

    // Convert quaternion to rotation matrix
    Mat4 Quaternion::toMatrix() const {
        float xx = x * x;
        float yy = y * y;
        float zz = z * z;
        float xy = x * y;
        float xz = x * z;
        float yz = y * z;
        float wx = w * x;
        float wy = w * y;
        float wz = w * z;

        Mat4 result;
        result.m[0] = 1 - 2 * (yy + zz);  result.m[4] = 2 * (xy - wz);      result.m[8] = 2 * (xz + wy);       result.m[12] = 0;
        result.m[1] = 2 * (xy + wz);      result.m[5] = 1 - 2 * (xx + zz);  result.m[9] = 2 * (yz - wx);       result.m[13] = 0;
        result.m[2] = 2 * (xz - wy);      result.m[6] = 2 * (yz + wx);      result.m[10] = 1 - 2 * (xx + yy);  result.m[14] = 0;
        result.m[3] = 0;                  result.m[7] = 0;                  result.m[11] = 0;                  result.m[15] = 1;

        return result;
    }

} // namespace alice2

#pragma once

#ifndef ALICE2_MATRIX_H
#define ALICE2_MATRIX_H

#include "Vector.h"
#include <cmath>

namespace alice2 {

    // Forward declaration for Quaternion
    struct Quaternion;

    // 4x4 Matrix class
    struct Mat4 {
        float m[16];

        Mat4() { identity(); }

        Mat4(const float _m[16]){
            for(int i = 0; i < 16; i++){
                m[i] = _m[i];
            }
        };
        
        void identity() {
            for (int i = 0; i < 16; i++) m[i] = 0;
            m[0] = m[5] = m[10] = m[15] = 1.0f;
        }

        static Mat4 translation(const Vec3& t) {
            Mat4 result;
            result.m[12] = t.x;
            result.m[13] = t.y;
            result.m[14] = t.z;
            return result;
        }

        static Mat4 rotation(const Vec3& axis, float angle) {
            Mat4 result;
            float c = std::cos(angle);
            float s = std::sin(angle);
            float t = 1.0f - c;
            
            Vec3 a = axis.normalized();
            
            result.m[0] = t * a.x * a.x + c;
            result.m[1] = t * a.x * a.y + s * a.z;
            result.m[2] = t * a.x * a.z - s * a.y;
            result.m[3] = 0;
            
            result.m[4] = t * a.x * a.y - s * a.z;
            result.m[5] = t * a.y * a.y + c;
            result.m[6] = t * a.y * a.z + s * a.x;
            result.m[7] = 0;
            
            result.m[8] = t * a.x * a.z + s * a.y;
            result.m[9] = t * a.y * a.z - s * a.x;
            result.m[10] = t * a.z * a.z + c;
            result.m[11] = 0;
            
            result.m[12] = result.m[13] = result.m[14] = 0;
            result.m[15] = 1;
            
            return result;
        }

        static Mat4 scale(const Vec3& s) {
            Mat4 result;
            result.m[0] = s.x;
            result.m[5] = s.y;
            result.m[10] = s.z;
            return result;
        }

        Mat4 operator*(const Mat4& other) const {
            Mat4 result;
            for (int col = 0; col < 4; ++col) {
                for (int row = 0; row < 4; ++row) {
                    float sum = 0.0f;
                    for (int k = 0; k < 4; ++k) {
                        sum += m[row + k * 4] * other.m[k + col * 4];
                    }
                    result.m[row + col * 4] = sum;
                }
            }
            return result;
        }

        Vec3 transformPoint(const Vec3& point) const {
            float w = m[3] * point.x + m[7] * point.y + m[11] * point.z + m[15];
            if (std::abs(w) < 1e-6f) {
                w = 1.0f;
            }
            return Vec3(
                (m[0] * point.x + m[4] * point.y + m[8] * point.z + m[12]) / w,
                (m[1] * point.x + m[5] * point.y + m[9] * point.z + m[13]) / w,
                (m[2] * point.x + m[6] * point.y + m[10] * point.z + m[14]) / w
            );
        }
    };

} // namespace alice2

#endif // ALICE2_MATRIX_H

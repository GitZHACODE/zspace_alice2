#pragma once

#ifndef ALICE2_OPENGL_H
#define ALICE2_OPENGL_H

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <string>
#include "Math.h"

namespace alice2 {

    // OpenGL error checking
    inline void checkGLError(const std::string& operation) {
        GLenum error = glGetError();
        if (error != GL_NO_ERROR) {
            std::cerr << "OpenGL Error in " << operation << ": " << error << std::endl;
        }
    }

    // OpenGL state management
    class GLState {
    public:
        static void enableDepthTest() {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
        }

        static void enableBlending() {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }

        static void enableMultisampling() {
            glEnable(GL_MULTISAMPLE);
        }

        static void setClearColor(float r, float g, float b, float a = 1.0f) {
            glClearColor(r, g, b, a);
        }

        static void clear() {
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }

        static void setViewport(int x, int y, int width, int height) {
            glViewport(x, y, width, height);
        }

        static void setLineWidth(float width) {
            glLineWidth(width);
        }

        static void setPointSize(float size) {
            glPointSize(size);
        }
    };

    // Basic drawing utilities
    class GLDraw {
    public:
        static void drawGrid(float size, int divisions, const Color& color = Color(0.5f, 0.5f, 0.5f, 1.0f)) {
            glColor3f(color.r, color.g, color.b);
            glBegin(GL_LINES);

            float step = size / divisions;
            float halfSize = size * 0.5f;

            // Draw grid lines on XY plane (Z=0) for Z-up coordinate system
            for (int i = 0; i <= divisions; i++) {
                float pos = -halfSize + i * step;

                // X-direction lines (parallel to X-axis)
                glVertex3f(-halfSize, pos, 0);
                glVertex3f(halfSize, pos, 0);

                // Y-direction lines (parallel to Y-axis)
                glVertex3f(pos, -halfSize, 0);
                glVertex3f(pos, halfSize, 0);
            }

            glEnd();
        }

        static void drawAxes(float length = 1.0f) {
            glBegin(GL_LINES);
            
            // X-axis (red)
            glColor3f(1.0f, 0.0f, 0.0f);
            glVertex3f(0, 0, 0);
            glVertex3f(length, 0, 0);
            
            // Y-axis (green)
            glColor3f(0.0f, 1.0f, 0.0f);
            glVertex3f(0, 0, 0);
            glVertex3f(0, length, 0);
            
            // Z-axis (blue)
            glColor3f(0.0f, 0.0f, 1.0f);
            glVertex3f(0, 0, 0);
            glVertex3f(0, 0, length);
            
            glEnd();
        }

        static void drawWireCube(float size = 1.0f) {
            float half = size * 0.5f;
            
            glBegin(GL_LINES);
            
            // Bottom face
            glVertex3f(-half, -half, -half); glVertex3f(half, -half, -half);
            glVertex3f(half, -half, -half); glVertex3f(half, -half, half);
            glVertex3f(half, -half, half); glVertex3f(-half, -half, half);
            glVertex3f(-half, -half, half); glVertex3f(-half, -half, -half);
            
            // Top face
            glVertex3f(-half, half, -half); glVertex3f(half, half, -half);
            glVertex3f(half, half, -half); glVertex3f(half, half, half);
            glVertex3f(half, half, half); glVertex3f(-half, half, half);
            glVertex3f(-half, half, half); glVertex3f(-half, half, -half);
            
            // Vertical edges
            glVertex3f(-half, -half, -half); glVertex3f(-half, half, -half);
            glVertex3f(half, -half, -half); glVertex3f(half, half, -half);
            glVertex3f(half, -half, half); glVertex3f(half, half, half);
            glVertex3f(-half, -half, half); glVertex3f(-half, half, half);
            
            glEnd();
        }

        static void drawPoint(const Vec3& position, float size = 1.0f) {
            glPointSize(size);
            glBegin(GL_POINTS);
            glVertex3f(position.x, position.y, position.z);
            glEnd();
        }

        static void drawLine(const Vec3& start, const Vec3& end) {
            glBegin(GL_LINES);
            glVertex3f(start.x, start.y, start.z);
            glVertex3f(end.x, end.y, end.z);
            glEnd();
        }
    };

    // Matrix utilities for OpenGL
    class GLMatrix {
    public:
        static void loadMatrix(const Mat4& matrix) {
            glLoadMatrixf(matrix.m);
        }

        static void multMatrix(const Mat4& matrix) {
            glMultMatrixf(matrix.m);
        }

        static Mat4 perspective(float fovy, float aspect, float nearPlane, float farPlane) {
            Mat4 result;
            float f = 1.0f / std::tan(fovy * DEG_TO_RAD * 0.5f);

            result.m[0] = f / aspect;
            result.m[5] = f;
            result.m[10] = (farPlane + nearPlane) / (nearPlane - farPlane);
            result.m[11] = -1.0f;
            result.m[14] = (2.0f * farPlane * nearPlane) / (nearPlane - farPlane);
            result.m[15] = 0.0f;

            return result;
        }

        static Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
            Vec3 f = (center - eye).normalized();
            Vec3 u = up.normalized();
            Vec3 s = f.cross(u).normalized();
            u = s.cross(f);

            Mat4 result;
            result.m[0] = s.x;
            result.m[4] = s.y;
            result.m[8] = s.z;
            result.m[1] = u.x;
            result.m[5] = u.y;
            result.m[9] = u.z;
            result.m[2] = -f.x;
            result.m[6] = -f.y;
            result.m[10] = -f.z;
            result.m[12] = -s.dot(eye);
            result.m[13] = -u.dot(eye);
            result.m[14] = f.dot(eye);
            
            return result;
        }
    };

} // namespace alice2

#endif // ALICE2_OPENGL_H

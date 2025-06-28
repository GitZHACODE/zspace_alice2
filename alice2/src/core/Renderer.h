#pragma once

#ifndef ALICE2_RENDERER_H
#define ALICE2_RENDERER_H

#include "../utils/Math.h"
#include "../utils/OpenGL.h"
#include <stack>
#include <memory>



namespace alice2 {

    class Camera;
    class FontRenderer;

    enum class RenderMode {
        Points,
        Lines,
        Triangles,
        Wireframe
    };

    class Renderer {
    public:
        Renderer();
        ~Renderer();

        // Initialization
        bool initialize();
        void shutdown();

        // Frame management
        void beginFrame();
        void endFrame();
        void clear();

        // Viewport
        void setViewport(int x, int y, int width, int height);
        void getViewport(int& x, int& y, int& width, int& height) const;

        // Camera setup
        void setCamera(Camera& camera);
        void setupProjection(Camera& camera);
        void setupView(Camera& camera);

        // Matrix stack
        void pushMatrix();
        void popMatrix();
        void loadMatrix(const Mat4& matrix);
        void multMatrix(const Mat4& matrix);
        void loadIdentity();

        // Material properties
        void setColor(const Vec3& color, float alpha = 1.0f);
        void setWireframe(bool wireframe);
        void setPointSize(float size);
        void setLineWidth(float width);

        // Lighting
        void enableLighting(bool enable);
        void setAmbientLight(const Vec3& color);
        void setDirectionalLight(const Vec3& direction, const Vec3& color);

        // Rendering modes
        void setRenderMode(RenderMode mode);
        RenderMode getRenderMode() const { return m_renderMode; }

        // Basic drawing
        void drawPoint(const Vec3& position);
        void drawPoint(const Vec3& position, const Vec3& color, float size = 1.0f);
        void drawLine(const Vec3& start, const Vec3& end);
        void drawLine(const Vec3& start, const Vec3& end, const Vec3& color, float width = 1.0f);
        void drawTriangle(const Vec3& v1, const Vec3& v2, const Vec3& v3);
        void drawTriangle(const Vec3& v1, const Vec3& v2, const Vec3& v3, const Vec3& color);
        void drawQuad(const Vec3& v1, const Vec3& v2, const Vec3& v3, const Vec3& v4);
        void drawQuad(const Vec3& v1, const Vec3& v2, const Vec3& v3, const Vec3& v4, const Vec3& color);

        // Primitive shapes
        void drawCube(float size = 1.0f);
        void drawCube(float size, const Vec3& color);
        void drawSphere(float radius = 1.0f, int segments = 16);
        void drawSphere(float radius, int segments, const Vec3& color);
        void drawCylinder(float radius = 1.0f, float height = 2.0f, int segments = 16);
        void drawCylinder(float radius, float height, int segments, const Vec3& color);
        void drawGrid(float size, int divisions, const Vec3& color = Vec3(0.5f, 0.5f, 0.5f));
        void drawAxes(float length = 1.0f);
        void drawAxes(float length, const Vec3& color);

        // Vertex arrays
        void drawPoints(const Vec3* points, int count);
        void drawLines(const Vec3* points, int count);
        void drawTriangles(const Vec3* vertices, int vertexCount, const int* indices = nullptr, int indexCount = 0);

        // Text rendering
        void drawText(const std::string& text, const Vec3& position, float size = 1.0f);    // 3D billboard text with screen-space sizing
        void drawString(const std::string& text, float x, float y);                         // 2D screen overlay text



        // State queries
        bool isInitialized() const { return m_initialized; }
        const Vec3& getCurrentColor() const { return m_currentColor; }
        float getCurrentAlpha() const { return m_currentAlpha; }

        // Debug
        void checkErrors() const;

    private:
        bool m_initialized;
        
        // Viewport
        int m_viewportX, m_viewportY, m_viewportWidth, m_viewportHeight;
        
        // Matrix stack
        std::stack<Mat4> m_matrixStack;
        
        // Current state
        Vec3 m_currentColor;
        float m_currentAlpha;
        bool m_wireframeMode;
        float m_pointSize;
        float m_lineWidth;
        RenderMode m_renderMode;
        
        // Lighting
        bool m_lightingEnabled;
        Vec3 m_ambientLight;
        Vec3 m_lightDirection;
        Vec3 m_lightColor;

        // Text rendering
        std::unique_ptr<FontRenderer> m_fontRenderer;

        // OpenGL state management
        void setupOpenGL();
        void applyRenderMode();
    };

} // namespace alice2

#endif // ALICE2_RENDERER_H

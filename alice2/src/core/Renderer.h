#pragma once

#ifndef ALICE2_RENDERER_H
#define ALICE2_RENDERER_H

#include "../utils/Math.h"
#include "../utils/OpenGL.h"
#include "../objects/MeshMaterial.h"
#include <stack>
#include <memory>
#include <cstdint>



namespace alice2 {

    class Camera;
    class FontRenderer;
    class GpuMeshRenderer;
    struct MeshData;

    struct MeshRenderStats {
        std::uint64_t uploads = 0;
        std::uint64_t uploadedBytes = 0;
        std::uint64_t drawCalls = 0;
        std::uint64_t triangles = 0;
        std::size_t cachedMeshes = 0;
    };

    enum class RenderMode {
        Points,
        Lines,
        Triangles,
        Wireframe
    };

    enum class SceneRenderMode {
        Regular,
        MeshWireframe,
        MeshWireframeWithVertices,
        MeshNormalShaded,
        MeshTransparent,
        MeshGray,
        MeshFlatShaded
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
        void setContentScale(float scale);
        float getContentScale() const { return m_contentScale; }

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
        void setColor(const Color& color);
        void setWireframe(bool wireframe);
        void setPointSize(float size);
        void setLineWidth(float width);

        // Lighting
        void enableLighting(bool enable);
        void setAmbientLight(const Color& color);
        void setDirectionalLight(const Vec3& direction, const Color& color);
        // Flat mesh lighting uses a camera-space ray direction. Ambient and key
        // light defaults work without creating any scene light objects.

        // Rendering modes
        void setRenderMode(RenderMode mode);
        RenderMode getRenderMode() const { return m_renderMode; }
        void setSceneRenderMode(SceneRenderMode mode);
        SceneRenderMode getSceneRenderMode() const { return m_sceneRenderMode; }
        void setVectorExportMode(bool enabled) { m_vectorExportMode = enabled; }
        bool getVectorExportMode() const { return m_vectorExportMode; }

        // Basic drawing
        void drawPoint(const Vec3& position);
        void drawPoint(const Vec3& position, const Color& color, float size = 1.0f);
        void drawLine(const Vec3& start, const Vec3& end);
        void drawLine(const Vec3& start, const Vec3& end, const Color& color, float width = 1.0f);
        void drawTriangle(const Vec3& v1, const Vec3& v2, const Vec3& v3);
        void drawTriangle(const Vec3& v1, const Vec3& v2, const Vec3& v3, const Color& color);
        void drawQuad(const Vec3& v1, const Vec3& v2, const Vec3& v3, const Vec3& v4);
        void drawQuad(const Vec3& v1, const Vec3& v2, const Vec3& v3, const Vec3& v4, const Color& color);

        // Primitive shapes
        void drawCube(float size = 1.0f);
        void drawCube(float size, const Color& color);
        void drawSphere(float radius = 1.0f, int segments = 16);
        void drawSphere(float radius, int segments, const Color& color);
        void drawCylinder(float radius = 1.0f, float height = 2.0f, int segments = 16);
        void drawCylinder(float radius, float height, int segments, const Color& color);
        void drawGrid(float size, int divisions, const Color& color = Color(0.5f, 0.5f, 0.5f, 1.0f));
        void drawAxes(float length = 1.0f);
        void drawAxes(float length, const Color& color);

        // Vertex arrays
        void drawPoints(const Vec3* points, int count);
        void drawLines(const Vec3* points, int count);
        void drawLines(const Vec3* points, int count, const Color& color, float width = 1.0f);
        void drawTriangles(const Vec3* vertices, int vertexCount, const int* indices = nullptr, int indexCount = 0);

        // Mesh rendering
        // Returns false when the compatibility/export path should be used.
        bool drawMeshBuffered(const std::shared_ptr<MeshData>& mesh, bool normalShaded,
                              const Color& front, const Color& back, bool faceColors,
                              bool showFaces = true, bool showEdges = false, bool showVertices = false,
                              float edgeWidth = 1.0f, float vertexSize = 3.0f, bool flatShaded = false, const MeshMaterial& material = {});
        void setBufferedMeshesEnabled(bool enabled) { m_bufferedMeshesEnabled = enabled; }
        bool getBufferedMeshesEnabled() const { return m_bufferedMeshesEnabled; }
        const MeshRenderStats& getMeshRenderStats() const { return m_meshRenderStats; }
        // Raw arrays stream through reusable GPU buffers; use MeshObject for resident geometry.
        void drawMesh(const Vec3* vertices, const Vec3* normals, const Color* colors, int vertexCount, const int* indices = nullptr, int indexCount = 0, bool enableLighting = false, bool flatShaded = false, const MeshMaterial& material = {});
        void drawMeshWireframe(const Vec3* vertices, const Color* colors, int vertexCount, const int* indices = nullptr, int indexCount = 0);
        void drawMeshEdges(const Vec3* vertices, const int* edgeIndices, const Color* edgeColors, int edgeCount);

        // Text rendering
        void drawText(const std::string& text, const Vec3& position, float size = 1.0f);    // 3D billboard text with screen-space sizing
        void drawString(const std::string& text, float x, float y);                         // 2D screen overlay text

        // 2D screen space drawing (coordinates in pixels)
        void draw2dPoint(const Vec2& position);                                                 // 2D point with current color/size
        void draw2dPoint(const Vec2& position, const Color& color, float size = 1.0f);          // 2D point with specified color/size
        void draw2dLine(const Vec2& start, const Vec2& end);                           // 2D line with current color/width
        void draw2dLine(const Vec2& start, const Vec2& end, const Color& color, float width = 1.0f); // 2D line with specified color/width



        // State queries
        bool isInitialized() const { return m_initialized; }
        const Color& getCurrentColor() const { return m_currentColor; }

        // Debug
        void checkErrors() const;

    private:
        bool m_bufferedMeshesEnabled = true;
        std::shared_ptr<GpuMeshRenderer> m_gpuMeshes;
        MeshRenderStats m_meshRenderStats;
        bool drawMeshArraysBuffered(const Vec3* vertices, const Vec3* normals, const Color* colors,
                                    int vertexCount, const int* indices, int indexCount);
        void initializeMeshBuffers();
        void shutdownMeshBuffers();
        void collectMeshBuffers();
        bool m_initialized;
        
        // Viewport
        int m_viewportX, m_viewportY, m_viewportWidth, m_viewportHeight;
        float m_contentScale;
        
        // Matrix stack
        std::stack<Mat4> m_matrixStack;
        
        // Current state
        Color m_currentColor;
        bool m_wireframeMode;
        float m_pointSize;
        float m_lineWidth;
        RenderMode m_renderMode;
        SceneRenderMode m_sceneRenderMode;
        bool m_vectorExportMode;
        
        // Lighting
        bool m_lightingEnabled;
        Color m_ambientLight;
        Vec3 m_lightDirection;
        Color m_lightColor;

        // Text rendering
        std::unique_ptr<FontRenderer> m_fontRenderer;

        // OpenGL state management
        void setupOpenGL();
        void applyRenderMode();

        // SVG/gl2ps text export
        void drawTextVectorExport(const std::string& text, const Vec3& position, float size);
        void drawStringVectorExport(const std::string& text, float x, float y);
    };

} // namespace alice2

#endif // ALICE2_RENDERER_H

#include "Renderer.h"
#include "Camera.h"
#include "FontRenderer.h"
#include <iostream>
#include <cmath>

// Debug logging flag - set to true to enable detailed renderer logging
#define DEBUG_RENDERER_LOGGING false

namespace alice2 {

    Renderer::Renderer()
        : m_initialized(false)
        , m_viewportX(0)
        , m_viewportY(0)
        , m_viewportWidth(800)
        , m_viewportHeight(600)
        , m_currentColor(1.0f, 1.0f, 1.0f, 1.0f)
        , m_wireframeMode(false)
        , m_pointSize(1.0f)
        , m_lineWidth(1.0f)
        , m_renderMode(RenderMode::Triangles)
        , m_lightingEnabled(true)
        , m_ambientLight(0.2f, 0.2f, 0.2f, 1.0f)
        , m_lightDirection(0.0f, -1.0f, -1.0f)
        , m_lightColor(1.0f, 1.0f, 1.0f, 1.0f)
        , m_fontRenderer(std::make_unique<FontRenderer>())
    {
    }

    Renderer::~Renderer() {
        shutdown();
    }

    bool Renderer::initialize() {
        if (m_initialized) return true;

        // Initialize OpenGL state
        setupOpenGL();

        // Initialize font renderer
        if (!m_fontRenderer->initialize()) {
            std::cerr << "Renderer: Failed to initialize FontRenderer" << std::endl;
            return false;
        }

        // Load default font
        if (!m_fontRenderer->loadDefaultFont(16.0f)) {
            std::cerr << "Renderer: Warning - Failed to load default font" << std::endl;
            // Continue anyway - text rendering will just be disabled
        }

        m_initialized = true;
        return true;
    }

    void Renderer::shutdown() {
        m_initialized = false;
    }

    void Renderer::beginFrame() {
        if (!m_initialized) return;
        
        // Clear matrix stack
        while (!m_matrixStack.empty()) {
            m_matrixStack.pop();
        }
        
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
    }

    void Renderer::endFrame() {
        if (!m_initialized) return;

        // Note: Buffer swapping is now handled by GLFW in the main loop
        checkErrors();
    }

    void Renderer::clear() {
        GLState::clear();
    }

    void Renderer::setViewport(int x, int y, int width, int height) {
        m_viewportX = x;
        m_viewportY = y;
        m_viewportWidth = width;
        m_viewportHeight = height;
        GLState::setViewport(x, y, width, height);
    }

    void Renderer::getViewport(int& x, int& y, int& width, int& height) const {
        x = m_viewportX;
        y = m_viewportY;
        width = m_viewportWidth;
        height = m_viewportHeight;
    }

    void Renderer::setCamera(Camera& camera) {
        if (DEBUG_RENDERER_LOGGING) {
            Vec3 pos = camera.getPosition();
            std::cout << "[RENDERER] setCamera: position=(" << pos.x << ", " << pos.y << ", " << pos.z << ")" << std::endl;
        }
        setupProjection(camera);
        setupView(camera);
    }

    void Renderer::setupProjection(Camera& camera) {
        if (DEBUG_RENDERER_LOGGING) {
            std::cout << "[RENDERER] setupProjection: Loading projection matrix" << std::endl;
        }
        glMatrixMode(GL_PROJECTION);
        GLMatrix::loadMatrix(camera.getProjectionMatrix());
    }

    void Renderer::setupView(Camera& camera) {
        if (DEBUG_RENDERER_LOGGING) {
            std::cout << "[RENDERER] setupView: Loading view matrix" << std::endl;
        }
        glMatrixMode(GL_MODELVIEW);
        GLMatrix::loadMatrix(camera.getViewMatrix());
        if (DEBUG_RENDERER_LOGGING) {
            std::cout << "[RENDERER] setupView: View matrix loaded" << std::endl;
        }
    }

    void Renderer::pushMatrix() {
        glPushMatrix();
        
        // Also maintain our own stack for queries
        GLfloat matrix[16];
        glGetFloatv(GL_MODELVIEW_MATRIX, matrix);
        Mat4 currentMatrix;
        for (int i = 0; i < 16; i++) {
            currentMatrix.m[i] = matrix[i];
        }
        m_matrixStack.push(currentMatrix);
    }

    void Renderer::popMatrix() {
        glPopMatrix();
        
        if (!m_matrixStack.empty()) {
            m_matrixStack.pop();
        }
    }

    void Renderer::loadMatrix(const Mat4& matrix) {
        GLMatrix::loadMatrix(matrix);
    }

    void Renderer::multMatrix(const Mat4& matrix) {
        GLMatrix::multMatrix(matrix);
    }

    void Renderer::loadIdentity() {
        glLoadIdentity();
    }

    void Renderer::setColor(const Color& color) {
        m_currentColor = color;
        glColor4f(color.r, color.g, color.b, color.a);
    }

    void Renderer::setWireframe(bool wireframe) {
        m_wireframeMode = wireframe;
        if (wireframe) {
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        } else {
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }
    }

    void Renderer::setPointSize(float size) {
        m_pointSize = size;
        GLState::setPointSize(size);
    }

    void Renderer::setLineWidth(float width) {
        m_lineWidth = width;
        GLState::setLineWidth(width);
    }

    void Renderer::enableLighting(bool enable) {
        m_lightingEnabled = enable;
        if (enable) {
            glEnable(GL_LIGHTING);
            glEnable(GL_LIGHT0);
            
            // Set ambient light
            GLfloat ambient[] = { m_ambientLight.r, m_ambientLight.g, m_ambientLight.b, m_ambientLight.a };
            glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);
            
            // Set directional light
            GLfloat lightPos[] = { -m_lightDirection.x, -m_lightDirection.y, -m_lightDirection.z, 0.0f };
            GLfloat lightColor[] = { m_lightColor.r, m_lightColor.g, m_lightColor.b, m_lightColor.a };
            glLightfv(GL_LIGHT0, GL_POSITION, lightPos);
            glLightfv(GL_LIGHT0, GL_DIFFUSE, lightColor);
            glLightfv(GL_LIGHT0, GL_SPECULAR, lightColor);
        } else {
            glDisable(GL_LIGHTING);
        }
    }

    void Renderer::setAmbientLight(const Color& color) {
        m_ambientLight = color;
        if (m_lightingEnabled) {
            GLfloat ambient[] = { color.r, color.g, color.b, color.a };
            glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);
        }
    }

    void Renderer::setDirectionalLight(const Vec3& direction, const Color& color) {
        m_lightDirection = direction.normalized();
        m_lightColor = color;
        if (m_lightingEnabled) {
            GLfloat lightPos[] = { -m_lightDirection.x, -m_lightDirection.y, -m_lightDirection.z, 0.0f };
            GLfloat lightColor[] = { m_lightColor.r, m_lightColor.g, m_lightColor.b, m_lightColor.a };
            glLightfv(GL_LIGHT0, GL_POSITION, lightPos);
            glLightfv(GL_LIGHT0, GL_DIFFUSE, lightColor);
            glLightfv(GL_LIGHT0, GL_SPECULAR, lightColor);
        }
    }

    void Renderer::setRenderMode(RenderMode mode) {
        m_renderMode = mode;
        applyRenderMode();
    }

    void Renderer::drawPoint(const Vec3& position) {
        GLDraw::drawPoint(position, m_pointSize);
    }

    void Renderer::drawPoint(const Vec3& position, const Color& color, float size) {
        Color oldColor = m_currentColor;
        float oldSize = m_pointSize;
        setColor(color);
        setPointSize(size);
        GLDraw::drawPoint(position, size);
        setColor(oldColor);
        setPointSize(oldSize);
    }

    void Renderer::drawLine(const Vec3& start, const Vec3& end) {
        GLDraw::drawLine(start, end);
    }

    void Renderer::drawLine(const Vec3& start, const Vec3& end, const Color& color, float width) {
        Color oldColor = m_currentColor;
        float oldWidth = m_lineWidth;
        setColor(color);
        setLineWidth(width);
        GLDraw::drawLine(start, end);
        setColor(oldColor);
        setLineWidth(oldWidth);
    }

    void Renderer::drawTriangle(const Vec3& v1, const Vec3& v2, const Vec3& v3) {
        glBegin(GL_TRIANGLES);
        glVertex3f(v1.x, v1.y, v1.z);
        glVertex3f(v2.x, v2.y, v2.z);
        glVertex3f(v3.x, v3.y, v3.z);
        glEnd();
    }

    void Renderer::drawTriangle(const Vec3& v1, const Vec3& v2, const Vec3& v3, const Color& color) {
        Color oldColor = m_currentColor;
        setColor(color);
        glBegin(GL_TRIANGLES);
        glVertex3f(v1.x, v1.y, v1.z);
        glVertex3f(v2.x, v2.y, v2.z);
        glVertex3f(v3.x, v3.y, v3.z);
        glEnd();
        setColor(oldColor);
    }

    void Renderer::drawQuad(const Vec3& v1, const Vec3& v2, const Vec3& v3, const Vec3& v4) {
        glBegin(GL_QUADS);
        glVertex3f(v1.x, v1.y, v1.z);
        glVertex3f(v2.x, v2.y, v2.z);
        glVertex3f(v3.x, v3.y, v3.z);
        glVertex3f(v4.x, v4.y, v4.z);
        glEnd();
    }

    void Renderer::drawQuad(const Vec3& v1, const Vec3& v2, const Vec3& v3, const Vec3& v4, const Color& color) {
        Color oldColor = m_currentColor;
        setColor(color);
        glBegin(GL_QUADS);
        glVertex3f(v1.x, v1.y, v1.z);
        glVertex3f(v2.x, v2.y, v2.z);
        glVertex3f(v3.x, v3.y, v3.z);
        glVertex3f(v4.x, v4.y, v4.z);
        glEnd();
        setColor(oldColor);
    }

    void Renderer::drawCube(float size) {
        GLDraw::drawWireCube(size);
    }

    void Renderer::drawCube(float size, const Color& color) {
        Color oldColor = m_currentColor;
        setColor(color);
        GLDraw::drawWireCube(size);
        setColor(oldColor);
    }

    void Renderer::drawSphere(float radius, int segments) {
        // TODO: Implement custom sphere rendering without GLUT
        // For now, draw a simple wireframe cube as placeholder
        drawCube(radius * 2.0f);
    }

    void Renderer::drawSphere(float radius, int segments, const Color& color) {
        Color oldColor = m_currentColor;
        setColor(color);
        // TODO: Implement custom sphere rendering without GLUT
        // For now, draw a simple wireframe cube as placeholder
        GLDraw::drawWireCube(radius * 2.0f);
        setColor(oldColor);
    }

    void Renderer::drawCylinder(float radius, float height, int segments) {
        // Simple cylinder implementation using GLUT
        GLUquadric* quad = gluNewQuadric();
        if (quad) {
            glPushMatrix();
            glTranslatef(0, -height * 0.5f, 0);
            gluCylinder(quad, radius, radius, height, segments, 1);
            gluDeleteQuadric(quad);
            glPopMatrix();
        }
    }

    void Renderer::drawCylinder(float radius, float height, int segments, const Color& color) {
        Color oldColor = m_currentColor;
        setColor(color);
        // Simple cylinder implementation using GLUT
        GLUquadric* quad = gluNewQuadric();
        if (quad) {
            glPushMatrix();
            glTranslatef(0, -height * 0.5f, 0);
            gluCylinder(quad, radius, radius, height, segments, 1);
            gluDeleteQuadric(quad);
            glPopMatrix();
        }
        setColor(oldColor);
    }

    void Renderer::drawGrid(float size, int divisions, const Color& color) {
        Color oldColor = m_currentColor;
        setColor(color);
        GLDraw::drawGrid(size, divisions, color);
        setColor(oldColor);
    }

    void Renderer::drawAxes(float length) {
        GLDraw::drawAxes(length);
    }

    void Renderer::drawAxes(float length, const Color& color) {
        Color oldColor = m_currentColor;
        setColor(color);
        GLDraw::drawAxes(length);
        setColor(oldColor);
    }

    void Renderer::setupOpenGL() {
        // Enable depth testing
        GLState::enableDepthTest();
        
        // Enable blending for transparency
        GLState::enableBlending();
        
        // Enable multisampling for anti-aliasing
        GLState::enableMultisampling();
        
        // Set default clear color
        GLState::setClearColor(0.2f, 0.2f, 0.2f, 1.0f);
        
        // Enable smooth points and lines
        glEnable(GL_POINT_SMOOTH);
        glEnable(GL_LINE_SMOOTH);
        glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);
        glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
        
        // Set default material properties
        GLfloat mat_ambient[] = { 0.2f, 0.2f, 0.2f, 1.0f };
        GLfloat mat_diffuse[] = { 0.8f, 0.8f, 0.8f, 1.0f };
        GLfloat mat_specular[] = { 0.5f, 0.5f, 0.5f, 1.0f };
        GLfloat mat_shininess[] = { 50.0f };
        
        glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, mat_ambient);
        glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mat_diffuse);
        glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, mat_specular);
        glMaterialfv(GL_FRONT_AND_BACK, GL_SHININESS, mat_shininess);
        
        // Enable color material
        glEnable(GL_COLOR_MATERIAL);
        glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

        // Enable V-sync 1 -> Disable 0
        glfwSwapInterval(1);
    }

    void Renderer::applyRenderMode() {
        switch (m_renderMode) {
            case RenderMode::Points:
                glPolygonMode(GL_FRONT_AND_BACK, GL_POINT);
                break;
            case RenderMode::Lines:
            case RenderMode::Wireframe:
                glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                break;
            case RenderMode::Triangles:
                glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
                break;
        }
    }

    void Renderer::checkErrors() const {
        checkGLError("Renderer");
    }

    void Renderer::drawText(const std::string& text, const Vec3& position, float size) {
        if (!m_initialized || !m_fontRenderer || !m_fontRenderer->isInitialized()) {
            return;
        }

        m_fontRenderer->drawText(text, position, size, m_currentColor);
    }



    void Renderer::drawString(const std::string& text, float x, float y) {
        if (!m_initialized || !m_fontRenderer || !m_fontRenderer->isInitialized()) {
            return;
        }

        m_fontRenderer->drawString(text, x, y, m_currentColor);
    }

    void Renderer::draw2dPoint(const Vec2& position) {
        if (!m_initialized) return;

        // Save current matrices
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();

        // Set up orthographic projection for 2D drawing
        glOrtho(0, m_viewportWidth, m_viewportHeight, 0, -1, 1);

        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();

        // Draw the point
        glPointSize(m_pointSize);
        glBegin(GL_POINTS);
        glVertex2f(position.x, position.y);
        glEnd();

        // Restore matrices
        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
    }

    void Renderer::draw2dPoint(const Vec2& position, const Color& color, float size) {
        if (!m_initialized) return;

        // Save current state
        Color oldColor = m_currentColor;
        float oldSize = m_pointSize;

        // Set new state
        setColor(color);
        setPointSize(size);

        // Draw the point
        draw2dPoint(position);

        // Restore state
        setColor(oldColor);
        setPointSize(oldSize);
    }

    void Renderer::draw2dLine(const Vec2& start, const Vec2& end) {
        if (!m_initialized) return;

        // Save current matrices
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();

        // Set up orthographic projection for 2D drawing
        glOrtho(0, m_viewportWidth, m_viewportHeight, 0, -1, 1);

        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();

        // Draw the line
        glLineWidth(m_lineWidth);
        glBegin(GL_LINES);
        glVertex2f(start.x, start.y);
        glVertex2f(end.x, end.y);
        glEnd();

        // Restore matrices
        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
    }

    void Renderer::draw2dLine(const Vec2& start, const Vec2& end, const Color& color, float width) {
        if (!m_initialized) return;

        // Save current state
        Color oldColor = m_currentColor;
        float oldWidth = m_lineWidth;

        // Set new state
        setColor(color);
        setLineWidth(width);

        // Draw the line
        draw2dLine(start,end);

        // Restore state
        setColor(oldColor);
        setLineWidth(oldWidth);
    }

    void Renderer::drawMesh(const Vec3* vertices, const Vec3* normals, const Color* colors, int vertexCount, const int* indices, int indexCount, bool enableLighting) {
        if (!vertices || vertexCount == 0) return;

        // Save current lighting state
        bool wasLightingEnabled = glIsEnabled(GL_LIGHTING);

        if (enableLighting && !wasLightingEnabled) {
            glEnable(GL_LIGHTING);
        } else if (!enableLighting && wasLightingEnabled) {
            glDisable(GL_LIGHTING);
        }

        glBegin(GL_TRIANGLES);

        if (indices && indexCount > 0) {
            // Use indices - render triangles
            for (int i = 0; i < indexCount; i += 3) {
                for (int j = 0; j < 3; j++) {
                    int idx = indices[i + j];
                    if (idx >= 0 && idx < vertexCount) {
                        if (normals) {
                            glNormal3f(normals[idx].x, normals[idx].y, normals[idx].z);
                        }
                        if (colors) {
                            glColor4f(colors[idx].r, colors[idx].g, colors[idx].b, colors[idx].a);
                        }
                        glVertex3f(vertices[idx].x, vertices[idx].y, vertices[idx].z);
                    }
                }
            }
        } else {
            // Use vertices directly - assume triangles
            for (int i = 0; i < vertexCount; i++) {
                if (normals) {
                    glNormal3f(normals[i].x, normals[i].y, normals[i].z);
                }
                if (colors) {
                    glColor4f(colors[i].r, colors[i].g, colors[i].b, colors[i].a);
                }
                glVertex3f(vertices[i].x, vertices[i].y, vertices[i].z);
            }
        }

        glEnd();

        // Restore lighting state
        if (enableLighting && !wasLightingEnabled) {
            glDisable(GL_LIGHTING);
        } else if (!enableLighting && wasLightingEnabled) {
            glEnable(GL_LIGHTING);
        }

        // Restore current color
        glColor4f(m_currentColor.r, m_currentColor.g, m_currentColor.b, m_currentColor.a);
    }

    void Renderer::drawMeshWireframe(const Vec3* vertices, const Color* colors, int vertexCount, const int* indices, int indexCount) {
        if (!vertices || vertexCount == 0) return;

        // Save current polygon mode
        GLint polygonMode[2];
        glGetIntegerv(GL_POLYGON_MODE, polygonMode);

        // Set wireframe mode
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

        glBegin(GL_TRIANGLES);

        if (indices && indexCount > 0) {
            // Use indices - render triangles
            for (int i = 0; i < indexCount; i += 3) {
                for (int j = 0; j < 3; j++) {
                    int idx = indices[i + j];
                    if (idx >= 0 && idx < vertexCount) {
                        if (colors) {
                            glColor4f(colors[idx].r, colors[idx].g, colors[idx].b, colors[idx].a);
                        }
                        glVertex3f(vertices[idx].x, vertices[idx].y, vertices[idx].z);
                    }
                }
            }
        } else {
            // Use vertices directly - assume triangles
            for (int i = 0; i < vertexCount; i++) {
                if (colors) {
                    glColor4f(colors[i].r, colors[i].g, colors[i].b, colors[i].a);
                }
                glVertex3f(vertices[i].x, vertices[i].y, vertices[i].z);
            }
        }

        glEnd();

        // Restore polygon mode
        glPolygonMode(GL_FRONT_AND_BACK, polygonMode[0]);

        // Restore current color
        glColor4f(m_currentColor.r, m_currentColor.g, m_currentColor.b, m_currentColor.a);
    }

    void Renderer::drawMeshEdges(const Vec3* vertices, const int* edgeIndices, const Color* edgeColors, int edgeCount) {
        if (!vertices || !edgeIndices || edgeCount == 0) return;

        glBegin(GL_LINES);

        for (int i = 0; i < edgeCount; i++) {
            int vertexA = edgeIndices[i * 2];
            int vertexB = edgeIndices[i * 2 + 1];

            // Set edge color if provided
            if (edgeColors) {
                glColor4f(edgeColors[i].r, edgeColors[i].g, edgeColors[i].b, edgeColors[i].a);
            }

            // Draw line between the two vertices
            glVertex3f(vertices[vertexA].x, vertices[vertexA].y, vertices[vertexA].z);
            glVertex3f(vertices[vertexB].x, vertices[vertexB].y, vertices[vertexB].z);
        }

        glEnd();

        // Restore current color
        glColor4f(m_currentColor.r, m_currentColor.g, m_currentColor.b, m_currentColor.a);
    }

    void Renderer::drawPoints(const Vec3* points, int count) {
        if (!points || count == 0) return;

        glBegin(GL_POINTS);

        for (int i = 0; i < count; i++) {
            glVertex3f(points[i].x, points[i].y, points[i].z);
        }

        glEnd();
    }

    void Renderer::drawLines(const Vec3* points, int count) {
        if (!points || count == 0) return;

        glBegin(GL_LINES);

        for (int i = 0; i < count; i++) {
            glVertex3f(points[i].x, points[i].y, points[i].z);
        }

        glEnd();
    }

} // namespace alice2

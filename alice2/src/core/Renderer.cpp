#include "Renderer.h"
#include "Camera.h"
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
        , m_currentColor(1.0f, 1.0f, 1.0f)
        , m_currentAlpha(1.0f)
        , m_wireframeMode(false)
        , m_pointSize(1.0f)
        , m_lineWidth(1.0f)
        , m_renderMode(RenderMode::Triangles)
        , m_lightingEnabled(true)
        , m_ambientLight(0.2f, 0.2f, 0.2f)
        , m_lightDirection(0.0f, -1.0f, -1.0f)
        , m_lightColor(1.0f, 1.0f, 1.0f)
    {
    }

    Renderer::~Renderer() {
        shutdown();
    }

    bool Renderer::initialize() {
        if (m_initialized) return true;

        // Initialize OpenGL state
        setupOpenGL();
        
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

    void Renderer::setColor(const Vec3& color, float alpha) {
        m_currentColor = color;
        m_currentAlpha = alpha;
        glColor4f(color.x, color.y, color.z, alpha);
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
            GLfloat ambient[] = { m_ambientLight.x, m_ambientLight.y, m_ambientLight.z, 1.0f };
            glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);
            
            // Set directional light
            GLfloat lightPos[] = { -m_lightDirection.x, -m_lightDirection.y, -m_lightDirection.z, 0.0f };
            GLfloat lightColor[] = { m_lightColor.x, m_lightColor.y, m_lightColor.z, 1.0f };
            glLightfv(GL_LIGHT0, GL_POSITION, lightPos);
            glLightfv(GL_LIGHT0, GL_DIFFUSE, lightColor);
            glLightfv(GL_LIGHT0, GL_SPECULAR, lightColor);
        } else {
            glDisable(GL_LIGHTING);
        }
    }

    void Renderer::setAmbientLight(const Vec3& color) {
        m_ambientLight = color;
        if (m_lightingEnabled) {
            GLfloat ambient[] = { color.x, color.y, color.z, 1.0f };
            glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);
        }
    }

    void Renderer::setDirectionalLight(const Vec3& direction, const Vec3& color) {
        m_lightDirection = direction.normalized();
        m_lightColor = color;
        if (m_lightingEnabled) {
            GLfloat lightPos[] = { -m_lightDirection.x, -m_lightDirection.y, -m_lightDirection.z, 0.0f };
            GLfloat lightColor[] = { m_lightColor.x, m_lightColor.y, m_lightColor.z, 1.0f };
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

    void Renderer::drawPoint(const Vec3& position, const Vec3& color, float size) {
        Vec3 oldColor = m_currentColor;
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

    void Renderer::drawLine(const Vec3& start, const Vec3& end, const Vec3& color, float width) {
        Vec3 oldColor = m_currentColor;
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

    void Renderer::drawTriangle(const Vec3& v1, const Vec3& v2, const Vec3& v3, const Vec3& color) {
        Vec3 oldColor = m_currentColor;
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

    void Renderer::drawQuad(const Vec3& v1, const Vec3& v2, const Vec3& v3, const Vec3& v4, const Vec3& color) {
        Vec3 oldColor = m_currentColor;
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

    void Renderer::drawCube(float size, const Vec3& color) {
        Vec3 oldColor = m_currentColor;
        setColor(color);
        GLDraw::drawWireCube(size);
        setColor(oldColor);
    }

    void Renderer::drawSphere(float radius, int segments) {
        // TODO: Implement custom sphere rendering without GLUT
        // For now, draw a simple wireframe cube as placeholder
        drawCube(radius * 2.0f);
    }

    void Renderer::drawSphere(float radius, int segments, const Vec3& color) {
        Vec3 oldColor = m_currentColor;
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

    void Renderer::drawCylinder(float radius, float height, int segments, const Vec3& color) {
        Vec3 oldColor = m_currentColor;
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

    void Renderer::drawGrid(float size, int divisions, const Vec3& color) {
        Vec3 oldColor = m_currentColor;
        setColor(color);
        GLDraw::drawGrid(size, divisions, color);
        setColor(oldColor);
    }

    void Renderer::drawAxes(float length) {
        GLDraw::drawAxes(length);
    }

    void Renderer::drawAxes(float length, const Vec3& color) {
        Vec3 oldColor = m_currentColor;
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

} // namespace alice2

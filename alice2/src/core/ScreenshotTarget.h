#pragma once

#include "Camera.h"
#include "Renderer.h"
#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace alice2 {

// Optional native render resolution; UI coordinates remain one framebuffer pixel.
inline std::pair<int, int> screenshotSizeFromEnvironment() {
    std::string value;
#ifdef _WIN32
    char* buffer = nullptr;
    size_t length = 0;
    if (_dupenv_s(&buffer, &length, "ALICE2_SCREENSHOT_SIZE") != 0)
        throw std::runtime_error("Cannot read ALICE2_SCREENSHOT_SIZE.");
    if (buffer) value = buffer;
    std::free(buffer);
#else
    if (const char* buffer = std::getenv("ALICE2_SCREENSHOT_SIZE")) value = buffer;
#endif
    if (value.empty()) return {0, 0};
    int width = 0, height = 0;
    char separator = 0;
    std::istringstream input(value);
    if (!(input >> width >> separator >> height) || separator != 'x' ||
        width <= 0 || height <= 0 || (input >> std::ws, !input.eof()))
        throw std::runtime_error("ALICE2_SCREENSHOT_SIZE must be WIDTHxHEIGHT, e.g. 3840x2160.");
    return {width, height};
}

// Preserve the interactive render state while capturing above the monitor size.
// A multisampled target retains the window's antialiasing before PNG readback.
class ScreenshotTarget {
public:
    ScreenshotTarget(Renderer& renderer, Camera& camera, int& windowWidth, int& windowHeight)
        : m_renderer(renderer), m_camera(camera), m_windowWidth(windowWidth), m_windowHeight(windowHeight),
          m_originalWidth(windowWidth), m_originalHeight(windowHeight), m_originalAspect(camera.getAspectRatio()) {
        renderer.getViewport(m_viewport[0], m_viewport[1], m_viewport[2], m_viewport[3]);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &m_drawFramebuffer);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &m_readFramebuffer);
        glGetIntegerv(GL_RENDERBUFFER_BINDING, &m_renderbuffer);
        glGetIntegerv(GL_PACK_ALIGNMENT, &m_packAlignment);
    }

    ScreenshotTarget(const ScreenshotTarget&) = delete;
    ScreenshotTarget& operator=(const ScreenshotTarget&) = delete;

    ~ScreenshotTarget() {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_drawFramebuffer);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_readFramebuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, m_renderbuffer);
        glPixelStorei(GL_PACK_ALIGNMENT, m_packAlignment);
        glDeleteFramebuffers(2, m_framebuffers);
        glDeleteRenderbuffers(3, m_buffers);
        m_windowWidth = m_originalWidth;
        m_windowHeight = m_originalHeight;
        m_camera.setAspectRatio(m_originalAspect);
        m_renderer.setViewport(m_viewport[0], m_viewport[1], m_viewport[2], m_viewport[3]);
    }

    bool initialize(int width, int height, int samples) {
        GLint limit = 0, maxSamples = 0;
        glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &limit);
        glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
        if (width > limit || height > limit) return false;
        samples = (std::min)(samples, maxSamples);
        glGenFramebuffers(2, m_framebuffers);
        glGenRenderbuffers(3, m_buffers);
        glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffers[0]);
        for (int i = 0; i < 2; ++i) {
            glBindRenderbuffer(GL_RENDERBUFFER, m_buffers[i]);
            const GLenum format = i == 0 ? GL_RGB8 : GL_DEPTH24_STENCIL8;
            if (samples > 1) glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, format, width, height);
            else glRenderbufferStorage(GL_RENDERBUFFER, format, width, height);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, i == 0 ? GL_COLOR_ATTACHMENT0 : GL_DEPTH_STENCIL_ATTACHMENT,
                                      GL_RENDERBUFFER, m_buffers[i]);
        }
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;

        glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffers[1]);
        glBindRenderbuffer(GL_RENDERBUFFER, m_buffers[2]);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGB8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_buffers[2]);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;
        m_windowWidth = width;
        m_windowHeight = height;
        m_camera.setAspectRatio(static_cast<float>(width) / height);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        return true;
    }

    void bindForRendering() { glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffers[0]); }

    void resolveForReading() {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_framebuffers[0]);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_framebuffers[1]);
        glBlitFramebuffer(0, 0, m_windowWidth, m_windowHeight, 0, 0, m_windowWidth, m_windowHeight,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_framebuffers[1]);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
    }

private:
    Renderer& m_renderer;
    Camera& m_camera;
    int& m_windowWidth;
    int& m_windowHeight;
    int m_originalWidth, m_originalHeight;
    float m_originalAspect;
    int m_viewport[4]{};
    GLint m_drawFramebuffer = 0, m_readFramebuffer = 0, m_renderbuffer = 0, m_packAlignment = 4;
    GLuint m_framebuffers[2]{};
    GLuint m_buffers[3]{};
};

} // namespace alice2

#include "Application.h"
#include <GLFW/glfw3.h>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <vector>
#include <thread>
#include <cstdio>
#include <cstring>

// STB image write for screenshots
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../depends/stb/stb_image_write.h"
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4005)
#endif
#include <gl2ps.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

// Debug logging flag - set to true to enable detailed application logging
#define DEBUG_APPLICATION_LOGGING false
#define DEBUG_MOUSE_BUTTON_LOGGING false

namespace alice2 {

    Application* Application::s_instance = nullptr;

    Application::Application()
        : m_running(false)
        , m_initialized(false)
        , m_window(nullptr)
        , m_windowTitle("zSpace Planariser - Alice2 3D Viewer")
        , m_windowWidth(1360)
        , m_windowHeight(880)
        , m_fullscreen(false)
        , m_vsync(true)
        , m_multisampleSamples(4)
        , m_deltaTime(0.0f)
        , m_totalTime(0.0f)
        , m_lastFrameTime(0.0f)
        , m_frameCount(0)
        , m_fps(0.0f)
        , m_fpsUpdateTime(0.0f)
        , m_fpsFrameCount(0)
    {
        s_instance = this;
        
        // Create core components
        m_scene = std::make_unique<Scene>();
        m_renderer = std::make_unique<Renderer>();
        m_camera = std::make_unique<Camera>();
        m_inputManager = std::make_unique<InputManager>();
        m_cameraController = std::make_unique<CameraController>(*m_camera, *m_inputManager);
        m_sketchManager = std::make_unique<SketchManager>();
    }

    Application::~Application() {
        shutdown();
        s_instance = nullptr;
    }

    bool Application::initialize(int argc, char** argv) {
        if (m_initialized) return true;

        std::cout << "Initializing alice2..." << std::endl;

        // Initialize window and OpenGL
        if (!initializeWindow(argc, argv)) {
            std::cerr << "Failed to initialize window" << std::endl;
            return false;
        }

        if (!initializeOpenGL()) {
            std::cerr << "Failed to initialize OpenGL" << std::endl;
            return false;
        }

        // Initialize renderer
        if (!m_renderer->initialize()) {
            std::cerr << "Failed to initialize renderer" << std::endl;
            return false;
        }

        // Setup camera
        m_camera->setPerspective(45.0f, (float)m_windowWidth / m_windowHeight, 0.1f, 1000.0f);
        // Camera initialization is handled by the Camera constructor with proper Z-up quaternion setup

        // Initialize sketch manager
        m_sketchManager->initialize(m_scene.get(), m_renderer.get(), m_camera.get(), m_inputManager.get());
        m_sketchManager->scanUserSrcDirectory();

        // Setup callbacks
        setupCallbacks();

        m_initialized = true;
        std::cout << "\nalice2 initialized successfully" << std::endl;
        return true;
    }

    void Application::run() {
        if (!m_initialized) {
            std::cerr << "Application not initialized" << std::endl;
            return;
        }

        m_running = true;
        std::cout << "Starting alice2 main loop..." << std::endl;

        // Initialize timing
        auto startTime = std::chrono::high_resolution_clock::now();
        m_lastFrameTime = 0.0f;

        // Main loop
        while (!glfwWindowShouldClose(m_window) && m_running) {
            // Poll for and process events
            glfwPollEvents();

            // Update and render
            update();
            render();

            // Swap front and back buffers
            glfwSwapBuffers(m_window);
        }
    }

    void Application::shutdown() {
        if (!m_initialized) return;

        std::cout << "Shutting down alice2..." << std::endl;

        m_running = false;

        if (m_sketchManager) {
            m_sketchManager->unloadCurrentSketch();
        }

        if (m_renderer) {
            m_renderer->shutdown();
        }

        // Clean up GLFW
        if (m_window) {
            glfwDestroyWindow(m_window);
            m_window = nullptr;
        }
        glfwTerminate();

        m_initialized = false;
    }

    bool Application::initializeWindow(int argc, char** argv) {
        // Initialize GLFW
        glfwSetErrorCallback(errorCallback);

        if (!glfwInit()) {
            std::cerr << "Failed to initialize GLFW" << std::endl;
            return false;
        }

        // Configure GLFW
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
        glfwWindowHint(GLFW_SAMPLES, m_multisampleSamples);
        glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);

        // Create window
        m_window = glfwCreateWindow(m_windowWidth, m_windowHeight, m_windowTitle.c_str(), nullptr, nullptr);
        if (!m_window) {
            std::cerr << "Failed to create GLFW window" << std::endl;
            glfwTerminate();
            return false;
        }

        // Make the window's context current
        glfwMakeContextCurrent(m_window);

        // Enable vsync
        glfwSwapInterval(m_vsync ? 1 : 0);

        return true;
    }

    bool Application::initializeOpenGL() {
        GLenum glewResult = glewInit();
        if (glewResult != GLEW_OK) {
            std::cerr << "GLEW initialization failed: " << glewGetErrorString(glewResult) << std::endl;
            return false;
        }

        std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;
        std::cout << "GLSL Version: " << glGetString(GL_SHADING_LANGUAGE_VERSION) << std::endl;

        return true;
    }

    void Application::setupCallbacks() {
        // Set GLFW callbacks
        glfwSetFramebufferSizeCallback(m_window, framebufferSizeCallback);
        glfwSetKeyCallback(m_window, keyCallback);
        glfwSetMouseButtonCallback(m_window, mouseButtonCallback);
        glfwSetCursorPosCallback(m_window, cursorPosCallback);
        glfwSetScrollCallback(m_window, scrollCallback);
        glfwSetDropCallback(m_window, dropCallback);
    }

    void Application::update() {
        if (DEBUG_APPLICATION_LOGGING) {
            std::cout << "[APP] ===== Frame " << m_frameCount << " Update Start =====" << std::endl;
        }

        updateTiming();

        if (DEBUG_APPLICATION_LOGGING) {
            std::cout << "[APP] Delta time: " << m_deltaTime << "s" << std::endl;
            std::cout << "[APP] Updating CameraController..." << std::endl;
        }

        // Update camera controller BEFORE resetting input states
        m_cameraController->update(m_deltaTime);

        if (DEBUG_APPLICATION_LOGGING) {
            std::cout << "[APP] Updating InputManager (will reset deltas)..." << std::endl;
        }

        // Update input manager (this resets mouse delta and wheel delta)
        m_inputManager->update();

        if (DEBUG_APPLICATION_LOGGING) {
            std::cout << "[APP] Updating Scene..." << std::endl;
        }

        m_scene->update(m_deltaTime);

        if (m_sketchManager->hasCurrentSketch()) {
            if (DEBUG_APPLICATION_LOGGING) {
                std::cout << "[APP] Updating current sketch..." << std::endl;
            }
            m_sketchManager->updateCurrentSketch(m_deltaTime);
        }

        updateFPS();

        if (DEBUG_APPLICATION_LOGGING) {
            std::cout << "[APP] ===== Frame " << m_frameCount << " Update End =====" << std::endl;
        }
    }

    void Application::render() {
        if (DEBUG_APPLICATION_LOGGING) {
            std::cout << "[APP] ===== Frame " << m_frameCount << " Render Start =====" << std::endl;
        }

        m_renderer->beginFrame();
        m_renderer->setViewport(0, 0, m_windowWidth, m_windowHeight);

        if (DEBUG_APPLICATION_LOGGING) {
            std::cout << "[APP] Setting camera on renderer..." << std::endl;
        }
        m_renderer->setCamera(*m_camera);

        // Set background color
        Color bgColor = m_scene->getBackgroundColor();
        glClearColor(bgColor.r, bgColor.g, bgColor.b, bgColor.a);
        m_renderer->clear();

        if (DEBUG_APPLICATION_LOGGING) {
            std::cout << "[APP] Rendering scene..." << std::endl;
        }

        // Render scene
        m_scene->render(*m_renderer, *m_camera);

        // Render current sketch
        if (m_sketchManager->hasCurrentSketch()) {
            if (DEBUG_APPLICATION_LOGGING) {
                std::cout << "[APP] Rendering current sketch..." << std::endl;
            }
            m_sketchManager->drawCurrentSketch(*m_renderer, *m_camera);
        }

        m_renderer->endFrame();

        if (DEBUG_APPLICATION_LOGGING) {
            std::cout << "[APP] ===== Frame " << m_frameCount << " Render End =====" << std::endl;
        }
    }

    void Application::updateTiming() {
        auto currentTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(currentTime.time_since_epoch());
        float currentTimeSeconds = duration.count() / 1000000.0f;
        
        m_deltaTime = currentTimeSeconds - m_lastFrameTime;
        m_lastFrameTime = currentTimeSeconds;
        m_totalTime += m_deltaTime;
        m_frameCount++;
    }

    void Application::updateFPS() {
        m_fpsFrameCount++;
        m_fpsUpdateTime += m_deltaTime;
        
        if (m_fpsUpdateTime >= 1.0f) {
            m_fps = m_fpsFrameCount / m_fpsUpdateTime;
            m_fpsFrameCount = 0;
            m_fpsUpdateTime = 0.0f;
        }
    }

    // Static callback implementations
    void Application::errorCallback(int error, const char* description) {
        std::cerr << "GLFW Error " << error << ": " << description << std::endl;
    }

    void Application::framebufferSizeCallback(GLFWwindow* window, int width, int height) {
        if (s_instance) {
            s_instance->m_windowWidth = width;
            s_instance->m_windowHeight = height;
            s_instance->m_camera->setAspectRatio((float)width / height);
            glViewport(0, 0, width, height);
        }
    }

    void Application::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
        if (s_instance && action == GLFW_PRESS) {
            // Handle function keys for camera save/load
            if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F8) {
                int slot = key - GLFW_KEY_F1;  // Convert F1-F8 to 0-7

                if (mods & GLFW_MOD_SHIFT) {
                    // Shift + F1-F8: Save camera
                    s_instance->m_cameraController->saveCamera(slot);
                } else {
                    // F1-F8: Load camera
                    s_instance->m_cameraController->loadCamera(slot);
                }
                return;
            }

            // Handle screenshot shortcuts
            if (key == GLFW_KEY_F9) {
                if ((mods & GLFW_MOD_CONTROL) && (mods & GLFW_MOD_SHIFT)) {
                    // Ctrl + Shift + F9: Take screenshots from all saved cameras
                    s_instance->takeScreenshotAllCameras();
                } else {
                    // F9: Take screenshot from current view
                    s_instance->takeScreenshot();
                }
                return;
            }

            if (key == GLFW_KEY_F10) {
                if (mods & GLFW_MOD_SHIFT) {
                    // Shift + F10: Take SVG screenshots from all saved cameras
                    s_instance->takeScreenshotAllCamerasSvg();
                } else {
                    // F10: Take SVG screenshot from current view
                    s_instance->takeScreenshotSvg();
                }
                return;
            }

            if (key == GLFW_KEY_F11) {
                s_instance->m_cameraController->resetToDefault();
                return;
            }

            if (key == GLFW_KEY_1) {
                s_instance->m_renderer->setSceneRenderMode(SceneRenderMode::MeshWireframe);
                return;
            }

            if (key == GLFW_KEY_2) {
                s_instance->m_renderer->setSceneRenderMode(SceneRenderMode::MeshWireframeWithVertices);
                return;
            }

            if (key == GLFW_KEY_3) {
                s_instance->m_renderer->setSceneRenderMode(SceneRenderMode::Regular);
                return;
            }

            if (key == GLFW_KEY_4) {
                s_instance->m_renderer->setSceneRenderMode(SceneRenderMode::MeshNormalShaded);
                return;
            }

            if (key == GLFW_KEY_5) {
                s_instance->m_renderer->setSceneRenderMode(SceneRenderMode::MeshTransparent);
                return;
            }

            if (key == GLFW_KEY_6) {
                s_instance->m_renderer->setSceneRenderMode(SceneRenderMode::MeshGray);
                return;
            }

            // Convert GLFW key to character for compatibility
            unsigned char charKey = 0;

            // Handle special keys
            if (key == GLFW_KEY_ESCAPE) {
                s_instance->quit();
                return;
            }

            // Convert printable keys
            if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) {
                charKey = (mods & GLFW_MOD_SHIFT) ? ('A' + (key - GLFW_KEY_A)) : ('a' + (key - GLFW_KEY_A));
            } else if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) {
                charKey = '0' + (key - GLFW_KEY_0);
            }

            if (charKey != 0) {
                // Get cursor position for compatibility
                double xpos, ypos;
                glfwGetCursorPos(window, &xpos, &ypos);

                // Set modifiers in InputManager
                s_instance->m_inputManager->setModifiers(mods);
                s_instance->m_inputManager->processKeyboard(charKey, (int)xpos, (int)ypos);

                // First, let the sketch handle the key
                bool handled = false;
                if (s_instance->m_sketchManager->hasCurrentSketch()) {
                    handled = s_instance->m_sketchManager->forwardKeyPress(charKey, (int)xpos, (int)ypos);
                }

                // If the sketch didn't handle it, process default behaviors
                if (!handled) {
                    switch (charKey) {
                        case 'g':
                        case 'G':
                            // Toggle grid
                            s_instance->m_scene->setShowGrid(!s_instance->m_scene->getShowGrid());
                            break;
                        case 'a':
                        case 'A':
                            // Toggle axes
                            s_instance->m_scene->setShowAxes(!s_instance->m_scene->getShowAxes());
                            break;
                        case 'f':
                        case 'F':
                            // Focus on scene bounds
                            s_instance->m_scene->calculateBounds();
                            s_instance->m_cameraController->focusOnBounds(
                                s_instance->m_scene->getBoundsMin(),
                                s_instance->m_scene->getBoundsMax()
                            );
                            break;
                        case 'n':
                        case 'N':
                            // Switch to next sketch
                            s_instance->m_sketchManager->switchToNextSketch();
                            break;
                        case 'p':
                        case 'P':
                            // Switch to previous sketch
                            s_instance->m_sketchManager->switchToPreviousSketch();
                            break;
                    }
                }
            }
        }
    }

    void Application::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
        if (s_instance) {
            // Get cursor position
            double xpos, ypos;
            glfwGetCursorPos(window, &xpos, &ypos);
            int x = (int)xpos;
            int y = (int)ypos;

            // Convert GLFW button and action to GLFW constants for InputManager
            int glfwButton = button;  // Use GLFW button constants directly
            int glfwState = (action == GLFW_PRESS) ? 0 : 1;  // 0 = pressed, 1 = released for compatibility

            if (DEBUG_MOUSE_BUTTON_LOGGING) {
                std::cout << "[APP] mouseButtonCallback: button=" << button << " action=" << action << " pos=(" << x << "," << y << ")" << std::endl;
                std::cout << "[APP] Processing mouse button: button=" << glfwButton << " state=" << glfwState << std::endl;
            }

            // Set modifiers in InputManager
            s_instance->m_inputManager->setModifiers(mods);
            s_instance->m_inputManager->processMouseButton(glfwButton, glfwState, x, y);

            if (s_instance->m_sketchManager->hasCurrentSketch()) {
                s_instance->m_sketchManager->forwardMousePress(glfwButton, glfwState, x, y);
            }
        }
    }

    void Application::cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
        if (s_instance) {
            int x = (int)xpos;
            int y = (int)ypos;

            s_instance->m_inputManager->processMouseMotion(x, y);
            if (s_instance->m_sketchManager->hasCurrentSketch()) {
                s_instance->m_sketchManager->forwardMouseMove(x, y);
            }
        }
    }

    void Application::scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
        if (s_instance) {
            float wheelDelta = (float)yoffset;
            if (DEBUG_MOUSE_BUTTON_LOGGING) {
                std::cout << "[APP] scrollCallback: yoffset=" << yoffset << " wheelDelta=" << wheelDelta << std::endl;
            }
            s_instance->m_inputManager->processMouseWheel(wheelDelta);
        }
    }

    void Application::dropCallback(GLFWwindow* /*window*/, int count, const char** paths) {
        if (s_instance && count > 0 && paths) {
            std::vector<std::string> dropped;
            dropped.reserve(static_cast<std::size_t>(count));
            for (int i = 0; i < count; ++i) {
                if (paths[i]) dropped.emplace_back(paths[i]);
            }
            if (!dropped.empty() && s_instance->m_sketchManager->hasCurrentSketch()) {
                s_instance->m_sketchManager->forwardFileDrop(dropped);
            }
        }
    }

    std::string Application::getClipboardText() const {
        if (!m_window) return {};
        const char* str = glfwGetClipboardString(m_window);
        return str ? std::string(str) : std::string();
    }

    bool Application::setClipboardText(const std::string& text) {
        if (!m_window) return false;
        glfwSetClipboardString(m_window, text.c_str());
        return true;
    }

    std::string Application::getClipboardFilePath() const {
#ifdef _WIN32
        if (!IsClipboardFormatAvailable(CF_HDROP)) return {};
        if (!OpenClipboard(nullptr)) return {};
        std::string result;
        HANDLE hData = GetClipboardData(CF_HDROP);
        if (hData) {
            HDROP hDrop = static_cast<HDROP>(hData);
            const UINT len = DragQueryFileA(hDrop, 0, nullptr, 0);
            if (len > 0) {
                std::string buf(len + 1, '\0');
                if (DragQueryFileA(hDrop, 0, buf.data(), len + 1) > 0) {
                    buf.resize(len);
                    result = std::move(buf);
                }
            }
        }
        CloseClipboard();
        return result;
#else
        return {};
#endif
    }

    // Global entry point
    int run(int argc, char** argv) {
        Application app;
        
        if (!app.initialize(argc, argv)) {
            return -1;
        }
        
        // Try to load the first available sketch
        const auto& sketches = app.getSketchManager().getAvailableSketches();
        if (!sketches.empty()) {
            app.getSketchManager().loadSketch(sketches[0].name);
        }
        
        app.run();
        return 0;
    }

    void Application::takeScreenshot() {
        if (!m_initialized || !m_window) {
            std::cerr << "[SCREENSHOT] Application not initialized" << std::endl;
            return;
        }

        // Create screenshots directory if it doesn't exist
        #ifdef _WIN32
            system("mkdir \"src\\screenshots\" 2>nul");
        #else
            system("mkdir -p \"src/screenshots\"");
        #endif

        // Generate timestamp for filename
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::stringstream ss;
        ss << "src/screenshots/screenshot_"
           << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S")
           << "_" << std::setfill('0') << std::setw(3) << ms.count()
           << ".png";

        std::string filename = ss.str();

        // Read pixels from framebuffer
        int width, height;
        glfwGetFramebufferSize(m_window, &width, &height);

        std::vector<unsigned char> pixels(width * height * 3);
        glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

        // Flip image vertically (OpenGL has origin at bottom-left, PNG at top-left)
        std::vector<unsigned char> flipped(width * height * 3);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int src_idx = ((height - 1 - y) * width + x) * 3;
                int dst_idx = (y * width + x) * 3;
                flipped[dst_idx] = pixels[src_idx];
                flipped[dst_idx + 1] = pixels[src_idx + 1];
                flipped[dst_idx + 2] = pixels[src_idx + 2];
            }
        }

        // Save as PNG
        if (stbi_write_png(filename.c_str(), width, height, 3, flipped.data(), width * 3)) {
            std::cout << "[SCREENSHOT] Screenshot saved: " << filename << std::endl;
        } else {
            std::cerr << "[SCREENSHOT] Failed to save screenshot: " << filename << std::endl;
        }
    }

    bool Application::saveScreenshotToFile(const std::string& filename) {
        if (!m_initialized || !m_window) return false;

        int width = 0, height = 0;
        glfwGetFramebufferSize(m_window, &width, &height);
        if (width <= 0 || height <= 0) return false;

        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadBuffer(GL_BACK);
        std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3);
        glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

        std::vector<unsigned char> flipped(pixels.size());
        const std::size_t rowBytes = static_cast<std::size_t>(width) * 3;
        for (int y = 0; y < height; ++y) {
            const std::size_t srcOff = static_cast<std::size_t>(height - 1 - y) * rowBytes;
            const std::size_t dstOff = static_cast<std::size_t>(y) * rowBytes;
            std::memcpy(flipped.data() + dstOff, pixels.data() + srcOff, rowBytes);
        }

        const int ok = stbi_write_png(filename.c_str(), width, height, 3, flipped.data(), width * 3);
        if (ok) {
            std::cout << "[SCREENSHOT] Saved guide image: " << filename << std::endl;
        }
        return ok != 0;
    }

    bool Application::writeCurrentViewSvg(const std::string& filename) {
        FILE* file = std::fopen(filename.c_str(), "wb");
        if (!file) {
            std::cerr << "[SVG] Failed to open SVG file: " << filename << std::endl;
            return false;
        }

        GLint viewport[4] = {0, 0, 0, 0};
        glGetIntegerv(GL_VIEWPORT, viewport);

        GLboolean wasBlendEnabled = glIsEnabled(GL_BLEND);
        GLint previousBlendSrc = GL_ONE;
        GLint previousBlendDst = GL_ZERO;
        glGetIntegerv(GL_BLEND_SRC, &previousBlendSrc);
        glGetIntegerv(GL_BLEND_DST, &previousBlendDst);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        GLint state = GL2PS_OVERFLOW;
        GLint bufferSize = 1024 * 1024;
        m_renderer->setVectorExportMode(true);
        while (state == GL2PS_OVERFLOW) {
            std::rewind(file);
            gl2psBeginPage(
                "alice2",
                "alice2",
                viewport,
                GL2PS_SVG,
                GL2PS_BSP_SORT,
                GL2PS_SILENT | GL2PS_SIMPLE_LINE_OFFSET | GL2PS_USE_CURRENT_VIEWPORT,
                GL_RGBA,
                0,
                nullptr,
                0,
                0,
                0,
                bufferSize,
                file,
                filename.c_str()
            );

            gl2psEnable(GL2PS_BLEND);
            gl2psBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            render();
            gl2psDisable(GL2PS_BLEND);
            state = gl2psEndPage();
            bufferSize *= 2;
        }
        m_renderer->setVectorExportMode(false);

        glBlendFunc(previousBlendSrc, previousBlendDst);
        if (wasBlendEnabled) {
            glEnable(GL_BLEND);
        } else {
            glDisable(GL_BLEND);
        }

        std::fclose(file);

        if (state == GL2PS_SUCCESS) {
            return true;
        }

        std::remove(filename.c_str());
        std::cerr << "[SVG] Failed to save current view: " << filename << " (gl2ps status " << state << ")" << std::endl;
        return false;
    }

    void Application::takeScreenshotSvg() {
        if (!m_initialized || !m_window) {
            std::cerr << "[SVG] Application not initialized" << std::endl;
            return;
        }

        // Create screenshots directory if it doesn't exist
        #ifdef _WIN32
            system("mkdir \"src\\screenshots\" 2>nul");
        #else
            system("mkdir -p \"src/screenshots\"");
        #endif

        // Generate timestamp using the same base naming as screenshots
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::stringstream ss;
        ss << "src/screenshots/screenshot_"
           << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S")
           << "_" << std::setfill('0') << std::setw(3) << ms.count()
           << ".svg";

        std::string filename = ss.str();
        if (writeCurrentViewSvg(filename)) {
            std::cout << "[SVG] Current view saved: " << filename << std::endl;
        }
    }

    void Application::takeScreenshotAllCamerasSvg() {
        if (!m_initialized || !m_window || !m_cameraController) {
            std::cerr << "[SVG] Application not initialized" << std::endl;
            return;
        }

        // Create screenshots directory if it doesn't exist
        #ifdef _WIN32
            system("mkdir \"src\\screenshots\" 2>nul");
        #else
            system("mkdir -p \"src/screenshots\"");
        #endif

        // Generate base timestamp using the same naming as camera screenshots
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::stringstream base_ss;
        base_ss << "src/screenshots/camera_"
                << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S")
                << "_" << std::setfill('0') << std::setw(3) << ms.count();

        std::string base_filename = base_ss.str();
        CameraState originalCamera = m_cameraController->getCurrentCameraState();
        int screenshotCount = 0;

        for (int slot = 0; slot < 8; ++slot) {
            if (m_cameraController->hasSavedCamera(slot)) {
                m_cameraController->loadCamera(slot);

                std::stringstream ss;
                ss << base_filename << "_F" << (slot + 1) << ".svg";
                std::string filename = ss.str();

                if (writeCurrentViewSvg(filename)) {
                    std::cout << "[SVG] Camera F" << (slot + 1) << " SVG saved: " << filename << std::endl;
                    screenshotCount++;
                }
            }
        }

        m_cameraController->setCameraState(originalCamera);

        if (screenshotCount > 0) {
            std::cout << "[SVG] Saved " << screenshotCount << " camera SVG screenshots" << std::endl;
        } else {
            std::cout << "[SVG] No saved cameras found - no SVG screenshots taken" << std::endl;
        }
    }

    void Application::takeScreenshotAllCameras() {
        if (!m_initialized || !m_window || !m_cameraController) {
            std::cerr << "[SCREENSHOT] Application not initialized" << std::endl;
            return;
        }

        // Create screenshots directory if it doesn't exist
        #ifdef _WIN32
            system("mkdir \"src\\screenshots\" 2>nul");
        #else
            system("mkdir -p \"src/screenshots\"");
        #endif

        // Generate base timestamp for filenames
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::stringstream base_ss;
        base_ss << "src/screenshots/camera_"
                << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S")
                << "_" << std::setfill('0') << std::setw(3) << ms.count();

        std::string base_filename = base_ss.str();

        int screenshotCount = 0;

        // Take screenshots from all saved camera positions
        for (int slot = 0; slot < 8; ++slot) {
            if (m_cameraController->hasSavedCamera(slot)) {
                // Load the saved camera
                m_cameraController->loadCamera(slot);

                // Force a render to update the view
                glfwSwapBuffers(m_window);
                render();

                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                // Generate filename for this camera slot
                std::stringstream ss;
                ss << base_filename << "_F" << (slot + 1) << ".png";
                std::string filename = ss.str();

                // Read pixels from framebuffer
                int width, height;
                glfwGetFramebufferSize(m_window, &width, &height);

                std::vector<unsigned char> pixels(width * height * 3);
                glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

                // Flip image vertically
                std::vector<unsigned char> flipped(width * height * 3);
                for (int y = 0; y < height; ++y) {
                    for (int x = 0; x < width; ++x) {
                        int src_idx = ((height - 1 - y) * width + x) * 3;
                        int dst_idx = (y * width + x) * 3;
                        flipped[dst_idx] = pixels[src_idx];
                        flipped[dst_idx + 1] = pixels[src_idx + 1];
                        flipped[dst_idx + 2] = pixels[src_idx + 2];
                    }
                }

                // Save as PNG
                if (stbi_write_png(filename.c_str(), width, height, 3, flipped.data(), width * 3)) {
                    std::cout << "[SCREENSHOT] Camera F" << (slot + 1) << " screenshot saved: " << filename << std::endl;
                    screenshotCount++;
                } else {
                    std::cerr << "[SCREENSHOT] Failed to save camera F" << (slot + 1) << " screenshot: " << filename << std::endl;
                }
            }
        }

        if (screenshotCount > 0) {
            std::cout << "[SCREENSHOT] Saved " << screenshotCount << " camera screenshots" << std::endl;
        } else {
            std::cout << "[SCREENSHOT] No saved cameras found - no screenshots taken" << std::endl;
        }
    }

} // namespace alice2

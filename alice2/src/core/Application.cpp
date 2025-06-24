#include "Application.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <chrono>

// Debug logging flag - set to true to enable detailed application logging
#define DEBUG_APPLICATION_LOGGING false
#define DEBUG_MOUSE_BUTTON_LOGGING false

namespace alice2 {

    Application* Application::s_instance = nullptr;

    Application::Application()
        : m_running(false)
        , m_initialized(false)
        , m_window(nullptr)
        , m_windowTitle("alice2 - 3D Scene Viewer")
        , m_windowWidth(1200)
        , m_windowHeight(800)
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
        Vec3 bgColor = m_scene->getBackgroundColor();
        glClearColor(bgColor.x, bgColor.y, bgColor.z, 1.0f);
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
                        case 'r':
                        case 'R':
                            // Reset camera
                            s_instance->m_cameraController->resetToDefault();
                            break;
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

} // namespace alice2

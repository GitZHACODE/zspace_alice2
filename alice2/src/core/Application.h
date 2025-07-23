#pragma once

#ifndef ALICE2_APPLICATION_H
#define ALICE2_APPLICATION_H

#include "../utils/Math.h"
#include "Scene.h"
#include "Renderer.h"
#include "Camera.h"
#include "../input/InputManager.h"
#include "../input/CameraController.h"
#include "../sketches/SketchManager.h"
#include <string>
#include <memory>

// Forward declare GLFW types
struct GLFWwindow;

namespace alice2 {

    class Application {
    public:
        Application();
        ~Application();

        // Application lifecycle
        bool initialize(int argc, char** argv);
        void run();
        void shutdown();

        // Window management
        void setWindowTitle(const std::string& title);
        void setWindowSize(int width, int height) {m_windowWidth = width; m_windowHeight = height;};
        void getWindowSize(int& width, int& height) const {width = m_windowWidth; height = m_windowHeight;};

        // Core components access
        Scene& getScene() { return *m_scene; }
        Renderer& getRenderer() { return *m_renderer; }
        Camera& getCamera() { return *m_camera; }
        InputManager& getInputManager() { return *m_inputManager; }

        // Screenshot functionality
        void takeScreenshot();
        void takeScreenshotAllCameras();
        CameraController& getCameraController() { return *m_cameraController; }
        SketchManager& getSketchManager() { return *m_sketchManager; }

        // Application state
        bool isRunning() const { return m_running; }
        void quit() { m_running = false; }

        // Time
        float getDeltaTime() const { return m_deltaTime; }
        float getTotalTime() const { return m_totalTime; }
        int getFrameCount() const { return m_frameCount; }
        float getFPS() const { return m_fps; }

        // Settings
        void setVSync(bool enabled);
        void setFullscreen(bool fullscreen);
        void setMultisampling(int samples);

        // Callbacks (called by GLFW)
        static void framebufferSizeCallback(GLFWwindow* window, int width, int height);
        static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
        static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
        static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);
        static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
        static void errorCallback(int error, const char* description);

        // Singleton access
        static Application* getInstance() { return s_instance; }

    private:
        static Application* s_instance;

        // Core components
        std::unique_ptr<Scene> m_scene;
        std::unique_ptr<Renderer> m_renderer;
        std::unique_ptr<Camera> m_camera;
        std::unique_ptr<InputManager> m_inputManager;
        std::unique_ptr<CameraController> m_cameraController;
        std::unique_ptr<SketchManager> m_sketchManager;

        // Application state
        bool m_running;
        bool m_initialized;

        // Window properties
        GLFWwindow* m_window;
        std::string m_windowTitle;
        int m_windowWidth;
        int m_windowHeight;
        bool m_fullscreen;
        bool m_vsync;
        int m_multisampleSamples;

        // Timing
        float m_deltaTime;
        float m_totalTime;
        float m_lastFrameTime;
        int m_frameCount;
        float m_fps;
        float m_fpsUpdateTime;
        int m_fpsFrameCount;

        // Internal methods
        void update();
        void render();
        void updateTiming();
        void updateFPS();
        bool initializeWindow(int argc, char** argv);
        bool initializeOpenGL();
        void setupCallbacks();
    };

    // Global application entry point
    int run(int argc, char** argv);

} // namespace alice2

#endif // ALICE2_APPLICATION_H

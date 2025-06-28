#pragma once

#ifndef ALICE2_ISKETCH_H
#define ALICE2_ISKETCH_H

#include <string>

namespace alice2 {

    class Scene;
    class Renderer;
    class Camera;
    class InputManager;

    // Interface for user sketches
    class ISketch {
    public:
        virtual ~ISketch() = default;

        // Sketch lifecycle
        virtual void setup() = 0;
        virtual void update(float deltaTime) = 0;
        virtual void draw(Renderer& renderer, Camera& camera) = 0;
        virtual void cleanup() {}

        // Input handling (optional)
        // Return true if the event was handled and should not be processed by default handlers
        virtual bool onKeyPress(unsigned char /*key*/, int /*x*/, int /*y*/) { return false; }
        virtual bool onMousePress(int /*button*/, int /*state*/, int /*x*/, int /*y*/) { return false; }
        virtual bool onMouseMove(int /*x*/, int /*y*/) { return false; }

        // Sketch information
        virtual std::string getName() const = 0;
        virtual std::string getDescription() const { return ""; }
        virtual std::string getAuthor() const { return ""; }
        virtual std::string getVersion() const { return "1.0"; }

        // Access to alice2 components
        void setScene(Scene* scene) { m_scene = scene; }
        void setRenderer(Renderer* renderer) { m_renderer = renderer; }
        void setCamera(Camera* camera) { m_camera = camera; }
        void setInputManager(InputManager* inputManager) { m_inputManager = inputManager; }

    protected:
        Scene* m_scene = nullptr;
        Renderer* m_renderer = nullptr;
        Camera* m_camera = nullptr;
        InputManager* m_inputManager = nullptr;

        // Helper methods for sketches
        Scene& scene() { return *m_scene; }
        Renderer& renderer() { return *m_renderer; }
        Camera& camera() { return *m_camera; }
        InputManager& input() { return *m_inputManager; }
    };

    // Macro to help register sketches
    #define ALICE2_REGISTER_SKETCH(SketchClass) \
        extern "C" { \
            alice2::ISketch* createSketch() { \
                return new SketchClass(); \
            } \
            void destroySketch(alice2::ISketch* sketch) { \
                delete sketch; \
            } \
            const char* getSketchName() { \
                SketchClass temp; \
                static std::string name = temp.getName(); \
                return name.c_str(); \
            } \
        }

} // namespace alice2

#endif // ALICE2_ISKETCH_H

#pragma once

#ifndef ALICE2_SKETCH_MANAGER_H
#define ALICE2_SKETCH_MANAGER_H

#include "ISketch.h"
#include "SketchRegistry.h"
#include <memory>
#include <vector>
#include <string>
#include <functional>

namespace alice2 {

    class Scene;
    class Renderer;
    class Camera;
    class InputManager;

    struct SketchInfo {
        std::string name;
        std::string description;
        std::string author;
        std::string version;
        std::string filePath;
        bool isLoaded;
    };

    class SketchManager {
    public:
        SketchManager();
        ~SketchManager();

        // Initialization
        void initialize(Scene* scene, Renderer* renderer, Camera* camera, InputManager* inputManager);

        // Sketch discovery and loading
        void scanUserSrcDirectory(const std::string& directory = "userSrc");
        void loadSketch(const std::string& name);
        void unloadCurrentSketch();
        void reloadCurrentSketch();

        // Sketch switching
        void switchToNextSketch();
        void switchToPreviousSketch();
        void switchToSketch(int index);
        int getCurrentSketchIndex() const;

        // Sketch management
        bool hasCurrentSketch() const { return m_currentSketch != nullptr; }
        ISketch* getCurrentSketch() const { return m_currentSketch.get(); }
        const std::string& getCurrentSketchName() const { return m_currentSketchName; }

        // Available sketches
        const std::vector<SketchInfo>& getAvailableSketches() const { return m_availableSketches; }
        bool isSketchAvailable(const std::string& name) const;

        // Sketch lifecycle
        void setupCurrentSketch();
        void updateCurrentSketch(float deltaTime);
        void drawCurrentSketch(Renderer& renderer, Camera& camera);
        void cleanupCurrentSketch();

        // Input forwarding
        bool forwardKeyPress(unsigned char key, int x, int y);
        bool forwardMousePress(int button, int state, int x, int y);
        bool forwardMouseMove(int x, int y);

        // Hot reload
        void enableHotReload(bool enable) { m_hotReloadEnabled = enable; }
        bool isHotReloadEnabled() const { return m_hotReloadEnabled; }
        void checkForChanges(); // Call periodically to check for file changes

        // Error handling
        bool hasError() const { return !m_lastError.empty(); }
        const std::string& getLastError() const { return m_lastError; }
        void clearError() { m_lastError.clear(); }

        // Callbacks
        using SketchLoadedCallback = std::function<void(const std::string&)>;
        using SketchUnloadedCallback = std::function<void(const std::string&)>;
        using SketchErrorCallback = std::function<void(const std::string&, const std::string&)>;

        void setSketchLoadedCallback(SketchLoadedCallback callback) { m_sketchLoadedCallback = callback; }
        void setSketchUnloadedCallback(SketchUnloadedCallback callback) { m_sketchUnloadedCallback = callback; }
        void setSketchErrorCallback(SketchErrorCallback callback) { m_sketchErrorCallback = callback; }

    private:
        // Core components
        Scene* m_scene;
        Renderer* m_renderer;
        Camera* m_camera;
        InputManager* m_inputManager;

        // Current sketch
        std::unique_ptr<ISketch> m_currentSketch;
        std::string m_currentSketchName;
        std::string m_currentSketchPath;
        int m_currentSketchIndex;

        // Available sketches
        std::vector<SketchInfo> m_availableSketches;
        std::string m_userSrcDirectory;

        // Hot reload
        bool m_hotReloadEnabled;
        std::vector<std::pair<std::string, long long>> m_fileTimestamps; // file path, last modified time

        // Error handling
        std::string m_lastError;

        // Callbacks
        SketchLoadedCallback m_sketchLoadedCallback;
        SketchUnloadedCallback m_sketchUnloadedCallback;
        SketchErrorCallback m_sketchErrorCallback;

        // Internal methods
        void scanDirectory(const std::string& directory);
        bool compileSketch(const std::string& filePath, std::string& outputPath);
        bool loadCompiledSketch(const std::string& libraryPath);
        void updateFileTimestamps();
        bool hasFileChanged(const std::string& filePath) const;
        long long getFileModificationTime(const std::string& filePath) const;
        void setError(const std::string& error);

        // Platform-specific dynamic loading
        void* loadLibrary(const std::string& path);
        void unloadLibrary(void* handle);
        void* getSymbol(void* handle, const std::string& name);

        void* m_currentLibraryHandle;
    };

} // namespace alice2

#endif // ALICE2_SKETCH_MANAGER_H

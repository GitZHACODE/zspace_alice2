#include "SketchManager.h"
#include "SketchRegistry.h"
#include "../core/Scene.h"
#include "../core/Renderer.h"
#include "../core/Camera.h"
#include "../input/InputManager.h"
#include <iostream>

namespace alice2
{

    SketchManager::SketchManager()
        : m_scene(nullptr), m_renderer(nullptr), m_camera(nullptr), m_inputManager(nullptr), m_currentSketch(nullptr), m_currentSketchIndex(-1), m_userSrcDirectory("userSrc"), m_hotReloadEnabled(false), m_currentLibraryHandle(nullptr)
    {
    }

    SketchManager::~SketchManager()
    {
        unloadCurrentSketch();
    }

    void SketchManager::initialize(Scene *scene, Renderer *renderer, Camera *camera, InputManager *inputManager)
    {
        m_scene = scene;
        m_renderer = renderer;
        m_camera = camera;
        m_inputManager = inputManager;
    }

    void SketchManager::scanUserSrcDirectory(const std::string &directory)
    {
        m_userSrcDirectory = directory;
        m_availableSketches.clear();

        // Get sketches from the registry
        const auto &registeredSketches = SketchRegistry::getInstance().getSketches();

        for (const auto &regSketch : registeredSketches)
        {
            SketchInfo sketchInfo;
            sketchInfo.name = regSketch.name;
            sketchInfo.description = regSketch.description;
            sketchInfo.author = regSketch.author;
            sketchInfo.version = regSketch.version;
            sketchInfo.filePath = directory + "/" + regSketch.name + ".cpp"; // Placeholder path
            sketchInfo.isLoaded = false;

            m_availableSketches.push_back(sketchInfo);
        }

        // Also add the legacy base sketch if it exists
        if (!SketchRegistry::getInstance().hasSketch("Base Sketch"))
        {
            SketchInfo baseSketch;
            baseSketch.name = "Base Sketch";
            baseSketch.description = "Basic template sketch";
            baseSketch.author = "alice2";
            baseSketch.version = "1.0";
            baseSketch.filePath = directory + "/sketch_base.cpp";
            baseSketch.isLoaded = false;

            m_availableSketches.push_back(baseSketch);
        }

        std::cout << "\nFound " << m_availableSketches.size() << " sketches available" << std::endl;
        for (const auto &sketch : m_availableSketches)
        {
            std::cout << "  - " << sketch.name << " by " << sketch.author << std::endl;
        }
    }

    void SketchManager::loadSketch(const std::string &name)
    {
        // Create the sketch via the auto-registrar
        auto newSketch = SketchRegistry::getInstance().createSketch(name);
        if (!newSketch)
        {
            setError("Sketch not found: " + name);
            return;
        }

        // Unload any existing sketch
        if (m_currentSketch)
        {
            unloadCurrentSketch();
        }

        // Adopt the new sketch
        m_currentSketch = std::move(newSketch);
        m_currentSketchName = name;

        // Update the current-sketch index
        m_currentSketchIndex = -1;
        for (size_t i = 0; i < m_availableSketches.size(); ++i)
        {
            if (m_availableSketches[i].name == name)
            {
                m_currentSketchIndex = static_cast<int>(i);
                break;
            }
        }

        // Wire up core components
        m_currentSketch->setScene(m_scene);
        m_currentSketch->setRenderer(m_renderer);
        m_currentSketch->setCamera(m_camera);
        m_currentSketch->setInputManager(m_inputManager);

        setupCurrentSketch();

        std::cout << "Loaded sketch: " << name
                  << " (" << (m_currentSketchIndex + 1)
                  << "/" << m_availableSketches.size() << ")\n";

        if (m_sketchLoadedCallback)
        {
            m_sketchLoadedCallback(name);
        }
    }

    void SketchManager::unloadCurrentSketch()
    {
        if (!m_currentSketch)
            return;

        // Let the sketch clean up
        cleanupCurrentSketch();

        // Release the instance
        m_currentSketch.reset();
        std::string oldName = m_currentSketchName;
        m_currentSketchName.clear();
        m_currentSketchIndex = -1;

        // Notify listeners
        if (m_sketchUnloadedCallback)
        {
            m_sketchUnloadedCallback(oldName);
        }

        std::cout << "Unloaded sketch: " << oldName << "\n";
    }
    void SketchManager::reloadCurrentSketch()
    {
        if (!m_currentSketchName.empty())
        {
            std::string name = m_currentSketchName;
            unloadCurrentSketch();
            loadSketch(name);
        }
    }

    bool SketchManager::isSketchAvailable(const std::string &name) const
    {
        for (const auto &sketch : m_availableSketches)
        {
            if (sketch.name == name)
            {
                return true;
            }
        }
        return false;
    }

    void SketchManager::setupCurrentSketch()
    {
        if (m_currentSketch)
        {
            try
            {
                m_currentSketch->setup();
            }
            catch (const std::exception &e)
            {
                setError("Error in sketch setup: " + std::string(e.what()));
            }
        }
    }

    void SketchManager::updateCurrentSketch(float deltaTime)
    {
        if (m_currentSketch)
        {
            try
            {
                m_currentSketch->update(deltaTime);
            }
            catch (const std::exception &e)
            {
                setError("Error in sketch update: " + std::string(e.what()));
            }
        }
    }

    void SketchManager::drawCurrentSketch(Renderer &renderer, Camera &camera)
    {
        if (m_currentSketch)
        {
            try
            {
                m_currentSketch->draw(renderer, camera);
            }
            catch (const std::exception &e)
            {
                setError("Error in sketch draw: " + std::string(e.what()));
            }
        }
    }

    void SketchManager::cleanupCurrentSketch()
    {
        if (m_currentSketch)
        {
            try
            {
                m_currentSketch->cleanup();
            }
            catch (const std::exception &e)
            {
                setError("Error in sketch cleanup: " + std::string(e.what()));
            }
        }
    }

    bool SketchManager::forwardKeyPress(unsigned char key, int x, int y)
    {
        if (m_currentSketch)
        {
            try
            {
                return m_currentSketch->onKeyPress(key, x, y);
            }
            catch (const std::exception &e)
            {
                setError("Error in sketch key press: " + std::string(e.what()));
            }
        }
        return false;
    }

    bool SketchManager::forwardMousePress(int button, int state, int x, int y)
    {
        if (m_currentSketch)
        {
            try
            {
                return m_currentSketch->onMousePress(button, state, x, y);
            }
            catch (const std::exception &e)
            {
                setError("Error in sketch mouse press: " + std::string(e.what()));
            }
        }
        return false;
    }

    bool SketchManager::forwardMouseMove(int x, int y)
    {
        if (m_currentSketch)
        {
            try
            {
                return m_currentSketch->onMouseMove(x, y);
            }
            catch (const std::exception &e)
            {
                setError("Error in sketch mouse move: " + std::string(e.what()));
            }
        }
        return false;
    }

    void SketchManager::checkForChanges()
    {
        // Hot reload not implemented yet
    }

    void SketchManager::setError(const std::string &error)
    {
        m_lastError = error;
        std::cerr << "SketchManager Error: " << error << std::endl;

        if (m_sketchErrorCallback)
        {
            m_sketchErrorCallback(m_currentSketchName, error);
        }
    }

    // Platform-specific stubs (not implemented yet)
    void *SketchManager::loadLibrary(const std::string &path)
    {
        return nullptr;
    }

    void SketchManager::unloadLibrary(void *handle)
    {
        // Not implemented
    }

    void *SketchManager::getSymbol(void *handle, const std::string &name)
    {
        return nullptr;
    }

    void SketchManager::switchToNextSketch()
    {
        if (m_availableSketches.empty())
            return;

        int nextIndex = (m_currentSketchIndex + 1) % static_cast<int>(m_availableSketches.size());
        switchToSketch(nextIndex);
    }

    void SketchManager::switchToPreviousSketch()
    {
        if (m_availableSketches.empty())
            return;

        int prevIndex = m_currentSketchIndex - 1;
        if (prevIndex < 0)
        {
            prevIndex = static_cast<int>(m_availableSketches.size()) - 1;
        }
        switchToSketch(prevIndex);
    }

    void SketchManager::switchToSketch(int index)
    {
        if (index < 0 || index >= static_cast<int>(m_availableSketches.size()))
        {
            return;
        }

        const std::string &sketchName = m_availableSketches[index].name;
        loadSketch(sketchName);
    }

    int SketchManager::getCurrentSketchIndex() const
    {
        return m_currentSketchIndex;
    }

} // namespace alice2

#pragma once

#ifndef ALICE2_SKETCH_REGISTRY_H
#define ALICE2_SKETCH_REGISTRY_H

#include "ISketch.h"
#include <memory>
#include <vector>
#include <string>
#include <functional>

namespace alice2 {

    // Function type for sketch factory functions
    using SketchFactory = std::function<std::unique_ptr<ISketch>()>;

    struct RegisteredSketch {
        std::string name;
        std::string description;
        std::string author;
        std::string version;
        SketchFactory factory;
    };

    class SketchRegistry {
    public:
        static SketchRegistry& getInstance() {
            static SketchRegistry instance;
            return instance;
        }

        // Register a sketch with the registry
        void registerSketch(const std::string& name, 
                          const std::string& description,
                          const std::string& author,
                          const std::string& version,
                          SketchFactory factory) {
            RegisteredSketch sketch;
            sketch.name = name;
            sketch.description = description;
            sketch.author = author;
            sketch.version = version;
            sketch.factory = factory;
            
            m_sketches.push_back(sketch);
        }

        // Get all registered sketches
        const std::vector<RegisteredSketch>& getSketches() const {
            return m_sketches;
        }

        // Create a sketch by name
        std::unique_ptr<ISketch> createSketch(const std::string& name) const {
            for (const auto& sketch : m_sketches) {
                if (sketch.name == name) {
                    return sketch.factory();
                }
            }
            return nullptr;
        }

        // Check if a sketch exists
        bool hasSketch(const std::string& name) const {
            for (const auto& sketch : m_sketches) {
                if (sketch.name == name) {
                    return true;
                }
            }
            return false;
        }

        // Get sketch info by name
        const RegisteredSketch* getSketchInfo(const std::string& name) const {
            for (const auto& sketch : m_sketches) {
                if (sketch.name == name) {
                    return &sketch;
                }
            }
            return nullptr;
        }

    private:
        SketchRegistry() = default;
        std::vector<RegisteredSketch> m_sketches;
    };

    // Helper class for automatic sketch registration
    template<typename SketchClass>
    class SketchRegistrar {
    public:
        SketchRegistrar() {
            // Create a temporary instance to get sketch info
            SketchClass temp;
            
            SketchRegistry::getInstance().registerSketch(
                temp.getName(),
                temp.getDescription(),
                temp.getAuthor(),
                temp.getVersion(),
                []() -> std::unique_ptr<ISketch> {
                    return std::make_unique<SketchClass>();
                }
            );
        }
    };

    // Macro to automatically register sketches
    #define ALICE2_REGISTER_SKETCH_AUTO(SketchClass) \
        namespace { \
            static alice2::SketchRegistrar<SketchClass> g_##SketchClass##_registrar; \
        }

} // namespace alice2

#endif // ALICE2_SKETCH_REGISTRY_H

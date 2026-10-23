#include "core/Application.h"
#include "sketches/SketchRegistry.h"
#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    try {
        const auto& sketches = alice2::SketchRegistry::getInstance().getSketches();
        if (argc > 1 && std::string(argv[1]) == "--list-sketches") {
            for (const auto& sketch : sketches) std::cout << sketch.name << '\n';
            return sketches.empty() ? 1 : 0;
        }
        // Resolve existing data/... paths consistently from IDEs and terminals.
        if (std::filesystem::is_directory(ALICE2_SOURCE_DIR))
            std::filesystem::current_path(ALICE2_SOURCE_DIR);
        return alice2::run(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "alice2: " << error.what() << '\n';
        return 1;
    }
}

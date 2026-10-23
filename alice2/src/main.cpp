#include "core/Application.h"
#include "utils/Runtime.h"
#include "sketches/SketchRegistry.h"
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    try {
        alice2::initializeRuntime(argv[0]);
        const auto& sketches = alice2::SketchRegistry::getInstance().getSketches();
        if (argc > 1 && std::string(argv[1]) == "--list-sketches") {
            for (const auto& sketch : sketches) std::cout << sketch.name << '\n';
            return sketches.empty() ? 1 : 0;
        }
        return alice2::run(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "alice2: " << error.what() << '\n';
        return 1;
    }
}

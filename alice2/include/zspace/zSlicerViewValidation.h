#pragma once
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace alice2::slicing {
// Opt-in developer validation. Normal interactive launches remain empty/input
// driven; validation launches use the real sketch, renderer and saved F1 camera.
struct SlicerViewValidation {
    std::string Input;
    bool Topology = false;
    int RelaxIterations = 8;
    int DisplayMode = 6;
    static std::string environment(const char* name) {
#if defined(_WIN32)
        char* buffer = nullptr;
        size_t length = 0;
        if (_dupenv_s(&buffer, &length, name) != 0) throw std::runtime_error("Cannot read validation option.");
        const std::string value = buffer ? buffer : "";
        std::free(buffer);
        return value;
#else
        const char* value = std::getenv(name);
        return value ? value : "";
#endif
    }
    static SlicerViewValidation read() {
        SlicerViewValidation options;
        options.Input = environment("ALICE2_VALIDATE_SLICER_INPUT");
        if (options.Input.empty()) return options;
        const auto mode = environment("ALICE2_VALIDATE_SLICER_MODE");
        if (!mode.empty() && mode != "harmonic" && mode != "topology") throw std::runtime_error("Validation mode must be harmonic or topology.");
        options.Topology = mode == "topology";
        const auto count = environment("ALICE2_VALIDATE_RELAX_ITERATIONS");
        if (!count.empty()) options.RelaxIterations = std::stoi(count);
        const auto view = environment("ALICE2_VALIDATE_DISPLAY_MODE");
        if (!view.empty()) options.DisplayMode = std::stoi(view);
        if (options.RelaxIterations < 0 || options.RelaxIterations > 100 || options.DisplayMode < 0 || options.DisplayMode > 7)
            throw std::runtime_error("Invalid slicer validation iteration/display option.");
        return options;
    }
};
}

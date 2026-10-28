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
    int Layer = 0;
    int HeightSurfaces = -1; // -1: settings default; 0/1: isolate global schedule vs surface correction.
    int HeightOptimization = -1; // -1: use the USD metadata; 0/1: A/B validation.
    std::string Output;
    bool AllowPartial = false; // Diagnostic captures never export partial paths.
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
        options.Output = environment("ALICE2_VALIDATE_SLICER_OUTPUT");
        options.AllowPartial = environment("ALICE2_VALIDATE_ALLOW_PARTIAL") == "1";
        const auto mode = environment("ALICE2_VALIDATE_SLICER_MODE");
        if (!mode.empty() && mode != "harmonic" && mode != "topology") throw std::runtime_error("Validation mode must be harmonic or topology.");
        options.Topology = mode == "topology";
        const auto count = environment("ALICE2_VALIDATE_RELAX_ITERATIONS");
        if (!count.empty()) options.RelaxIterations = std::stoi(count);
        const auto view = environment("ALICE2_VALIDATE_DISPLAY_MODE");
        if (!view.empty()) options.DisplayMode = std::stoi(view);
        const auto layer=environment("ALICE2_VALIDATE_LAYER");
        if(!layer.empty())options.Layer=std::stoi(layer);
        const auto surfaces=environment("ALICE2_VALIDATE_HEIGHT_SURFACES");
        if(!surfaces.empty())options.HeightSurfaces=std::stoi(surfaces);
        const auto height=environment("ALICE2_VALIDATE_HEIGHT_OPTIMIZATION");
        if(!height.empty())options.HeightOptimization=std::stoi(height);
        if (options.RelaxIterations < 0 || options.RelaxIterations > 100 || options.DisplayMode < 0 || options.DisplayMode > (options.Topology ? 8 : 9) || options.Layer < 0 || options.HeightOptimization < -1 || options.HeightOptimization > 1 || options.HeightSurfaces < -1 || options.HeightSurfaces > 1)
            throw std::runtime_error("Invalid slicer validation iteration/display option.");
        return options;
    }
};
}

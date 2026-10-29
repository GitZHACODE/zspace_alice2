#pragma once
#include <zspace/slicing.h>
#include <nlohmann/json.hpp>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace alice2::slicing {
// Defaults in millimetres for settings omitted from the USD stage.
inline zSpace::SlicerSettings defaultSlicerSettings()
{
    zSpace::SlicerSettings s;
    s.BoundaryWidth = {60.0f};
    s.BracingWidth = 40.0f;
    s.OverlapWidth = 0.0f;
    s.DoubleBoundary = true;
    s.SlotOffset = 1.0f;
    s.BracingSlot = 0.5f;
    s.FieldScaleUp = 1.1f;
    s.FieldCellSize = 5.0f;
    s.LayerSpacing = 8.0f;
    s._HTraceStepFraction = 0.2;
    s._HTraceMaxSteps = 100000;
    s._HEqualiseIterations = 2;
    s._HArapIterations = 10;
    s._HUnrollStart = 0;
    s.HPrintHeight.Tolerance = 0.05; // Approved project fabrication allowance, in mm.
    s.HPrintHeight.Iterations = 32; // Larger batches avoid repeated path reconstruction.
    s.HPrintHeight.AllowLayerCountChange = true;
    return s;
}

inline zSpace::PrintSynSetting defaultPrintSynSettings()
{
    zSpace::PrintSynSetting s;
    s.ResampleLength = 5.0f;
    s.InnerResampleLength = 20.0f;
    s.SurfaceChordTolerance = 0.1;
    s.FeatureAngleTol = 120.0f;
    s.SeamOffset = 40.0f;
    s.SeamSize = 20.0f;
    return s;
}

// Applies a settings JSON's "slicer" object onto s. TLayerSpacing maps to
// the shared LayerSpacing; keys that match nothing (e.g. the removed
// TTopoSplit / TAngleSplit / TAngleSplitTol) are appended to unknown.
// LayerSpacing is the only layering setting. Throws nlohmann::json::exception
// on a value of the wrong type.
inline void applySlicerJson(const nlohmann::json& obj, zSpace::SlicerSettings& s, std::vector<std::string>& unknown)
{
    if (obj.contains("BracingSlotEven") || obj.contains("BracingSlotOdd"))
        throw std::runtime_error("Use BracingSlot and SlotOffset; separate even/odd bracing slots are no longer supported.");
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        std::string key = it.key();
        if (key == "TLayerSpacing")
            key = "LayerSpacing"; // was Topology-only before LayerSpacing became shared
        const nlohmann::json& v = it.value();
        if (key == "BoundaryWidth") {
            s.BoundaryWidth.clear();
            if (v.is_array())
                for (const nlohmann::json& w : v) s.BoundaryWidth.push_back(w.get<float>());
            else
                s.BoundaryWidth.push_back(v.get<float>());
        }
        else if (key == "BracingWidth") {
            if (v.is_array()) {
                if (v.empty()) throw std::runtime_error("BracingWidth cannot be empty");
                s.BracingWidth = v.at(0).get<float>(); // Per-edge widths resolved by USD input.
            } else s.BracingWidth = v.get<float>();
        }
        else if (key == "OverlapWidth") s.OverlapWidth = v.get<float>();
        else if (key == "DoubleBoundary") s.DoubleBoundary = v.get<bool>();
        else if (key == "SlotOffset" || key == "BoundarySlotOffset") s.SlotOffset = v.get<float>();
        else if (key == "BracingSlot") s.BracingSlot = v.get<float>();
        else if (key == "FieldScaleUp") s.FieldScaleUp = v.get<float>();
        else if (key == "FieldCellSize") s.FieldCellSize = v.get<float>();
        else if (key == "LayerSpacing") s.LayerSpacing = v.get<float>();
        else if (key == "_HTraceStepFraction" || key == "HTraceStepFraction") s._HTraceStepFraction = v.get<double>();
        else if (key == "_HTraceMaxSteps" || key == "HTraceMaxSteps") s._HTraceMaxSteps = v.get<int>();
        else if (key == "_HEqualiseIterations" || key == "HEqualiseIterations") s._HEqualiseIterations = v.get<int>();
        else if (key == "_HArapIterations" || key == "HArapIterations") s._HArapIterations = v.get<int>();
        else if (key == "_HUnrollStart" || key == "HUnrollStart") s._HUnrollStart = v.get<int>();
        else if (key == "HPrintHeightAllowLayerCountChange") s.HPrintHeight.AllowLayerCountChange = v.get<bool>();
        else if (key == "HPrintHeightOptimizeSurfaces") s.HPrintHeight.OptimizeSurfaces = v.get<bool>();
        else unknown.push_back("slicer." + it.key());
    }
}

// Same for the "printSyn" object.
inline void applyPrintSynJson(const nlohmann::json& obj, zSpace::PrintSynSetting& s, std::vector<std::string>& unknown)
{
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        const std::string& key = it.key();
        const nlohmann::json& v = it.value();
        if (key == "ResampleLength") s.ResampleLength = v.get<float>();
        else if (key == "InnerResampleLength") {
            s.InnerResampleLength = v.get<float>();
            if (!std::isfinite(s.InnerResampleLength) || s.InnerResampleLength < zSpace::zTsField::MinPhysicalParameter)
                throw std::runtime_error("InnerResampleLength must be finite and at least 0.0001 mm.");
        }
        else if (key == "SurfaceChordTolerance") s.SurfaceChordTolerance = v.get<double>();
        else if (key == "FeatureAngleTol") s.FeatureAngleTol = v.get<float>();
        else if (key == "SeamOffset") s.SeamOffset = v.get<float>();
        else if (key == "SeamSize") s.SeamSize = v.get<float>();
        else unknown.push_back("printSyn." + key);
    }
}


} // namespace alice2::slicing

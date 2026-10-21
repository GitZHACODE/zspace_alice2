#pragma once
#include <zspace/slicing.h>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace alice2::slicing {
// Defaults for settings omitted from the USD stage.
inline zSpace::SlicerSettings defaultSlicerSettings()
{
    zSpace::SlicerSettings s;
    s.BoundaryWidth = {0.06f};
    s.BracingWidth = 0.04f;
    s.OverlapWidth = 0.0f;
    s.DoubleBoundary = true;
    s.BoundarySlotOffset = 1.0f;
    s.BracingSlotEven = 0.45f;
    s.BracingSlotOdd = 0.55f;
    s.FieldScaleUp = 1.1f;
    s.FieldCellSize = 0.005f;
    s.LayerSpacing = 0.008f;
    s.HTraceStepFraction = 0.2;
    s.HTraceMaxSteps = 100000;
    s.HEqualiseIterations = 2;
    s.HArapIterations = 10;
    s.HUnrollStart = 0;
    return s;
}

inline zSpace::PrintSynSetting defaultPrintSynSettings()
{
    zSpace::PrintSynSetting s;
    s.ResampleLength = 0.005f;
    s.FeatureAngleTol = 120.0f;
    s.SeamOffset = 0.04f;
    s.SeamSize = 0.02f;
    return s;
}

// Applies a settings JSON's "slicer" object onto s. TLayerSpacing maps to
// the shared LayerSpacing; keys that match nothing (e.g. the removed
// TTopoSplit / TAngleSplit / TAngleSplitTol) are appended to unknown.
// LayerSpacing is the only layering setting. Throws nlohmann::json::exception
// on a value of the wrong type.
inline void applySlicerJson(const nlohmann::json& obj, zSpace::SlicerSettings& s, std::vector<std::string>& unknown)
{
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
        else if (key == "BoundarySlotOffset") s.BoundarySlotOffset = v.get<float>();
        else if (key == "BracingSlotEven") s.BracingSlotEven = v.get<float>();
        else if (key == "BracingSlotOdd") s.BracingSlotOdd = v.get<float>();
        else if (key == "FieldScaleUp") s.FieldScaleUp = v.get<float>();
        else if (key == "FieldCellSize") s.FieldCellSize = v.get<float>();
        else if (key == "LayerSpacing") s.LayerSpacing = v.get<float>();
        else if (key == "HTraceStepFraction") s.HTraceStepFraction = v.get<double>();
        else if (key == "HTraceMaxSteps") s.HTraceMaxSteps = v.get<int>();
        else if (key == "HEqualiseIterations") s.HEqualiseIterations = v.get<int>();
        else if (key == "HArapIterations") s.HArapIterations = v.get<int>();
        else if (key == "HUnrollStart") s.HUnrollStart = v.get<int>();
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
        else if (key == "FeatureAngleTol") s.FeatureAngleTol = v.get<float>();
        else if (key == "SeamOffset") s.SeamOffset = v.get<float>();
        else if (key == "SeamSize") s.SeamSize = v.get<float>();
        else unknown.push_back("printSyn." + key);
    }
}


} // namespace alice2::slicing

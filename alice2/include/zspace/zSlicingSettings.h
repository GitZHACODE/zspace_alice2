#pragma once
#include <zspace/slicing.h>

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
    return s;
}

inline zSpace::PrintSynSetting defaultPrintSynSettings()
{
    zSpace::PrintSynSetting s;
    s.ResampleLength = 5.0f;
    s.FeatureAngleTol = 120.0f;
    s.SeamOffset = 40.0f;
    s.SeamSize = 20.0f;
    return s;
}

} // namespace alice2::slicing

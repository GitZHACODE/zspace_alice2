#pragma once

#ifndef ALICE2_ZSLICINGPARAMETERS_H
#define ALICE2_ZSLICINGPARAMETERS_H

#include <zspace/interface.h>

namespace alice2 {
namespace SlicingParameters {

    inline const char* inputMeshPath = "data/carbcomn/carbMesh.obj";
    inline const char* bracingGraphPath = "data/Carbcomn/graph.obj";

    constexpr int longitudeCornerStartVertexId = 43;
    constexpr int longitudeCornerEndVertexId = 66;

    // Longitudinal slicing: this is the layer spacing / print height used to walk the longitude direction.
    constexpr float longitudeLayerSpacing = 0.01f;

    constexpr float printBoundaryWidth = 0.018f;
    constexpr float printBracingWidth = 0.024f;
    constexpr float printOverlapWidth = 0.002f;

    constexpr float boundarySdfTarget = (printBoundaryWidth * 0.5f) - printOverlapWidth;
    constexpr float bracingSdfTarget = (printBracingWidth * 0.5f) - printOverlapWidth;
    constexpr float trimSlotWidth = printBracingWidth - (3.0f * printOverlapWidth);
    constexpr float edgeTrimSlotWidth = printBoundaryWidth - (3.0f * printOverlapWidth);

    constexpr float sdfWidth = 0.001f;
    constexpr int sdfFieldResolutionX = 200;
    constexpr int sdfFieldResolutionY = 200;
    inline const zSpace::zDomain<zSpace::zPoint> sdfFieldBounds(
        zSpace::zPoint(-0.25, -1.2, 0.0),
        zSpace::zPoint(0.25, 0.2, 0.0)
    );

    constexpr float trimSlotStaggerEven = 0.4f;//ratio
    constexpr float trimSlotStaggerOdd = 0.6f;
    constexpr float trimSlotMinLength = 0.10f;
    constexpr float trimSlotLengthExtra = 0.002f;

    constexpr double contourCleanupMergeTolerance = 0.0005;
    constexpr double contourEndpointCloseMultiplier = 6.0;
    constexpr double contourEndpointCloseMinTolerance = 0.03;

    constexpr float postProcessSampleLength = 0.014285714f;
    constexpr float postProcessFeatureAngleThreshold = 30.0f;
    constexpr float postProcessSdfSearchPadding = 0.006f;

} // namespace SlicingParameters
} // namespace alice2

#endif // ALICE2_ZSLICINGPARAMETERS_H

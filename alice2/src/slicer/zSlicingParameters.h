#pragma once

#ifndef ALICE2_ZSLICINGPARAMETERS_H
#define ALICE2_ZSLICINGPARAMETERS_H

#include <zspace/interface.h>

namespace alice2 {
namespace SlicingParameters {

    inline const char* inputMeshPath = "data/londonCreatesExhibition/block0.obj";
    inline const char* bracingGraphPath = "data/londonCreatesExhibition/graph.obj";

    constexpr int longitudeCornerStartVertexId = 23;
    constexpr int longitudeCornerEndVertexId = 21;
    constexpr int bracingInputGraphPairCount = 2;
    constexpr int bracingInputBottomGraphIds[bracingInputGraphPairCount] = { 0, 1 };
    constexpr int bracingInputTopGraphIds[bracingInputGraphPairCount] = { 2, 3 };

    // Longitudinal slicing: this is the layer spacing / print height used to walk the longitude direction.
    constexpr float longitudeLayerSpacing = 0.09f;
    // constexpr float longitudeLayerSpacing = 3.01f;

    //actual concrete thickness will be = printwidth*2 - overlapwidth
    constexpr float printBoundaryWidth = 0.30f;
    constexpr float printBracingWidth = 0.30f;
    constexpr float printOverlapWidth = 0.0f;

    constexpr float boundarySdfTarget = (printBoundaryWidth * 0.5f) - printOverlapWidth;
    constexpr float bracingSdfTarget = (printBracingWidth * 0.5f) - printOverlapWidth;
    constexpr float trimSlotWidth = printBracingWidth - ( printOverlapWidth);
    constexpr float edgeTrimSlotWidth = printBoundaryWidth - (printOverlapWidth);

    constexpr float sdfWidth = 0.001f;
    constexpr int sdfFieldResolutionX = 200;
    constexpr int sdfFieldResolutionY = 200;
    inline const zSpace::zDomain<zSpace::zPoint> sdfFieldBounds(
        zSpace::zPoint(-7, -16, 0.0),
        zSpace::zPoint(3, 2, 0.0)
    );



    constexpr float trimSlotStaggerEven = 0.4f;//ratio
    constexpr float trimSlotStaggerOdd = 0.6f;
    constexpr int boundaryTrimSegmentId = 2;
    constexpr float boundaryTrimSlotRatioEven = 0.1f;
    constexpr float boundaryTrimSlotRatioOdd = 0.9f;
    constexpr float trimSlotMinLength = 0.10f;
    constexpr float trimSlotLengthExtra = 0.05f;

    constexpr double bracingFeatureExtensionRatio = 0.10;
    constexpr double bracingFeatureMinLength = 1e-6;

    constexpr double contourCleanupMergeTolerance = 0.13;
    constexpr double contourEndpointCloseMultiplier = 6.0;
    constexpr double contourEndpointCloseMinTolerance = 0.4;

    constexpr float postProcessSampleLength = 0.15f;
    constexpr float postProcessFeatureAngleThreshold = 30.0f;
    constexpr float postProcessSdfSearchPadding = 0.6f;

} // namespace SlicingParameters
} // namespace alice2

#endif // ALICE2_ZSLICINGPARAMETERS_H

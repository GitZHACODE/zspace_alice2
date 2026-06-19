#pragma once

#ifndef ALICE2_ZSPACE_DRAW_H
#define ALICE2_ZSPACE_DRAW_H

#include "zDisplaySettings.h"

#if ALICE2_WITH_ZSPACE_CORE
namespace zSpace {
    class zObjectGraph;
    class zObjectMesh;
    class zObjectPointCloud;
}
#endif

namespace alice2 {

    class Renderer;

#if ALICE2_WITH_ZSPACE_CORE
    void drawZSpaceMesh(Renderer& renderer, zSpace::zObjectMesh& mesh, const zDisplayMeshSetting& display = zDisplayMeshSetting{});
    void drawZSpaceGraph(Renderer& renderer, zSpace::zObjectGraph& graph, const zDisplayGraphSetting& display = zDisplayGraphSetting{});
    void drawZSpacePointCloud(Renderer& renderer, zSpace::zObjectPointCloud& points, const zDisplayPointCloudSetting& display = zDisplayPointCloudSetting{});
#endif

} // namespace alice2

#endif // ALICE2_ZSPACE_DRAW_H

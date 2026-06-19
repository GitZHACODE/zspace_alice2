#include "zSpaceDraw.h"

#if ALICE2_WITH_ZSPACE_CORE

#include "../core/Renderer.h"
#include <zspace/interface.h>

#include <vector>

namespace alice2 {
namespace {

    Vec3 toVec3(const zSpace::zVector& p)
    {
        return Vec3(p.x, p.y, p.z);
    }

    void appendTriangle(std::vector<Vec3>& vertices, const Vec3& a, const Vec3& b, const Vec3& c)
    {
        vertices.push_back(a);
        vertices.push_back(b);
        vertices.push_back(c);
    }

} // namespace

    void drawZSpaceMesh(Renderer& renderer, zSpace::zObjectMesh& mesh, const zDisplayMeshSetting& display)
    {
        if (display.showFaces) {
            std::vector<Vec3> triangles;

            for (zSpace::zItMeshFace face(mesh); !face.end(); face++) {
                zSpace::zIntArray vertexIds;
                face.getVertices(vertexIds);
                if (vertexIds.size() < 3) continue;

                zSpace::zPointArray facePositions;
                face.getVertexPositions(facePositions);
                if (facePositions.size() < 3) continue;

                const Vec3 root = toVec3(facePositions[0]);
                for (size_t i = 1; i + 1 < facePositions.size(); ++i) {
                    appendTriangle(triangles, root, toVec3(facePositions[i]), toVec3(facePositions[i + 1]));
                }
            }

            if (!triangles.empty()) {
                std::vector<Color> colors(triangles.size(), display.faceColor);
                renderer.drawMesh(triangles.data(), nullptr, colors.data(), static_cast<int>(triangles.size()), nullptr, 0, false);
            }
        }

        if (display.showEdges) {
            for (zSpace::zItMeshEdge edge(mesh); !edge.end(); edge++) {
                zSpace::zPointArray edgePositions;
                edge.getVertexPositions(edgePositions);
                if (edgePositions.size() == 2) {
                    renderer.drawLine(toVec3(edgePositions[0]), toVec3(edgePositions[1]), display.edgeColor, display.edgeWidth);
                }
            }
        }

        if (display.showVertices) {
            for (zSpace::zItMeshVertex vertex(mesh); !vertex.end(); vertex++) {
                renderer.drawPoint(toVec3(vertex.getPosition()), display.vertexColor, display.vertexSize);
            }
        }
    }

    void drawZSpaceGraph(Renderer& renderer, zSpace::zObjectGraph& graph, const zDisplayGraphSetting& display)
    {
        if (display.showEdges) {
            for (zSpace::zItGraphEdge edge(graph); !edge.end(); edge++) {
                zSpace::zPointArray edgePositions;
                edge.getVertexPositions(edgePositions);
                if (edgePositions.size() == 2) {
                    renderer.drawLine(toVec3(edgePositions[0]), toVec3(edgePositions[1]), display.edgeColor, display.edgeWidth);
                }
            }
        }

        if (display.showVertices) {
            for (zSpace::zItGraphVertex vertex(graph); !vertex.end(); vertex++) {
                renderer.drawPoint(toVec3(vertex.getPosition()), display.vertexColor, display.vertexSize);
            }
        }
    }

    void drawZSpacePointCloud(Renderer& renderer, zSpace::zObjectPointCloud& points, const zDisplayPointCloudSetting& display)
    {
        for (zSpace::zItPointCloudVertex vertex(points); !vertex.end(); vertex++) {
            renderer.drawPoint(toVec3(vertex.getPosition()), display.vertexColor, display.vertexSize);
        }
    }

} // namespace alice2

#endif // ALICE2_WITH_ZSPACE_CORE

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

    Color toColor(const zSpace::zColor& c)
    {
        return Color(c.r, c.g, c.b, c.a);
    }

    void appendTriangle(std::vector<Vec3>& vertices, const Vec3& a, const Vec3& b, const Vec3& c)
    {
        vertices.push_back(a);
        vertices.push_back(b);
        vertices.push_back(c);
    }

    void appendTriangleColors(std::vector<Color>& colors, const Color& a, const Color& b, const Color& c)
    {
        colors.push_back(a);
        colors.push_back(b);
        colors.push_back(c);
    }

} // namespace

    void drawZSpaceMesh(Renderer& renderer, zSpace::zObjectMesh& mesh, const zDisplayMeshSetting& display)
    {
        if (display.showFaces) {
            std::vector<Vec3> triangles;
            std::vector<Color> colors;
            zSpace::zColorArray vertexColors;

            if (display.useMeshColors) {
                zSpace::zFnMesh fnMesh(mesh);
                fnMesh.getVertexColors(vertexColors);
            }

            for (zSpace::zItMeshFace face(mesh); !face.end(); face++) {
                zSpace::zIntArray vertexIds;
                face.getVertices(vertexIds);
                if (vertexIds.size() < 3) continue;

                zSpace::zPointArray facePositions;
                face.getVertexPositions(facePositions);
                if (facePositions.size() < 3) continue;

                const Vec3 root = toVec3(facePositions[0]);
                const Color rootColor = (display.useMeshColors && vertexIds[0] < static_cast<int>(vertexColors.size()))
                    ? toColor(vertexColors[vertexIds[0]])
                    : display.faceColor;

                for (size_t i = 1; i + 1 < facePositions.size(); ++i) {
                    appendTriangle(triangles, root, toVec3(facePositions[i]), toVec3(facePositions[i + 1]));
                    const Color colorA = (display.useMeshColors && vertexIds[i] < static_cast<int>(vertexColors.size()))
                        ? toColor(vertexColors[vertexIds[i]])
                        : display.faceColor;
                    const Color colorB = (display.useMeshColors && vertexIds[i + 1] < static_cast<int>(vertexColors.size()))
                        ? toColor(vertexColors[vertexIds[i + 1]])
                        : display.faceColor;
                    appendTriangleColors(colors, rootColor, colorA, colorB);
                }
            }

            if (!triangles.empty()) {
                if (colors.size() != triangles.size()) colors.assign(triangles.size(), display.faceColor);
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

#include "zUnroller.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <unordered_set>

using namespace zSpace;

namespace alice2 {

    namespace {
        zUtilsCore& coreUtils()
        {
            static zUtilsCore core;
            return core;
        }
    }

    bool loadMesh(const std::string& path, zObjMesh& mesh, std::string* message)
    {
        auto result = zIO::readMesh(path, mesh);
        if (message) *message = result ? "loaded" : result.message();
        return static_cast<bool>(result);
    }

    bool buildSections(zObjMesh& mesh, const zIntArray& medialIds, const zIntArray& featuredNumStrides,
        std::vector<zItMeshHalfEdgeArray>& loops, zObjMesh& topMesh, zObjMesh& bottomMesh,
        zScalarArray& scalars, zObjMeshArray& sectionMeshes,
        zObjGraphArray& sectionGraphs, zObjGraphArray& contourGraphs)
    {
        if (medialIds.size() < 2) return false;

        zIntArray medial = medialIds;
        zVector normal(0, 0, 1);
        computeVLoops(mesh, medial,  loops, topMesh, bottomMesh);
        computeGeodesicScalars(mesh, loops, scalars, true);
        computeGeodesicContours(loops, scalars, SlicingParameters::longitudeLayerSpacing, topMesh, bottomMesh, sectionMeshes);
        createSectionGraphs(sectionMeshes, sectionGraphs);
        computeSDF(sectionGraphs, sectionMeshes, contourGraphs);
        return true;
    }

    void getBoundaryOffset(zObjMesh& inMesh, bool keepExistingFaces, float offset, zObjMesh& outMesh)
    {
        zFnMesh fnMesh(inMesh);
        zPointArray positions;
        zIntArray polyCounts;
        zIntArray polyConnects;

        if (!keepExistingFaces) {
            fnMesh.getVertexPositions(positions);
            for (zItMeshVertex v(inMesh); !v.end(); v++) {
                if (v.onBoundary()) {
                    zVector normal = v.getNormal();
                    normal.normalize();
                    positions[v.getId()] = v.getPosition() + normal * offset;
                }
            }
            fnMesh.getPolygonData(polyConnects, polyCounts);
        }
        else {
            std::vector<zIntArray> boundaryMap;
            boundaryMap.assign(fnMesh.numVertices(), zIntArray());
            for (zItMeshVertex v(inMesh); !v.end(); v++) {
                if (!v.onBoundary()) continue;
                boundaryMap[v.getId()].push_back(static_cast<int>(positions.size()));
                positions.push_back(v.getPosition());

                zVector normal = v.getNormal();
                normal.normalize();
                boundaryMap[v.getId()].push_back(static_cast<int>(positions.size()));
                positions.push_back(v.getPosition() + normal * offset);
            }

            for (zItMeshHalfEdge he(inMesh); !he.end(); he++) {
                if (!he.onBoundary()) continue;
                zIntArray edgeVerts;
                he.getVertices(edgeVerts);
                if (edgeVerts.size() < 2) continue;
                if (boundaryMap[edgeVerts[0]].size() < 2 || boundaryMap[edgeVerts[1]].size() < 2) continue;

                polyConnects.push_back(boundaryMap[edgeVerts[0]][0]);
                polyConnects.push_back(boundaryMap[edgeVerts[1]][0]);
                polyConnects.push_back(boundaryMap[edgeVerts[1]][1]);
                polyConnects.push_back(boundaryMap[edgeVerts[0]][1]);
                polyCounts.push_back(4);
            }
        }

        zFnMesh outFn(outMesh);
        outFn.clear();
        if (!positions.empty()) outFn.create(positions, polyCounts, polyConnects);
    }

    void setPtGraph(zObjGraph& graph, zPoint& refPt, bool setX, bool setY, bool setZ)
    {
        zFnGraph fnGraph(graph);
        zPoint* positions = fnGraph.getRawVertexPositions();
        for (int i = 0; i < fnGraph.numVertices(); i++) {
            if (setX) positions[i].x = refPt.x;
            if (setY) positions[i].y = refPt.y;
            if (setZ) positions[i].z = refPt.z;
        }
    }

    void setPtMesh(zObjMesh& mesh, zPoint& refPt, bool setX, bool setY, bool setZ)
    {
        zFnMesh fnMesh(mesh);
        zPoint* positions = fnMesh.getRawVertexPositions();
        for (int i = 0; i < fnMesh.numVertices(); i++) {
            if (setX) positions[i].x = refPt.x;
            if (setY) positions[i].y = refPt.y;
            if (setZ) positions[i].z = refPt.z;
        }
    }

    void getFaceVerticesFromHalfedge(zItMeshHalfEdge& heStart, bool forward, zPointArray& faceVerts)
    {
        faceVerts.clear();
        zItMeshHalfEdge he = heStart;
        do {
            faceVerts.push_back(forward ? he.getVertex().getPosition() : he.getStartVertex().getPosition());
            he = forward ? he.getNext() : he.getPrev();
        } while (he != heStart);
    }

    void getFaceVerticesFromHalfedge(zItMeshHalfEdge& heStart, bool forward, zIntArray& faceVerts)
    {
        faceVerts.clear();
        zItMeshHalfEdge he = heStart;
        do {
            faceVerts.push_back(forward ? he.getVertex().getId() : he.getStartVertex().getId());
            he = forward ? he.getNext() : he.getPrev();
        } while (he != heStart);
    }

    void getLoop(zItMeshHalfEdge& heStart, bool forward, bool corner, int vCounter, std::vector<zItMeshHalfEdgeArray>& loops)
    {
        zItMeshHalfEdge heU = forward ? heStart.getNext() : heStart.getPrev();
        if (corner) heU = heStart;
        zItMeshHalfEdge heV = forward ? heU.getSym().getNext() : heU.getSym().getPrev();
        zItMeshHalfEdgeArray loop;
        for (int i = 0; i < vCounter; i++) {
            loop.push_back(forward ? heV.getSym() : heV);
            heV = forward ? heV.getNext().getSym().getNext() : heV.getPrev().getSym().getPrev();
        }
        loops.push_back(loop);
    }
    void colorMesh(zObjMesh& mesh, zFloatArray& scalars)
    {
        if (scalars.empty()) return;
        zFnMesh fnMesh(mesh);
        zColor* colors = fnMesh.getRawVertexColors();
        zDomainFloat scalarDomain(coreUtils().zMin(scalars), coreUtils().zMax(scalars));
        zDomainColor colorDomain(zColor(1, 0, 0, 1), zColor(0, 1, 0, 1));
        for (int i = 0; i < fnMesh.numVertices() && i < static_cast<int>(scalars.size()); i++) {
            colors[i] = coreUtils().blendColor(scalars[i], scalarDomain, colorDomain, zRGB);
        }
        fnMesh.computeFaceColorfromVertexColor();
    }

    zPoint getContourPosition(float threshold, zVector& vertexLower, zVector& vertexHigher, float thresholdLow, float thresholdHigh)
    {
        const float scale = coreUtils().ofMap(threshold, thresholdLow, thresholdHigh, 0.0f, 1.0f);
        zVector edge = vertexHigher - vertexLower;
        const double edgeLength = edge.length();
        edge.normalize();
        return vertexLower + edge * edgeLength * scale;
    }

    void getPokeMesh(zObjMesh& mesh, zObjMesh& triMesh)
    {
        zFnMesh fnMesh(mesh);
        zPointArray vertices;
        zPointArray centers;
        fnMesh.getVertexPositions(vertices);
        fnMesh.getCenters(zFaceData, centers);

        zPointArray positions = vertices;
        positions.insert(positions.end(), centers.begin(), centers.end());
        zIntArray counts;
        zIntArray connects;
        const int centerOffset = static_cast<int>(vertices.size());

        for (zItMeshFace f(mesh); !f.end(); f++) {
            zIntArray faceVerts;
            f.getVertices(faceVerts);
            for (int i = 0; i < static_cast<int>(faceVerts.size()); i++) {
                connects.push_back(faceVerts[i]);
                connects.push_back(faceVerts[(i + 1) % faceVerts.size()]);
                connects.push_back(centerOffset + f.getId());
                counts.push_back(3);
            }
        }

        zFnMesh fnTriMesh(triMesh);
        fnTriMesh.clear();
        fnTriMesh.create(positions, counts, connects);
    }

    void closestPointsToMesh(zPointArray& inPoints, zObjMesh mesh, zIntArray& faceIds, zPointArray& closestPoints)
    {
        faceIds.assign(inPoints.size(), 0);
        closestPoints.assign(inPoints.size(), zPoint());
        zPointArray faceCenters;
        zFnMesh fnMesh(mesh);
        fnMesh.getCenters(zFaceData, faceCenters);

        for (int i = 0; i < static_cast<int>(inPoints.size()); i++) {
            double bestDistance = std::numeric_limits<double>::max();
            int bestFace = 0;
            for (int f = 0; f < static_cast<int>(faceCenters.size()); f++) {
                zVector delta = inPoints[i] - faceCenters[f];
                const double distance = delta.length();
                if (distance < bestDistance) {
                    bestDistance = distance;
                    bestFace = f;
                }
            }
            faceIds[i] = bestFace;
            closestPoints[i] = faceCenters.empty() ? inPoints[i] : faceCenters[bestFace];
        }
    }

    void createBoundaryEdgeGraph(zObjMesh& mesh, bool closeGraph, zObjGraph& graph)
    {
        zPointArray positions;
        zIntArray edgeConnects;
        zColorArray colors;

        zItMeshHalfEdge he;
        bool foundBoundary = false;
        for (zItMeshHalfEdge tmpHE(mesh); !tmpHE.end(); tmpHE++) {
            if (tmpHE.onBoundary()) {
                he = tmpHE;
                foundBoundary = true;
                break;
            }
        }
        if (!foundBoundary) return;

        zItMeshHalfEdge startHe = he;
        positions.push_back(he.getStartVertex().getPosition());
        colors.push_back(he.getStartVertex().getColor());

        do {
            zPoint nextPosition = he.getVertex().getPosition();
            zVector closingDelta = nextPosition - positions[0];
            const bool returnsToStart = closingDelta.length() <= 1e-6;

            if (!returnsToStart) {
                positions.push_back(nextPosition);
                colors.push_back(he.getVertex().getColor());

                edgeConnects.push_back(static_cast<int>(positions.size()) - 2);
                edgeConnects.push_back(static_cast<int>(positions.size()) - 1);
            }

            he = he.getNext();
        } while (he != startHe);

        if (closeGraph && positions.size() > 1) {
            edgeConnects.push_back(static_cast<int>(positions.size()) - 1);
            edgeConnects.push_back(0);
        }

        zFnGraph fnGraph(graph);
        fnGraph.create(positions, edgeConnects);
        fnGraph.setVertexColors(colors);
    }

    void UVParametrisation(zObjMesh mesh, zObjMesh& paramMesh)
    {
        paramMesh = mesh;
        zFnMesh fnParam(paramMesh);
        zPointArray positions;
        fnParam.getVertexPositions(positions);
        if (positions.empty()) return;

        zPoint minBB;
        zPoint maxBB;
        fnParam.getBounds(minBB, maxBB);
        const double width = std::max(1e-6, static_cast<double>(maxBB.x - minBB.x));
        const double height = std::max(1e-6, static_cast<double>(maxBB.y - minBB.y));
        for (auto& p : positions) {
            p = zPoint((p.x - minBB.x) / width, (p.y - minBB.y) / height, 0);
        }
        fnParam.setVertexPositions(positions);
    }
    
    void getBaryCentricCoordinates_triangle(zPoint& pt, zPoint& t0, zPoint& t1, zPoint& t2, zPoint& baryCoordinates)
    {
        zVector v0 = t1 - t0;
        zVector v1 = t2 - t0;
        zVector v2 = pt - t0;
        const float d00 = v0 * v0;
        const float d01 = v0 * v1;
        const float d11 = v1 * v1;
        const float d20 = v2 * v0;
        const float d21 = v2 * v1;
        const float denom = d00 * d11 - d01 * d01;
        if (std::abs(denom) <= std::numeric_limits<float>::epsilon()) {
            baryCoordinates = zPoint(1, 0, 0);
            return;
        }
        const float v = (d11 * d20 - d01 * d21) / denom;
        const float w = (d00 * d21 - d01 * d20) / denom;
        baryCoordinates = zPoint(1.0f - v - w, v, w);
    }

    void getProjectionPoint_triangle(zPoint& baryCoordinates, zPoint& t0, zPoint& t1, zPoint& t2, zPoint& projectionPt)
    {
        projectionPt = t0 * baryCoordinates.x + t1 * baryCoordinates.y + t2 * baryCoordinates.z;
    }

    zPoint closestPointOnTriangle(zPoint p, zPoint a, zPoint b, zPoint c)
    {
        zVector ab = b - a;
        zVector ac = c - a;
        zVector ap = p - a;
        double d1 = ab * ap;
        double d2 = ac * ap;
        if (d1 <= 0.0 && d2 <= 0.0) return a;

        zVector bp = p - b;
        double d3 = ab * bp;
        double d4 = ac * bp;
        if (d3 >= 0.0 && d4 <= d3) return b;

        double vc = d1 * d4 - d3 * d2;
        if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
            double v = d1 / (d1 - d3);
            return a + (ab * v);
        }

        zVector cp = p - c;
        double d5 = ab * cp;
        double d6 = ac * cp;
        if (d6 >= 0.0 && d5 <= d6) return c;

        double vb = d5 * d2 - d1 * d6;
        if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
            double w = d2 / (d2 - d6);
            return a + (ac * w);
        }

        double va = d3 * d6 - d5 * d4;
        if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) {
            zVector bc = c - b;
            double w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
            return b + (bc * w);
        }

        double denom = 1.0 / (va + vb + vc);
        double v = vb * denom;
        double w = vc * denom;
        return a + (ab * v) + (ac * w);
    }

    int snapGraphVerticesToClosestMesh(zObjGraph& graph, zObjMesh& mesh, double tolerance)
    {
        zFnGraph fnGraph(graph);
        zPointArray positions;
        fnGraph.getVertexPositions(positions);
        if (positions.empty()) return 0;

        int movedCount = 0;
        for (int graphVertexId = 0; graphVertexId < static_cast<int>(positions.size()); graphVertexId++) {
            zPoint p = positions[graphVertexId];
            zPoint closest = p;
            double closestDistance2 = std::numeric_limits<double>::max();

            for (zItMeshFace f(mesh); !f.end(); f++) {
                zPointArray faceVerts;
                f.getVertexPositions(faceVerts);
                if (faceVerts.size() < 3) continue;
                for (int tri = 1; tri < static_cast<int>(faceVerts.size()) - 1; tri++) {
                    zPoint candidate = closestPointOnTriangle(p, faceVerts[0], faceVerts[tri], faceVerts[tri + 1]);
                    zVector d = candidate - p;
                    double distance2 = d * d;
                    if (distance2 < closestDistance2) {
                        closestDistance2 = distance2;
                        closest = candidate;
                    }
                }
            }

            if (closestDistance2 < std::numeric_limits<double>::max() && closestDistance2 > tolerance * tolerance) {
                positions[graphVertexId] = closest;
                movedCount++;
            }
        }

        if (movedCount > 0) fnGraph.setVertexPositions(positions);
        return movedCount;
    }

    bool barycentericProjection_triMesh(zObjGraph& graph, zObjMesh& inMesh, zObjMesh& projectionMesh, zVectorArray* mappedNormals)
    {
        zFnGraph fnGraph(graph);
        zPointArray positions;
        fnGraph.getVertexPositions(positions);
        if (mappedNormals) mappedNormals->assign(positions.size(), zVector(0, 0, 1));

        int missedProjectionCount = 0;
        for (int graphVertexId = 0; graphVertexId < static_cast<int>(positions.size()); graphVertexId++) {
            zPoint& p = positions[graphVertexId];
            bool projected = false;
            int nearestFaceId = -1;
            double nearestFaceDistance = std::numeric_limits<double>::max();

            for (zItMeshFace f(inMesh); !f.end(); f++) {
                zPointArray faceVerts;
                f.getVertexPositions(faceVerts);
                if (faceVerts.size() < 3) continue;

                zItMeshFace projectionFace(projectionMesh, f.getId());
                zPointArray projectionVerts;
                projectionFace.getVertexPositions(projectionVerts);
                if (projectionVerts.size() != faceVerts.size() || projectionVerts.size() < 3) continue;

                for (auto& facePt : faceVerts) {
                    zVector d = p - facePt;
                    const double distance = d.length();
                    if (distance < nearestFaceDistance) {
                        nearestFaceDistance = distance;
                        nearestFaceId = f.getId();
                    }
                }

                for (int tri = 1; tri < static_cast<int>(faceVerts.size()) - 1; tri++) {
                    zPoint bary;
                    getBaryCentricCoordinates_triangle(p, faceVerts[0], faceVerts[tri], faceVerts[tri + 1], bary);

                    const double tolerance = 1e-4;
                    const bool isInside =
                        bary.x >= -tolerance && bary.y >= -tolerance && bary.z >= -tolerance &&
                        bary.x <= 1.0 + tolerance && bary.y <= 1.0 + tolerance && bary.z <= 1.0 + tolerance;
                    if (!isInside) continue;

                    getProjectionPoint_triangle(bary, projectionVerts[0], projectionVerts[tri], projectionVerts[tri + 1], p);
                    if (mappedNormals) {
                        zItMeshFace mappedFace(projectionMesh, f.getId());
                        zVector n = mappedFace.getNormal();
                        if (n.length() > 1e-6) n.normalize();
                        (*mappedNormals)[graphVertexId] = n;
                    }
                    projected = true;
                    break;
                }

                if (projected) break;
            }

            if (!projected) {
                std::cout << "[barycentericProjection_triMesh] failed vertex " << graphVertexId
                    << " p=(" << p.x << "," << p.y << "," << p.z << ")"
                    << " nearestFace=" << nearestFaceId
                    << " nearestFaceVertexDistance=" << nearestFaceDistance
                    << std::endl;
                missedProjectionCount++;
            }
        }

        if (missedProjectionCount > 0) {
            zFnGraph failGraph(graph);
            zPoint minBB, maxBB;
            failGraph.getBounds(minBB, maxBB);
            zFnMesh fnInMesh(inMesh);
            zPoint meshMinBB, meshMaxBB;
            fnInMesh.getBounds(meshMinBB, meshMaxBB);
            std::cout << "[barycentericProjection_triMesh] graph bounds min=("
                << minBB.x << "," << minBB.y << "," << minBB.z << ") max=("
                << maxBB.x << "," << maxBB.y << "," << maxBB.z << ")"
                << " mesh bounds min=(" << meshMinBB.x << "," << meshMinBB.y << "," << meshMinBB.z << ") max=("
                << meshMaxBB.x << "," << meshMaxBB.y << "," << meshMaxBB.z << ")"
                << std::endl;
            std::cout << "[barycentericProjection_triMesh] ERROR failed to project "
                << missedProjectionCount << " graph vertices." << std::endl;
            return false;
        }

        fnGraph.setVertexPositions(positions);
        return true;
    }

    bool barycentericProjection_triMesh(zObjGraph& graph, zObjMesh& inMesh, zObjMesh& projectionMesh)
    {
        return barycentericProjection_triMesh(graph, inMesh, projectionMesh, nullptr);
    }

    void printGraphSDFDebug(const char* label, int sectionId, zObjGraph& graph, const zDomain<zPoint>& fieldBB)
    {
        zFnGraph fnGraph(graph);
        zPointArray positions;
        zIntArray edgeConnects;
        fnGraph.getVertexPositions(positions);
        fnGraph.getEdgeData(edgeConnects);

        double minEdgeLength = std::numeric_limits<double>::max();
        double maxEdgeLength = 0.0;
        int zeroLengthEdges = 0;
        int nonFiniteVertices = 0;
        int outOfFieldVertices = 0;

        for (auto& p : positions) {
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) nonFiniteVertices++;
            if (p.x < fieldBB.min.x || p.x > fieldBB.max.x || p.y < fieldBB.min.y || p.y > fieldBB.max.y) outOfFieldVertices++;
        }

        for (int e = 0; e + 1 < static_cast<int>(edgeConnects.size()); e += 2) {
            const int a = edgeConnects[e];
            const int b = edgeConnects[e + 1];
            if (a < 0 || b < 0 || a >= static_cast<int>(positions.size()) || b >= static_cast<int>(positions.size())) continue;

            zVector edge = positions[b] - positions[a];
            const double length = edge.length();
            minEdgeLength = std::min(minEdgeLength, length);
            maxEdgeLength = std::max(maxEdgeLength, length);
            if (length <= 1e-6) zeroLengthEdges++;
        }

        zPoint minBB, maxBB;
        fnGraph.getBounds(minBB, maxBB);
        std::cout << "[computeSDF] section " << sectionId << " " << label
            << " graph vertices=" << fnGraph.numVertices()
            << " edges=" << fnGraph.numEdges()
            << " bounds min=(" << minBB.x << "," << minBB.y << "," << minBB.z << ")"
            << " max=(" << maxBB.x << "," << maxBB.y << "," << maxBB.z << ")"
            << " minEdge=" << minEdgeLength
            << " maxEdge=" << maxEdgeLength
            << " zeroEdges=" << zeroLengthEdges
            << " nonFiniteVertices=" << nonFiniteVertices
            << " outOfFieldVertices=" << outOfFieldVertices
            << std::endl;
    }

    zPoint sampleHalfEdgeLoopNormalised(zItMeshHalfEdgeArray& loop, float t)
    {
        zPointArray points;
        if (loop.empty()) return zPoint();

        points.push_back(loop.front().getStartVertex().getPosition());
        for (auto& he : loop) points.push_back(he.getVertex().getPosition());
        if (points.size() == 1) return points.front();

        zFloatArray lengths;
        lengths.assign(points.size() - 1, 0.0f);
        float totalLength = 0.0f;
        for (int i = 0; i + 1 < static_cast<int>(points.size()); i++) {
            lengths[i] = (points[i + 1] - points[i]).length();
            totalLength += lengths[i];
        }
        if (totalLength <= 1e-6f) return points.front();

        const float target = std::max(0.0f, std::min(1.0f, t)) * totalLength;
        float accumulated = 0.0f;
        for (int i = 0; i < static_cast<int>(lengths.size()); i++) {
            if (accumulated + lengths[i] >= target) {
                const float localT = (lengths[i] <= 1e-6f) ? 0.0f : (target - accumulated) / lengths[i];
                return points[i] + ((points[i + 1] - points[i]) * localT);
            }
            accumulated += lengths[i];
        }

        return points.back();
    }

    zPoint sampleGraphPolylineNormalised(zObjGraph& graph, float t)
    {
        zFnGraph fnGraph(graph);
        zPointArray positions;
        zIntArray edgeConnects;
        fnGraph.getVertexPositions(positions);
        fnGraph.getEdgeData(edgeConnects);
        if (positions.empty()) return zPoint();
        if (edgeConnects.size() < 2) return positions.front();

        zFloatArray lengths;
        lengths.assign(edgeConnects.size() / 2, 0.0f);
        float totalLength = 0.0f;
        for (int e = 0; e + 1 < static_cast<int>(edgeConnects.size()); e += 2) {
            const int a = edgeConnects[e];
            const int b = edgeConnects[e + 1];
            if (a < 0 || b < 0 || a >= static_cast<int>(positions.size()) || b >= static_cast<int>(positions.size())) continue;
            const float length = (positions[b] - positions[a]).length();
            lengths[e / 2] = length;
            totalLength += length;
        }
        if (totalLength <= 1e-6f) return positions.front();

        const float target = std::max(0.0f, std::min(1.0f, t)) * totalLength;
        float accumulated = 0.0f;
        for (int e = 0; e + 1 < static_cast<int>(edgeConnects.size()); e += 2) {
            const float length = lengths[e / 2];
            const int a = edgeConnects[e];
            const int b = edgeConnects[e + 1];
            if (a < 0 || b < 0 || a >= static_cast<int>(positions.size()) || b >= static_cast<int>(positions.size())) continue;
            if (accumulated + length >= target) {
                const float localT = (length <= 1e-6f) ? 0.0f : (target - accumulated) / length;
                return positions[a] + ((positions[b] - positions[a]) * localT);
            }
            accumulated += length;
        }

        return positions[edgeConnects.back()];
    }

    void createPerpendicularTrimSlots(zObjGraph& sourceGraph, zObjGraph& outGraph, bool alternate, float trimLength, int maxEdges = -1)
    {
        zFnGraph fnSource(sourceGraph);
        if (fnSource.numVertices() == 0 || fnSource.numEdges() == 0) {
            zFnGraph fnOut(outGraph);
            fnOut.clear();
            return;
        }

        zPointArray sourcePositions;
        zIntArray sourceEdges;
        fnSource.getVertexPositions(sourcePositions);
        fnSource.getEdgeData(sourceEdges);

        zPointArray trimPositions;
        zIntArray trimEdges;
        const float t = alternate ? SlicingParameters::trimSlotStaggerEven : SlicingParameters::trimSlotStaggerOdd;
        const int edgeLimit = (maxEdges < 0) ? (int)(sourceEdges.size() / 2) : std::min(maxEdges, (int)(sourceEdges.size() / 2));

        for (int i = 0; i < edgeLimit; i++) {
            const int a = sourceEdges[i * 2];
            const int b = sourceEdges[(i * 2) + 1];
            if (a < 0 || b < 0 || a >= static_cast<int>(sourcePositions.size()) || b >= static_cast<int>(sourcePositions.size())) continue;

            zVector dir = sourcePositions[b] - sourcePositions[a];
            if (dir.length() <= 0.0001f) continue;
            dir.normalize();

            zVector perp(-dir.y, dir.x, 0.0f);
            if (perp.length() <= 0.0001f) continue;
            perp.normalize();

            zPoint mid = sourcePositions[a] + ((sourcePositions[b] - sourcePositions[a]) * t);
            const int id = static_cast<int>(trimPositions.size());
            trimPositions.push_back(mid + (perp * trimLength));
            trimPositions.push_back(mid - (perp * trimLength));
            trimEdges.push_back(id);
            trimEdges.push_back(id + 1);
        }

        zFnGraph fnOut(outGraph);
        fnOut.clear();
        if (!trimPositions.empty()) fnOut.create(trimPositions, trimEdges);
        fnOut.setEdgeColor(zBLUE);
        fnOut.setEdgeWeight(3);
    }

    void combineGraphObjects(const zObjGraphArray& graphs, zObjGraph& outGraph)
    {
        zPointArray positions;
        zIntArray edgeConnects;

        for (auto& graph : graphs) {
            zFnGraph fnGraph(const_cast<zObjGraph&>(graph));
            zPointArray graphPositions;
            zIntArray graphEdges;
            fnGraph.getVertexPositions(graphPositions);
            fnGraph.getEdgeData(graphEdges);
            const int offset = static_cast<int>(positions.size());
            positions.insert(positions.end(), graphPositions.begin(), graphPositions.end());
            for (int id : graphEdges) edgeConnects.push_back(id + offset);
        }

        zFnGraph fnOut(outGraph);
        fnOut.clear();
        if (!positions.empty()) fnOut.create(positions, edgeConnects);
    }

    void createGraphFromOrderedVertexIds(zObjGraph& graph, const zPointArray& sourcePositions, const zIntArray& sequence, bool closeGraph)
    {
        zFnGraph fnGraph(graph);
        fnGraph.clear();
        if (sequence.size() < 2) return;

        zPointArray positions;
        zIntArray edgeConnects;
        positions.reserve(sequence.size());
        for (int id : sequence) {
            if (id < 0 || id >= static_cast<int>(sourcePositions.size())) continue;
            positions.push_back(sourcePositions[id]);
        }
        if (positions.size() < 2) return;

        for (int i = 0; i + 1 < static_cast<int>(positions.size()); i++) {
            edgeConnects.push_back(i);
            edgeConnects.push_back(i + 1);
        }
        if (closeGraph && positions.size() > 2) {
            edgeConnects.push_back(static_cast<int>(positions.size()) - 1);
            edgeConnects.push_back(0);
        }
        fnGraph.create(positions, edgeConnects);
    }

    void mergeContourGraphOpenVertices(zObjGraph& graph, double tolerance)
    {
        zFnGraph fnGraph(graph);
        zPointArray positions;
        zIntArray edgeConnects;
        fnGraph.getVertexPositions(positions);
        fnGraph.getEdgeData(edgeConnects);
        if (positions.empty() || edgeConnects.empty()) return;

        std::vector<zIntArray> adjacency(positions.size(), zIntArray());
        for (int e = 0; e + 1 < static_cast<int>(edgeConnects.size()); e += 2) {
            const int a = edgeConnects[e];
            const int b = edgeConnects[e + 1];
            if (a < 0 || b < 0 || a >= static_cast<int>(positions.size()) || b >= static_cast<int>(positions.size())) continue;
            adjacency[a].push_back(b);
            adjacency[b].push_back(a);
        }

        std::vector<int> remap(positions.size(), -1);
        zPointArray newPositions;
        for (int i = 0; i < static_cast<int>(positions.size()); i++) {
            if (remap[i] != -1) continue;
            remap[i] = static_cast<int>(newPositions.size());
            zPoint merged = positions[i];
            int mergedCount = 1;

            if (adjacency[i].size() <= 1) {
                for (int j = i + 1; j < static_cast<int>(positions.size()); j++) {
                    if (remap[j] != -1 || adjacency[j].size() > 1) continue;
                    if (positions[i].distanceTo(positions[j]) >= tolerance) continue;
                    remap[j] = remap[i];
                    merged += positions[j];
                    mergedCount++;
                }
            }

            newPositions.push_back(merged * (1.0 / static_cast<double>(mergedCount)));
        }

        zIntArray newEdges;
        std::unordered_set<unsigned long long> edgeKeys;
        for (int e = 0; e + 1 < static_cast<int>(edgeConnects.size()); e += 2) {
            int a = edgeConnects[e];
            int b = edgeConnects[e + 1];
            if (a < 0 || b < 0 || a >= static_cast<int>(remap.size()) || b >= static_cast<int>(remap.size())) continue;
            a = remap[a];
            b = remap[b];
            if (a == b) continue;
            const int lo = std::min(a, b);
            const int hi = std::max(a, b);
            const unsigned long long key = (static_cast<unsigned long long>(lo) << 32) | static_cast<unsigned int>(hi);
            if (!edgeKeys.insert(key).second) continue;
            newEdges.push_back(a);
            newEdges.push_back(b);
        }

        if (newPositions.size() != positions.size() || newEdges.size() != edgeConnects.size()) {
            std::cout << "[cleanContourGraph] merged open vertices oldV=" << positions.size()
                << " newV=" << newPositions.size()
                << " oldE=" << (edgeConnects.size() / 2)
                << " newE=" << (newEdges.size() / 2)
                << std::endl;
            fnGraph.create(newPositions, newEdges);
        }
    }

    bool buildLongestContourCycle(zObjGraph& graph, zIntArray& bestSequence, bool& closed, double endpointCloseTolerance)
    {
        zFnGraph fnGraph(graph);
        zPointArray positions;
        zIntArray edgeConnects;
        fnGraph.getVertexPositions(positions);
        fnGraph.getEdgeData(edgeConnects);
        bestSequence.clear();
        closed = false;
        if (positions.size() < 3 || edgeConnects.size() < 4) return false;

        std::vector<zIntArray> adjacency(positions.size(), zIntArray());
        std::unordered_set<unsigned long long> edgeKeys;
        for (int e = 0; e + 1 < static_cast<int>(edgeConnects.size()); e += 2) {
            const int a = edgeConnects[e];
            const int b = edgeConnects[e + 1];
            if (a < 0 || b < 0 || a >= static_cast<int>(positions.size()) || b >= static_cast<int>(positions.size())) continue;
            if ((positions[b] - positions[a]).length() < 1e-6) continue;
            const int lo = std::min(a, b);
            const int hi = std::max(a, b);
            const unsigned long long key = (static_cast<unsigned long long>(lo) << 32) | static_cast<unsigned int>(hi);
            if (!edgeKeys.insert(key).second) continue;
            adjacency[a].push_back(b);
            adjacency[b].push_back(a);
        }

        zIntArray endpoints;
        int degreeMoreCount = 0;
        for (int i = 0; i < static_cast<int>(adjacency.size()); i++) {
            if (adjacency[i].size() == 1) endpoints.push_back(i);
            else if (adjacency[i].size() > 2) degreeMoreCount++;
        }

        if (endpoints.size() == 2 && positions[endpoints[0]].distanceTo(positions[endpoints[1]]) <= endpointCloseTolerance) {
            adjacency[endpoints[0]].push_back(endpoints[1]);
            adjacency[endpoints[1]].push_back(endpoints[0]);
            edgeKeys.insert((static_cast<unsigned long long>(std::min(endpoints[0], endpoints[1])) << 32)
                | static_cast<unsigned int>(std::max(endpoints[0], endpoints[1])));
        }

        auto directedKey = [](int a, int b) -> unsigned long long {
            return (static_cast<unsigned long long>(a) << 32) | static_cast<unsigned int>(b);
        };

        auto turnAngle = [&](int previous, int current, int next) -> double {
            zVector in = positions[current] - positions[previous];
            zVector out = positions[next] - positions[current];
            if (in.length() < 1e-6 || out.length() < 1e-6) return 10.0;
            in.normalize();
            out.normalize();
            double cross = (in.x * out.y) - (in.y * out.x);
            double dot = (in.x * out.x) + (in.y * out.y);
            double angle = atan2(cross, dot);
            if (angle <= 0.0) angle += 3.14159265358979323846 * 2.0;
            return angle;
        };

        std::unordered_set<unsigned long long> visitedDirectedEdges;
        double bestPerimeter = -1.0;

        for (int start = 0; start < static_cast<int>(adjacency.size()); start++) {
            for (int startNext : adjacency[start]) {
                const unsigned long long startKey = directedKey(start, startNext);
                if (visitedDirectedEdges.count(startKey)) continue;

                zIntArray sequence;
                int previous = start;
                int current = startNext;
                sequence.push_back(start);
                std::unordered_set<int> sequenceVertices;
                sequenceVertices.insert(start);
                bool isClosed = false;

                for (int safety = 0; safety < static_cast<int>(edgeKeys.size()) * 2 + 4; safety++) {
                    visitedDirectedEdges.insert(directedKey(previous, current));

                    if (current == start) {
                        isClosed = true;
                        break;
                    }
                    if (sequenceVertices.count(current)) break;

                    sequence.push_back(current);
                    sequenceVertices.insert(current);

                    int next = -1;
                    double bestTurn = std::numeric_limits<double>::max();
                    for (int candidate : adjacency[current]) {
                        if (candidate == previous && adjacency[current].size() > 1) continue;
                        if (candidate != start && sequenceVertices.count(candidate)) continue;
                        const double angle = turnAngle(previous, current, candidate);
                        if (angle < bestTurn) {
                            bestTurn = angle;
                            next = candidate;
                        }
                    }
                    if (next < 0) break;

                    previous = current;
                    current = next;
                }

                if (!isClosed || sequence.size() < 3) continue;
                if (sequence.size() < 3) continue;

                double perimeter = 0.0;
                for (int i = 0; i < static_cast<int>(sequence.size()); i++) {
                    const int a = sequence[i];
                    const int b = sequence[(i + 1) % sequence.size()];
                    perimeter += (positions[b] - positions[a]).length();
                }
                if (perimeter > bestPerimeter) {
                    bestPerimeter = perimeter;
                    bestSequence = sequence;
                    closed = true;
                }
            }
        }

        return !bestSequence.empty();
    }

    void cleanContourGraphForToolpath(int graphId, zObjGraph& graph, double mergeTolerance)
    {
        zFnGraph fnGraph(graph);
        if (fnGraph.numVertices() == 0 || fnGraph.numEdges() == 0) return;

        mergeContourGraphOpenVertices(graph, mergeTolerance);
        fnGraph = zFnGraph(graph);

        zPointArray positions;
        zIntArray edgeConnects;
        fnGraph.getVertexPositions(positions);
        fnGraph.getEdgeData(edgeConnects);

        std::vector<zIntArray> adjacency(positions.size(), zIntArray());
        int zeroLengthEdges = 0;
        for (int e = 0; e + 1 < static_cast<int>(edgeConnects.size()); e += 2) {
            const int a = edgeConnects[e];
            const int b = edgeConnects[e + 1];
            if (a < 0 || b < 0 || a >= static_cast<int>(positions.size()) || b >= static_cast<int>(positions.size())) continue;
            if ((positions[b] - positions[a]).length() < 1e-6) {
                zeroLengthEdges++;
                continue;
            }
            adjacency[a].push_back(b);
            adjacency[b].push_back(a);
        }

        zIntArray endpoints;
        int degreeTwoCount = 0;
        int degreeMoreCount = 0;
        for (int i = 0; i < static_cast<int>(adjacency.size()); i++) {
            if (adjacency[i].size() == 1) endpoints.push_back(i);
            else if (adjacency[i].size() == 2) degreeTwoCount++;
            else if (adjacency[i].size() > 2) degreeMoreCount++;
        }

        const double endpointCloseTolerance = std::max(
            mergeTolerance * SlicingParameters::contourEndpointCloseMultiplier,
            SlicingParameters::contourEndpointCloseMinTolerance
        );
        if (endpoints.size() == 2 && degreeMoreCount == 0) {
            const double endpointDistance = positions[endpoints[0]].distanceTo(positions[endpoints[1]]);
            if (endpointDistance <= endpointCloseTolerance) {
                edgeConnects.push_back(endpoints[1]);
                edgeConnects.push_back(endpoints[0]);
                fnGraph.create(positions, edgeConnects);
                std::cout << "[cleanContourGraph] graph " << graphId
                    << " closed two contour endpoints"
                    << " endpointDistance=" << endpointDistance
                    << " vertices=" << positions.size()
                    << " edges=" << (edgeConnects.size() / 2)
                    << std::endl;
                return;
            }
            std::cout << "[cleanContourGraph] graph " << graphId
                << " not closing two endpoints; gap too large"
                << " endpointDistance=" << endpointDistance
                << " tolerance=" << endpointCloseTolerance
                << std::endl;
        }

        if (endpoints.empty() && degreeMoreCount == 0 && zeroLengthEdges == 0 && degreeTwoCount == static_cast<int>(positions.size())) return;

        zIntArray sequence;
        bool closed = false;
        if (buildLongestContourCycle(graph, sequence, closed, endpointCloseTolerance) && closed) {
            createGraphFromOrderedVertexIds(graph, positions, sequence, true);
            std::cout << "[cleanContourGraph] graph " << graphId
                << " rebuilt longest contour loop"
                << " oldV=" << positions.size()
                << " oldE=" << (edgeConnects.size() / 2)
                << " newV=" << sequence.size()
                << " endpoints=" << endpoints.size()
                << " degreeMore=" << degreeMoreCount
                << " zeroEdges=" << zeroLengthEdges
                << std::endl;
        }
        else {
            std::cout << "[cleanContourGraph] graph " << graphId
                << " could not rebuild closed contour"
                << " vertices=" << positions.size()
                << " edges=" << (edgeConnects.size() / 2)
                << " endpoints=" << endpoints.size()
                << " degreeMore=" << degreeMoreCount
                << " zeroEdges=" << zeroLengthEdges
                << std::endl;
        }
    }

    void makeSingleEdgeGraph(zObjGraph& sourceGraph, int edgeId, zObjGraph& outGraph)
    {
        zFnGraph fnSource(sourceGraph);
        zPointArray positions;
        zIntArray edgeConnects;
        fnSource.getVertexPositions(positions);
        fnSource.getEdgeData(edgeConnects);
        zFnGraph fnOut(outGraph);
        fnOut.clear();
        const int e = edgeId * 2;
        if (e + 1 >= static_cast<int>(edgeConnects.size())) return;
        const int a = edgeConnects[e];
        const int b = edgeConnects[e + 1];
        if (a < 0 || b < 0 || a >= static_cast<int>(positions.size()) || b >= static_cast<int>(positions.size())) return;
        zPointArray outPositions = { positions[a], positions[b] };
        zIntArray outEdges = { 0, 1 };
        fnOut.create(outPositions, outEdges);
    }

    bool makeFirstCornerEdgeGraph(zObjGraph& sourceGraph, zObjGraph& outGraph)
    {
        zFnGraph fnSource(sourceGraph);
        zPointArray positions;
        zColorArray colors;
        zIntArray edgeConnects;
        fnSource.getVertexPositions(positions);
        fnSource.getVertexColors(colors);
        fnSource.getEdgeData(edgeConnects);

        auto isCornerColor = [](const zColor& color) {
            return color.r > 0.8 && color.g > 0.2 && color.g < 0.75 && color.b < 0.2;
        };

        zFnGraph fnOut(outGraph);
        fnOut.clear();
        for (int e = 0; e + 1 < static_cast<int>(edgeConnects.size()); e += 2) {
            const int a = edgeConnects[e];
            const int b = edgeConnects[e + 1];
            if (a < 0 || b < 0 || a >= static_cast<int>(positions.size()) || b >= static_cast<int>(positions.size())) continue;
            if (a >= static_cast<int>(colors.size()) || b >= static_cast<int>(colors.size())) continue;
            if (!isCornerColor(colors[a]) || !isCornerColor(colors[b])) continue;

            zPointArray outPositions = { positions[a], positions[b] };
            zIntArray outEdges = { 0, 1 };
            fnOut.create(outPositions, outEdges);
            return true;
        }

        return false;
    }

    void colorSDFFieldFromValues(zObjMeshScalarField& field, const zScalarArray& values, double threshold)
    {
        zFnMesh fnMesh(field);
        zColor* colors = fnMesh.getRawVertexColors();
        if (!colors) return;

        double minNeg = std::numeric_limits<double>::max();
        for (double value : values) {
            if (value < 0.0 && std::isfinite(value)) minNeg = std::min(minNeg, value);
        }
        if (minNeg == std::numeric_limits<double>::max()) minNeg = -1.0;

        const zColor darkBlue(0.0, 40.0 / 255.0, 240.0 / 255.0, 1.0);
        const zColor lightBlue(180.0 / 255.0, 200.0 / 255.0, 1.0, 1.0);
        const zColor magenta(240.0 / 255.0, 0.0, 140.0 / 255.0, 1.0);
        const zColor gray(220.0 / 255.0, 220.0 / 255.0, 220.0 / 255.0, 1.0);

        const int count = std::min(fnMesh.numVertices(), static_cast<int>(values.size()));
        for (int i = 0; i < count; i++) {
            const double value = values[i];
            if (!std::isfinite(value)) {
                colors[i] = gray;
                continue;
            }

            if (value > -threshold && value < threshold) {
                colors[i] = magenta;
            }
            else if (value < -threshold) {
                double t = (value - (-threshold)) / (minNeg - (-threshold));
                t = std::max(0.0, std::min(1.0, t));
                colors[i] = zColor(
                    darkBlue.r + (lightBlue.r - darkBlue.r) * t,
                    darkBlue.g + (lightBlue.g - darkBlue.g) * t,
                    darkBlue.b + (lightBlue.b - darkBlue.b) * t,
                    1.0);
            }
            else {
                colors[i] = gray;
            }
        }
    }

    void computeDualGraph_BST(zObjMesh& mesh, zObjGraph& graph, zItGraphVertexArray& bsfVertices, zIntPairArray& bsfVertexPairs)
    {
        zFnMesh fnMesh(mesh);
        zIntArray inEdgeDualEdge;
        zIntArray dualEdgeInEdge;
        fnMesh.getDualGraph(graph, inEdgeDualEdge, dualEdgeInEdge, true, false, false);

        int maxValence = -1;
        zItGraphVertex maxVertex;
        for (zItGraphVertex v(graph); !v.end(); v++) {
            if (v.getValence() > maxValence) {
                maxValence = v.getValence();
                maxVertex = v;
            }
        }
        if (maxValence >= 0) maxVertex.getBSF(bsfVertices, bsfVertexPairs);
    }

    zIntPair getCommonEdge(zItMeshFace& f1, zItMeshFace& f2)
    {
        zItMeshHalfEdgeArray f1HalfEdges;
        zItMeshHalfEdgeArray f2HalfEdges;
        f1.getHalfEdges(f1HalfEdges);
        f2.getHalfEdges(f2HalfEdges);
        for (auto& he1 : f1HalfEdges) {
            for (auto& he2 : f2HalfEdges) {
                if (he1.getEdge().getId() == he2.getEdge().getId()) return zIntPair(he1.getId(), he2.getId());
            }
        }
        return zIntPair(-1, -1);
    }

    void creatUnrollMesh(zObjMesh& mesh, zObjMesh& unrollMeshObj, zObjGraph& dualGraph, zInt2DArray& oriVertexUnrollVertexMap, std::unordered_map<zIntPair, int, zPairHash>& oriFaceVertexUnrollVertex, zItGraphVertexArray& bsfVertices, zIntPairArray& bsfVertexPairs)
    {
        zFnMesh fnMesh(mesh);
        computeDualGraph_BST(mesh, dualGraph, bsfVertices, bsfVertexPairs);

        zPoint* vertexPositions = fnMesh.getRawVertexPositions();
        zPointArray positions;
        zIntArray counts;
        zIntArray connects;
        oriVertexUnrollVertexMap.assign(fnMesh.numVertices(), zIntArray());
        oriFaceVertexUnrollVertex.clear();

        for (zItMeshFace f(mesh); !f.end(); f++) {
            zIntArray faceVerts;
            f.getVertices(faceVerts);
            for (int vertexId : faceVerts) {
                const int newId = static_cast<int>(positions.size());
                connects.push_back(newId);
                oriVertexUnrollVertexMap[vertexId].push_back(newId);
                oriFaceVertexUnrollVertex[zIntPair(f.getId(), vertexId)] = newId;
                positions.push_back(vertexPositions[vertexId]);
            }
            counts.push_back(static_cast<int>(faceVerts.size()));
        }

        zFnMesh fnUnroll(unrollMeshObj);
        fnUnroll.clear();
        fnUnroll.create(positions, counts, connects);
    }

    void unrollMesh(zObjMesh&, zObjMesh& unrollMeshObj, zObjGraph&, zInt2DArray&, std::unordered_map<zIntPair, int, zPairHash>&, zIntPairArray&)
    {
        zFnMesh fnUnroll(unrollMeshObj);
        zPointArray positions;
        fnUnroll.getVertexPositions(positions);
        if (positions.empty()) return;

        zPoint minBB;
        zPoint maxBB;
        fnUnroll.getBounds(minBB, maxBB);
        const double width = std::max(1e-6, static_cast<double>(maxBB.x - minBB.x));
        const double height = std::max(1e-6, static_cast<double>(maxBB.y - minBB.y));
        for (auto& p : positions) {
            p.x = (p.x - minBB.x) / width;
            p.y = (p.y - minBB.y) / height;
            p.z = 0;
        }
        fnUnroll.setVertexPositions(positions);
    }

    void mergeMesh(zObjMesh& mesh)
    {
        zPointArray positions;
        zIntArray counts;
        zIntArray connects;

        for (zItMeshFace f(mesh); !f.end(); f++) {
            zPointArray facePositions;
            f.getVertexPositions(facePositions);
            for (auto& p : facePositions) {
                int id = -1;
                if (!coreUtils().checkRepeatVector(p, positions, id)) {
                    id = static_cast<int>(positions.size());
                    positions.push_back(p);
                }
                connects.push_back(id);
            }
            counts.push_back(static_cast<int>(facePositions.size()));
        }

        zFnMesh fnMesh(mesh);
        fnMesh.clear();
        if (!positions.empty()) fnMesh.create(positions, counts, connects);
    }

    void createShapes(zObjMesh& mesh, zIntArray& medialIds, zIntArray& featuredNumStrides, zVector& norm, float, int& numFrames, zObjMesh& topMeshObj, zObjMesh& bottomMeshObj)
    {
        std::vector<zItMeshHalfEdgeArray> loops;
        computeVLoops(mesh, medialIds,loops, topMeshObj, bottomMeshObj);
        numFrames = std::max(2, static_cast<int>(loops.size()));
    }

    void blendShapes(zObjMesh& shape0, zObjMesh& shape1, int numFrames, zObjMeshArray& meshes)
    {
        meshes.assign(std::max(0, numFrames), zObjMesh());
        if (numFrames <= 0) return;

        zFnMesh fn0(shape0);
        zFnMesh fn1(shape1);
        zPointArray pos0;
        zPointArray pos1;
        zIntArray counts;
        zIntArray connects;
        fn0.getVertexPositions(pos0);
        fn1.getVertexPositions(pos1);
        fn0.getPolygonData(connects, counts);

        const int count = std::min(pos0.size(), pos1.size());
        for (int i = 0; i < numFrames; i++) {
            const float weight = (numFrames == 1) ? 0.0f : static_cast<float>(i) / static_cast<float>(numFrames - 1);
            zPointArray positions = pos0;
            for (int j = 0; j < count; j++) positions[j] = pos0[j] * (1.0f - weight) + pos1[j] * weight;
            zFnMesh fnMesh(meshes[i]);
            fnMesh.create(positions, counts, connects);
        }
    }
    bool walkTopBottomStrips(
        zObjMesh& mesh,
        zItMeshHalfEdge heTopStart,
        zItMeshHalfEdge heBottomStart,
        std::vector<zItMeshHalfEdgeArray>& loops,
        zObjMesh& topMeshObj,
        zObjMesh& bottomMeshObj)
    {
        zFnMesh fn(mesh);

        auto printHalfEdge = [](const char* label, zItMeshHalfEdge he) {
            zItMeshVertex start = he.getStartVertex();
            zItMeshVertex end = he.getVertex();
            zVector edge = he.getVector();
            std::cout << "[walkTopBottomStrips] " << label
                << " he#" << he.getId()
                << " face#" << he.getFace().getId()
                << " " << start.getId() << " -> " << end.getId()
                << " val(" << start.getValence() << "," << end.getValence() << ")"
                << " len " << edge.length()
                << std::endl;
        };

        auto nextStripHalfEdge = [](zItMeshHalfEdge he, bool flip) {
            return flip
                ? he.getPrev().getSym().getPrev()
                : he.getNext().getSym().getNext();
        };

        auto stripReachedCorner = [](zItMeshHalfEdge he, bool flip) {
            return flip
                ? he.getVertex().getValence() == 3
                : he.getStartVertex().getValence() == 3;
        };

        zPointArray topPositions;
        zIntArray topCounts;
        zIntArray topConnects;
        zIntArray topVertexMap(fn.numVertices(), -1);
        std::vector<int> topOriginalVertexIds;

        zPointArray bottomPositions;
        zIntArray bottomCounts;
        zIntArray bottomConnects;
        zIntArray bottomVertexMap(fn.numVertices(), -1);
        std::vector<int> bottomOriginalVertexIds;

        auto mappedVertex = [&](int originalVertexId, zPointArray& positions, zIntArray& vertexMap, std::vector<int>& originalVertexIds) -> int {
            int& mappedId = vertexMap[originalVertexId];
            if (mappedId < 0) {
                zItMeshVertex v(mesh, originalVertexId);
                mappedId = static_cast<int>(positions.size());
                positions.push_back(v.getPosition());
                originalVertexIds.push_back(originalVertexId);
            }
            return mappedId;
        };

        auto appendFace = [&](zItMeshHalfEdge he, bool flip, zPointArray& positions, zIntArray& vertexMap, std::vector<int>& originalVertexIds, zIntArray& counts, zIntArray& connects) {
            zIntArray faceVerts;
            getFaceVerticesFromHalfedge(he, !flip, faceVerts);

            counts.push_back(static_cast<int>(faceVerts.size()));
            for (int originalVertexId : faceVerts) {
                connects.push_back(mappedVertex(originalVertexId, positions, vertexMap, originalVertexIds));
            }

            return faceVerts;
        };

        auto collectLongitudeEdges = [&](int startVID, int endVID) {
            zItMeshHalfEdgeArray longitudeEdges;

            if (startVID == endVID) {
                std::cout << "[walkTopBottomStrips] FAIL longitude pair has identical start/end vertex: "
                    << startVID << std::endl;
                return longitudeEdges;
            }

            zItMeshVertex vStart(mesh, startVID);
            zItMeshVertex vEnd(mesh, endVID);
            zVector dir = vEnd.getPosition() - vStart.getPosition();

            std::cout << "[walkTopBottomStrips] longitude pair " << startVID << " -> " << endVID
                << " val(" << vStart.getValence() << "," << vEnd.getValence() << ")"
                << " dir length " << dir.length() << std::endl;

            zItMeshHalfEdgeArray hEdgesStart;
            vStart.getConnectedHalfEdges(hEdgesStart);
            std::cout << "[walkTopBottomStrips] connected halfedges at longitude start: "
                << hEdgesStart.size() << std::endl;

            if (hEdgesStart.empty()) {
                std::cout << "[walkTopBottomStrips] FAIL longitude start vertex has no connected halfedges." << std::endl;
                return longitudeEdges;
            }

            float minAngle = std::numeric_limits<float>::max();
            zItMeshHalfEdge heStart = hEdgesStart[0];

            printHalfEdge("longitude initial candidate", heStart);
            for (auto& he : hEdgesStart) {
                const float angle = he.getVector().angle(dir);
                printHalfEdge("longitude checking candidate", he);
                std::cout << "[walkTopBottomStrips]   angle to longitude dir: " << angle << std::endl;
                if (angle < minAngle) {
                    minAngle = angle;
                    heStart = he;
                    std::cout << "[walkTopBottomStrips]   new longitude heStart, minAngle: " << minAngle << std::endl;
                }
            }

            printHalfEdge("longitude selected heStart", heStart);

            zItMeshHalfEdge heWalk = heStart;
            bool reachedEnd = false;
            for (int safety = 0; safety < fn.numPolygons() + 10; safety++) {
                longitudeEdges.push_back(heWalk);
                printHalfEdge("longitude walk edge", heWalk);

                if (heWalk.getVertex().getId() == endVID) {
                    std::cout << "[walkTopBottomStrips] longitude reached end vertex " << endVID << std::endl;
                    reachedEnd = true;
                    break;
                }

                heWalk = heWalk.getNext().getSym().getNext();
            }

            if (!reachedEnd) {
                std::cout << "[walkTopBottomStrips] FAIL longitude walk did not reach end vertex "
                    << endVID << std::endl;
                longitudeEdges.clear();
            }

            return longitudeEdges;
        };

        std::vector<std::pair<int, int>> visitedLongitudePairs;

        auto appendLongitudePair = [&](int startVID, int endVID) {
            const std::pair<int, int> key(startVID, endVID);
            if (std::find(visitedLongitudePairs.begin(), visitedLongitudePairs.end(), key) != visitedLongitudePairs.end()) {
                std::cout << "[walkTopBottomStrips] skip duplicate longitude pair already collected: "
                    << startVID << " -> " << endVID << std::endl;
                return true;
            }

            visitedLongitudePairs.push_back(key);

            zItMeshHalfEdgeArray longitudeEdges = collectLongitudeEdges(startVID, endVID);
            if (longitudeEdges.empty()) return false;

            loops.push_back(longitudeEdges);

            std::cout << "[walkTopBottomStrips] longitude loop added from "
                << startVID << " -> " << endVID
                << " edge count=" << longitudeEdges.size() << std::endl;
            return true;
        };

        auto appendLongitudePairsForStation = [&](const zIntArray& topFaceVerts, const zIntArray& bottomFaceVerts) {
            std::cout << "[walkTopBottomStrips] station top input vertex ids:";
            for (int id : topFaceVerts) std::cout << " " << id;
            std::cout << std::endl;

            std::cout << "[walkTopBottomStrips] station bottom input vertex ids:";
            for (int id : bottomFaceVerts) std::cout << " " << id;
            std::cout << std::endl;

            if (topFaceVerts.size() != bottomFaceVerts.size()) {
                std::cout << "[walkTopBottomStrips] FAIL top/bottom face vertex counts differ "
                    << topFaceVerts.size() << " vs " << bottomFaceVerts.size() << std::endl;
                return false;
            }

            for (int i = 0; i < static_cast<int>(topFaceVerts.size()); i++) {
                const int topVID = topFaceVerts[i];
                const int bottomVID = bottomFaceVerts[i];
                std::cout << "[walkTopBottomStrips] station longitude vertex pair "
                    << "index " << i << ": " << topVID << " -> " << bottomVID << std::endl;

                if (!appendLongitudePair(topVID, bottomVID)) return false;
            }

            return true;
        };
        
        zItMeshHalfEdge heTopWalk = heTopStart;
        zItMeshHalfEdge heBottomWalk = heBottomStart;
        int station = 0;
        int safety = 0;

        do {
            std::cout << "[walkTopBottomStrips] station " << station << std::endl;
            printHalfEdge("top", heTopWalk);
            printHalfEdge("bottom", heBottomWalk);

            zIntArray topFaceVerts = appendFace(heTopWalk, true, topPositions, topVertexMap, topOriginalVertexIds, topCounts, topConnects);
            zIntArray bottomFaceVerts = appendFace(heBottomWalk, false, bottomPositions, bottomVertexMap, bottomOriginalVertexIds, bottomCounts, bottomConnects);

            if (!appendLongitudePairsForStation(topFaceVerts, bottomFaceVerts)) {
                std::cout << "[walkTopBottomStrips] FAIL at station " << station << std::endl;
                loops.clear();
                return false;
            }

            zItMeshHalfEdge nextTop = nextStripHalfEdge(heTopWalk, true);
            zItMeshHalfEdge nextBottom = nextStripHalfEdge(heBottomWalk, false);
            const bool topDone = stripReachedCorner(nextTop, true);
            const bool bottomDone = stripReachedCorner(nextBottom, false);

            if (topDone || bottomDone) {
                std::cout << "[walkTopBottomStrips] stop reason: "
                    << (topDone ? "top valence 3" : "")
                    << (topDone && bottomDone ? " + " : "")
                    << (bottomDone ? "bottom valence 3" : "")
                    << std::endl;
                break;
            }

            heTopWalk = nextTop;
            heBottomWalk = nextBottom;
            station++;
            safety++;

        } while (safety < fn.numPolygons() + 10);

        if (safety >= fn.numPolygons() + 10) {
            std::cout << "[walkTopBottomStrips] FAIL stop reason: safety limit" << std::endl;
            loops.clear();
            return false;
        }

        std::cout << "[walkTopBottomStrips] top mesh positions=" << topPositions.size()
            << ", faces=" << topCounts.size()
            << ", connects=" << topConnects.size() << std::endl;
        std::cout << "[walkTopBottomStrips] top result vertex map (resultVID -> inputVID):" << std::endl;
        for (int i = 0; i < static_cast<int>(topOriginalVertexIds.size()); i++) {
            std::cout << "[walkTopBottomStrips]   top " << i << " -> input " << topOriginalVertexIds[i] << std::endl;
        }

        std::cout << "[walkTopBottomStrips] bottom mesh positions=" << bottomPositions.size()
            << ", faces=" << bottomCounts.size()
            << ", connects=" << bottomConnects.size() << std::endl;
        std::cout << "[walkTopBottomStrips] bottom result vertex map (resultVID -> inputVID):" << std::endl;
        for (int i = 0; i < static_cast<int>(bottomOriginalVertexIds.size()); i++) {
            std::cout << "[walkTopBottomStrips]   bottom " << i << " -> input " << bottomOriginalVertexIds[i] << std::endl;
        }

        std::cout << "[walkTopBottomStrips] longitude loop rows=" << loops.size() << std::endl;
        for (int i = 0; i < static_cast<int>(loops.size()); i++) {
            std::cout << "[walkTopBottomStrips]   loop[" << i << "] edge count=" << loops[i].size() << std::endl;
        }

        zFnMesh fnTop(topMeshObj);
        fnTop.clear();
        if (!topPositions.empty()) fnTop.create(topPositions, topCounts, topConnects);

        zFnMesh fnBottom(bottomMeshObj);
        fnBottom.clear();
        if (!bottomPositions.empty()) fnBottom.create(bottomPositions, bottomCounts, bottomConnects);

        return true;
    }

    void computeVLoops(zObjMesh& mesh, zIntArray& medialIds,   std::vector<zItMeshHalfEdgeArray>& loops, zObjMesh& topMeshObj, zObjMesh& bottomMeshObj)
    {
        loops.clear();
        zFnMesh fnMesh(mesh);
        std::cout << "[computeVLoops] ---- begin ----" << std::endl;
        std::cout << "[computeVLoops] medialIds size: " << medialIds.size() << std::endl;
        if (medialIds.size() < 2) {
            std::cout << "[computeVLoops] abort: medialIds needs at least 2 vertex ids." << std::endl;
            return;
        }

        // const int stride = std::max(1, featuredNumStrides[0]);
        const int startVID = medialIds[0];
        const int endVID = medialIds[1];
        std::cout << "[computeVLoops] input edge vertices: " << startVID << " -> " << endVID << std::endl;

        zItMeshVertex vStart(mesh, startVID);
        zItMeshVertex vEnd(mesh, endVID);
        zVector dir = vEnd.getPosition() - vStart.getPosition();
        std::cout << "[computeVLoops] vStart valence: " << vStart.getValence()
            << ", vEnd valence: " << vEnd.getValence()
            << ", dir length: " << dir.length() << std::endl;

        auto printHalfEdge = [](const char* label, zItMeshHalfEdge he) {
            zItMeshVertex start = he.getStartVertex();
            zItMeshVertex end = he.getVertex();
            zVector edge = he.getVector();
            std::cout << "[computeVLoops] " << label
                << " he#" << he.getId()
                << " face#" << he.getFace().getId()
                << " " << start.getId() << " -> " << end.getId()
                << " val(" << start.getValence() << "," << end.getValence() << ")"
                << " len " << edge.length()
                << std::endl;
        };

        zItMeshHalfEdgeArray hEdgesStart;
        vStart.getConnectedHalfEdges(hEdgesStart);
        std::cout << "[computeVLoops] connected halfedges at start vertex: " << hEdgesStart.size() << std::endl;
        if (hEdgesStart.empty()) {
            std::cout << "[computeVLoops] abort: no connected halfedges." << std::endl;
            return;
        }

        float minAngle = std::numeric_limits<float>::max();
        zItMeshHalfEdge heStart = hEdgesStart[0];
        printHalfEdge("initial heStart candidate", heStart);
        for (auto& he : hEdgesStart) {
            const float angle = he.getVector().angle(dir);
            printHalfEdge("checking start candidate", he);
            std::cout << "[computeVLoops]   angle to input dir: " << angle << std::endl;
            if (angle < minAngle) {
                minAngle = angle;
                heStart = he;
                std::cout << "[computeVLoops]   new heStart, minAngle: " << minAngle << std::endl;
            }
        }
        printHalfEdge("selected heStart", heStart);
        //HESTARRT: LONGTITUDE CORNER
        zItMeshHalfEdge he = heStart; // temp assign fix later?
        // norm.normalize();

        zItMeshHalfEdge heBottom;
        zItMeshHalfEdge heTop;
        bool foundTop = false;
        bool foundBottom = false;
        int tempCounter = 0;
        std::cout << "[computeVLoops] searching heTop / heBottom " << std::endl;
        for (auto& he : hEdgesStart) {
            printHalfEdge("top/bottom candidate root", he);
            std::cout << "[computeVLoops]   next vertex valence: "
                << he.getVertex().getValence() << std::endl;
            if(he.getVertex().getValence() != 3 && he!=heStart) 
            {
                heTop = he.getSym();
                foundTop = true;
                printHalfEdge("  assigned heTop", heTop);
            }
            else if(he == heStart){
                std::cout << "[computeVLoops]   walking to find heBottom." << std::endl;
                while(he.getVertex().getValence() != 3 && tempCounter < fnMesh.numPolygons() + 10)
                {
                    he = he.getNext().getSym().getNext();
                    tempCounter++;
                    printHalfEdge("    heBottom walk step", he);
                    std::cout << "[computeVLoops]     tempCounter: " << tempCounter
                        << ", next vertex valence: " << he.getNext().getVertex().getValence()
                        << std::endl;
                }

                if (he.getVertex().getValence() != 3) {
                    std::cout << "[computeVLoops] abort: heBottom search hit safety limit before valence-3 vertex." << std::endl;
                    return;
                }

                heBottom = he.getSym().getPrev().getSym();
                foundBottom = true;
                printHalfEdge("  assigned heBottom", heBottom);
            }
            }
        //done 06.21
         std::cout << "[computeVLoops] after top/bottom search tempCounter: " << tempCounter << std::endl;

        if (!foundTop || !foundBottom) {
            std::cout << "[computeVLoops] abort: failed to find "
                << (!foundTop ? "heTop " : "")
                << (!foundBottom ? "heBottom" : "")
                << std::endl;
            return;
        }

        if (!walkTopBottomStrips(mesh, heTop, heBottom, loops, topMeshObj, bottomMeshObj)) {
            loops.clear();
            zFnMesh fnTop(topMeshObj);
            fnTop.clear();
            zFnMesh fnBottom(bottomMeshObj);
            fnBottom.clear();
            std::cout << "[computeVLoops] abort: paired strip walk failed." << std::endl;
            return;
        }
    }

    void computeVLoops(zObjMesh& mesh, zIntArray& medialIds, std::vector<zItMeshHalfEdgeArray>& loops, zObjMesh& topMeshObj, zObjMesh& bottomMeshObj, SliceMetadata* metadata)
    {
        computeVLoops(mesh, medialIds, loops, topMeshObj, bottomMeshObj);
        if (metadata) {
            zObjGraphArray emptyGraphs;
            populateSliceMetadata(mesh, loops, emptyGraphs, *metadata);
        }
    }

    void populateSliceMetadata(zObjMesh& mesh, std::vector<zItMeshHalfEdgeArray>& loops, zObjGraphArray& sectionGraphs, SliceMetadata& metadata)
    {
        metadata.cornerVertexIds.clear();
        metadata.cornerLongitudeIds.clear();
        metadata.sectionVertexOriginalIds.clear();
        metadata.layerT.clear();

        for (zItMeshVertex v(mesh); !v.end(); v++) {
            if (v.getValence() == 3) metadata.cornerVertexIds.push_back(v.getId());
        }

        auto isCornerVertex = [&](int vertexId) {
            return std::find(metadata.cornerVertexIds.begin(), metadata.cornerVertexIds.end(), vertexId) != metadata.cornerVertexIds.end();
        };

        for (int i = 0; i < static_cast<int>(loops.size()); i++) {
            if (loops[i].empty()) continue;
            const int startId = loops[i].front().getStartVertex().getId();
            const int endId = loops[i].back().getVertex().getId();
            if (isCornerVertex(startId) || isCornerVertex(endId)) metadata.cornerLongitudeIds.push_back(i);
        }

        metadata.layerT.assign(sectionGraphs.size(), 0.0f);
        for (int layer = 0; layer < static_cast<int>(sectionGraphs.size()); layer++) {
            metadata.layerT[layer] = (sectionGraphs.size() <= 1)
                ? 0.0f
                : static_cast<float>(layer) / static_cast<float>(sectionGraphs.size() - 1);
        }

        std::cout << "[populateSliceMetadata] corners=" << metadata.cornerVertexIds.size()
            << " cornerLongitudes=" << metadata.cornerLongitudeIds.size()
            << " layers=" << metadata.layerT.size()
            << std::endl;
    }

    void computeGeodesicScalars(zObjMesh& mesh, std::vector<zItMeshHalfEdgeArray>& loops, zScalarArray& scalars, bool normalise)
    {
        zFnMesh fnMesh(mesh);
        scalars.clear();
        scalars.assign(fnMesh.numVertices(), -1.0f);

        float minMaxDist = std::numeric_limits<float>::max();
        std::vector<zDomainFloat> loopDomains(loops.size(), zDomainFloat(10000, -10000));

        for (int l = 0; l < static_cast<int>(loops.size()); l++) {
            float length = 0.0f;
            for (int j = 0; j < static_cast<int>(loops[l].size()); j++) {
                if (j == 0) {
                    scalars[loops[l][j].getVertex().getId()] = length;
                    loopDomains[l].min = length;
                }

                length += loops[l][j].getLength();
                scalars[loops[l][j].getStartVertex().getId()] = length;

                if (j == static_cast<int>(loops[l].size()) - 1 && length < minMaxDist) minMaxDist = length;
                if (length > loopDomains[l].max) loopDomains[l].max = length;
            }
        }

        if (normalise && minMaxDist < std::numeric_limits<float>::max()) {
            zDomainFloat outDomain(0, minMaxDist);
            for (int l = 0; l < static_cast<int>(loops.size()); l++) {
                for (int j = 0; j < static_cast<int>(loops[l].size()); j++) {
                    const int id = loops[l][j].getStartVertex().getId();
                    scalars[id] = coreUtils().ofMap(scalars[id], loopDomains[l], outDomain);
                }
            }
        }

        zFloatArray scalarFloats(scalars.begin(), scalars.end());
        colorMesh(mesh, scalarFloats);
    }

    void computeGeodesicContours(std::vector<zItMeshHalfEdgeArray>& loops, zScalarArray& scalars, float spacing, zObjMesh& topMeshObj, zObjMesh& bottomMeshObj, zObjMeshArray& meshes)
    {
        if (loops.empty() || scalars.empty() || spacing <= 0.0f) {
            meshes.clear();
            return;
        }

        const zScalar minScalar = coreUtils().zMin(scalars);
        const zScalar maxScalar = coreUtils().zMax(scalars);
        const int totalContours = std::max(1, static_cast<int>(std::ceil((maxScalar - minScalar) / spacing)));
        const float increment = (maxScalar - minScalar) / static_cast<float>(totalContours);

        meshes.clear();
        meshes.assign(totalContours, bottomMeshObj);

        for (int l = 0; l < totalContours; l++) {
            const float threshold = l * increment;
            zFnMesh fnMesh(meshes[l]);
            zPoint* points = fnMesh.getRawVertexPositions();
            if (!points) continue;

            for (int i = 0; i < static_cast<int>(loops.size()); i++) {
                for (int j = 0; j < static_cast<int>(loops[i].size()); j++) {
                    const int startId = loops[i][j].getStartVertex().getId();
                    const int endId = loops[i][j].getVertex().getId();
                    if (startId < 0 || startId >= static_cast<int>(scalars.size())) continue;
                    if (endId < 0 || endId >= static_cast<int>(scalars.size())) continue;

                    const float s0 = scalars[startId];
                    const float s1 = scalars[endId];
                    if ((s0 <= threshold && s1 >= threshold) || (s0 >= threshold && s1 <= threshold)) {
                        zPoint v0 = loops[i][j].getStartVertex().getPosition();
                        zPoint v1 = loops[i][j].getVertex().getPosition();
                        points[i] = getContourPosition(threshold, v1, v0, s1, s0);
                    }
                }
            }
        }

        meshes.push_back(topMeshObj);
    }

    void computeGeodesicContours(zObjMesh& mesh, zFloatArray& scalars, float spacing, zObjGraphArray& contourGraphs)
    {
        if (scalars.empty() || spacing <= 0.0f) {
            contourGraphs.clear();
            return;
        }

        const zScalar minScalar = coreUtils().zMin(scalars);
        const zScalar maxScalar = coreUtils().zMax(scalars);
        const int totalContours = std::max(1, static_cast<int>(std::ceil((maxScalar - minScalar) / spacing)));
        contourGraphs.assign(totalContours, zObjGraph());

        for (int i = 0; i < totalContours; i++) {
            zPointArray positions;
            zIntArray edgeConnects;
            zColorArray vertexColors;
            zFnMesh fnMesh(mesh);
            fnMesh.getIsoContour(scalars, minScalar + i * spacing, positions, edgeConnects, vertexColors);
            zFnGraph fnGraph(contourGraphs[i]);
            fnGraph.create(positions, edgeConnects);
            fnGraph.setEdgeColor(zColor(1, 1, 1, 1));
            fnGraph.setEdgeWeight(2);
        }
    }

    void createSectionGraphs(zObjMeshArray& meshes, zObjGraphArray& sectionGraphs)
    {
        sectionGraphs.assign(meshes.size(), zObjGraph());
        for (int i = 0; i < static_cast<int>(meshes.size()); i++) {
            createBoundaryEdgeGraph(meshes[i], true, sectionGraphs[i]);
            zFnGraph fnGraph(sectionGraphs[i]);
            fnGraph.setEdgeColor(zColor(0, 1, 0, 1));
            fnGraph.setEdgeWeight(3);
        }
    }

    void computeSDFLayers(zObjGraphArray& sectionGraphs, zObjMeshArray& sectionMeshes, int layerCount,
        zObjGraphArray& contourGraphs, zObjMeshScalarFieldArray* sdfFields, zObjGraphArray* transformedFlatGraphs)
    {
        computeSDFLayers(sectionGraphs, sectionMeshes, layerCount, contourGraphs, sdfFields, transformedFlatGraphs, nullptr, nullptr, nullptr);
    }

    void computeSDFLayers(zObjGraphArray& sectionGraphs, zObjMeshArray& sectionMeshes, int layerCount,
        zObjGraphArray& contourGraphs, zObjMeshScalarFieldArray* sdfFields,
        zObjGraphArray* transformedFlatGraphs, const zObjGraphArray* bracingGraphs,
        zObjGraphArray* flatBracingGraphs, zObjGraphArray* bracingSlotGraphs,
        SDFLayerDebugData* debugData)
    {
        contourGraphs.clear();
        if (sdfFields) sdfFields->clear();
        if (transformedFlatGraphs) transformedFlatGraphs->clear();
        if (flatBracingGraphs) flatBracingGraphs->clear();
        if (bracingSlotGraphs) bracingSlotGraphs->clear();

        if (layerCount <= 0 || sectionGraphs.empty() || sectionMeshes.empty()) return;

        const int availableLayerCount = std::min(static_cast<int>(sectionGraphs.size()), static_cast<int>(sectionMeshes.size()));
        const int computeLayerCount = std::min(layerCount, availableLayerCount);

        zObjGraphArray layerGraphs;
        zObjMeshArray layerMeshes;
        zObjGraphArray layerBracingGraphs;
        layerGraphs.reserve(computeLayerCount);
        layerMeshes.reserve(computeLayerCount);
        layerBracingGraphs.reserve(computeLayerCount);

        for (int i = 0; i < computeLayerCount; i++) {
            layerGraphs.push_back(sectionGraphs[i]);
            layerMeshes.push_back(sectionMeshes[i]);
            if (bracingGraphs && i < static_cast<int>(bracingGraphs->size())) layerBracingGraphs.push_back((*bracingGraphs)[i]);
        }

        computeSDF(layerGraphs, layerMeshes, contourGraphs, sdfFields, transformedFlatGraphs,
            bracingGraphs ? &layerBracingGraphs : nullptr, flatBracingGraphs, bracingSlotGraphs, debugData);
    }


    void computeSDF(zObjGraphArray& sectionGraphs, zObjMeshArray& sectionMeshes, zObjGraphArray& contourGraphs, zObjMeshScalarFieldArray* sdfFields, zObjGraphArray* transformedFlatGraphs)
    {
        computeSDF(sectionGraphs, sectionMeshes, contourGraphs, sdfFields, transformedFlatGraphs, nullptr, nullptr, nullptr);
    }

    void computeSDF(zObjGraphArray& sectionGraphs, zObjMeshArray& sectionMeshes, zObjGraphArray& contourGraphs,
        zObjMeshScalarFieldArray* sdfFields, zObjGraphArray* transformedFlatGraphs,
        const zObjGraphArray* bracingGraphs, zObjGraphArray* flatBracingGraphs,
        zObjGraphArray* bracingSlotGraphs, SDFLayerDebugData* debugData)
    {
        contourGraphs.clear();
        contourGraphs.assign(sectionGraphs.size(), zObjGraph());
        if (sdfFields) {
            sdfFields->clear();
            sdfFields->assign(sectionGraphs.size(), zObjMeshScalarField());
        }
        if (transformedFlatGraphs) {
            transformedFlatGraphs->clear();
            transformedFlatGraphs->assign(sectionGraphs.size(), zObjGraph());
        }
        if (flatBracingGraphs) {
            flatBracingGraphs->clear();
            flatBracingGraphs->assign(sectionGraphs.size(), zObjGraph());
        }
        if (bracingSlotGraphs) {
            bracingSlotGraphs->clear();
            bracingSlotGraphs->assign(sectionGraphs.size(), zObjGraph());
        }
        if (debugData) {
            debugData->finalFields.clear();
            debugData->finalFields.assign(sectionGraphs.size(), zScalarArray());
            debugData->localFlattenedMeshes.clear();
            debugData->localFlattenedMeshes.assign(sectionGraphs.size(), zObjMesh());
            debugData->fieldMeshes.clear();
            debugData->fieldMeshes.assign(sectionGraphs.size(), zObjMeshScalarField());
            debugData->flatContourGraphs.clear();
            debugData->flatContourGraphs.assign(sectionGraphs.size(), zObjGraph());
        }

        constexpr float printBoundaryWidth = SlicingParameters::printBoundaryWidth;
        constexpr float printBracingWidth = SlicingParameters::printBracingWidth;
        constexpr float printOverlapWidth = SlicingParameters::printOverlapWidth;
         constexpr float printBracingDistanceWidth = printBracingWidth - 0.5f * printOverlapWidth;
        constexpr float offset_1st_exterior = printBoundaryWidth * 0.5f;
        constexpr float offset_2nd_exterior = printBoundaryWidth - (2.0f * printOverlapWidth);
        constexpr float trimSlotWidth = SlicingParameters::trimSlotWidth;
        constexpr float edgeTrimSlotWidth = SlicingParameters::edgeTrimSlotWidth;
        constexpr float sdfWidth = SlicingParameters::sdfWidth;
        constexpr int fieldResX = SlicingParameters::sdfFieldResolutionX;
        constexpr int fieldResY = SlicingParameters::sdfFieldResolutionY;
        const zDomain<zPoint>& layerFieldBB = SlicingParameters::sdfFieldBounds;
        auto flattenPlanarMeshToXY = [](zObjMesh& mesh) {
            zFnMesh fnMesh(mesh);
            zPointArray positions;
            fnMesh.getVertexPositions(positions);
            if (positions.size() < 3) {
                std::cout << "[computeSDF] WARNING cannot plane-flatten mesh with fewer than 3 vertices." << std::endl;
                return;
            }

            zPoint origin = positions[0];
            zVector xAxis;
            bool foundXAxis = false;
            for (int v = 1; v < static_cast<int>(positions.size()); v++) {
                xAxis = positions[v] - origin;
                if (xAxis.length() > 1e-6) {
                    xAxis.normalize();
                    foundXAxis = true;
                    break;
                }
            }

            if (!foundXAxis) {
                std::cout << "[computeSDF] WARNING cannot plane-flatten mesh: no valid x axis." << std::endl;
                return;
            }

            zVector normal(0, 0, 0);
            for (int aId = 1; aId < static_cast<int>(positions.size()) && normal.length() <= 1e-6; aId++) {
                for (int bId = aId + 1; bId < static_cast<int>(positions.size()); bId++) {
                    zVector a = positions[aId] - origin;
                    zVector b = positions[bId] - origin;
                    normal = a ^ b;
                    if (normal.length() > 1e-6) break;
                }
            }

            if (normal.length() <= 1e-6) {
                std::cout << "[computeSDF] WARNING cannot plane-flatten mesh: no valid normal." << std::endl;
                return;
            }

            normal.normalize();
            zVector yAxis = normal ^ xAxis;
            if (yAxis.length() <= 1e-6) {
                std::cout << "[computeSDF] WARNING cannot plane-flatten mesh: no valid y axis." << std::endl;
                return;
            }
            yAxis.normalize();

            for (auto& p : positions) {
                zVector d = p - origin;
                p = zPoint(d * xAxis, d * yAxis, 0.0);
            }

            fnMesh.setVertexPositions(positions);
            std::cout << "[computeSDF] plane-to-XY origin=(" << origin.x << "," << origin.y << "," << origin.z << ")"
                << " xAxis=(" << xAxis.x << "," << xAxis.y << "," << xAxis.z << ")"
                << " yAxis=(" << yAxis.x << "," << yAxis.y << "," << yAxis.z << ")"
                << " normal=(" << normal.x << "," << normal.y << "," << normal.z << ")"
                << std::endl;
        };

        for (int i = 0; i < static_cast<int>(sectionGraphs.size()) && i < static_cast<int>(sectionMeshes.size()); i++) {
            zObjMesh flattenedMesh;
            zInt2DArray oriVertexUnrollVertexMap;
            std::unordered_map<zIntPair, int, zPairHash> oriFaceVertexUnrollVertex;
            zItGraphVertexArray bsfVertices;
             zIntPairArray bsfVertexPairs;
            zObjGraph dualGraph;

            creatUnrollMesh(sectionMeshes[i], flattenedMesh, dualGraph, oriVertexUnrollVertexMap, oriFaceVertexUnrollVertex, bsfVertices, bsfVertexPairs);
            flattenPlanarMeshToXY(flattenedMesh);
            mergeMesh(flattenedMesh);

            zObjGraph flatGraph = sectionGraphs[i];
            if (!barycentericProjection_triMesh(flatGraph, sectionMeshes[i], flattenedMesh)) {
                std::cout << "[computeSDF] section " << i
                    << " WARNING skipping SDF: flat graph projection failed"
                    << std::endl;
                continue;
            }
            zFnGraph fnFlatGraph(flatGraph);

            if (fnFlatGraph.numVertices() == 0) {
                std::cout << "[computeSDF] section " << i << " WARNING flat boundary graph empty" << std::endl;
                continue;
            }

            zPointArray flatGraphPositions;
            fnFlatGraph.getVertexPositions(flatGraphPositions);
            const zPoint origin = flatGraphPositions[0];

            zFnMesh fnFlattenedMesh(flattenedMesh);
            zPoint* flattenedPositions = fnFlattenedMesh.getRawVertexPositions();
            for (int v = 0; v < fnFlattenedMesh.numVertices(); v++) {
                flattenedPositions[v] -= origin;
            }

            zPoint* flatGraphRawPositions = fnFlatGraph.getRawVertexPositions();
            for (int v = 0; v < fnFlatGraph.numVertices(); v++) {
                flatGraphRawPositions[v] -= origin;
            }

            printGraphSDFDebug("flatGraph before polygon SDF", i, flatGraph, layerFieldBB);

            if (transformedFlatGraphs) (*transformedFlatGraphs)[i] = flatGraph;

            zObjMeshScalarField localField;
            zObjMeshScalarField& field = sdfFields ? (*sdfFields)[i] : localField;
            zFnMeshScalarField fnField(field);
            fnField.create(layerFieldBB.min, layerFieldBB.max, fieldResX, fieldResY, 1, true, false);
            zDomainColor colorDomain(zBLUE, zRED);
            fnField.setFieldColorDomain(colorDomain);

            zScalarArray polyField;
            fnField.getScalars_Polygon(polyField, flatGraph, false);


            int finiteCount = 0;
            if (!polyField.empty()) {
                double minScalar = std::numeric_limits<double>::max();
                double maxScalar = -std::numeric_limits<double>::max();
                for (double value : polyField) {
                    if (!std::isfinite(value)) continue;
                    minScalar = std::min(minScalar, value);
                    maxScalar = std::max(maxScalar, value);
                    finiteCount++;
                }
                std::cout << "[computeSDF] section " << i
                    << " polyField count=" << polyField.size()
                    << " finite=" << finiteCount
                    << " range=[" << minScalar << "," << maxScalar << "]"
                    << std::endl;
            }
            else {
                std::cout << "[computeSDF] section " << i << " WARNING polyField empty" << std::endl;
            }

            if (polyField.size() != static_cast<size_t>(fieldResX * fieldResY) || finiteCount != static_cast<int>(polyField.size())) {
                std::cout << "[computeSDF] section " << i
                    << " WARNING skipping contour: invalid polygon field values"
                    << std::endl;
                continue;
            }

            zScalarArray scalarOffsetOuter = polyField;
            zScalarArray scalarOffsetInner = polyField;
            //offset
            for (int sf = 0; sf < static_cast<int>(polyField.size()); sf++) {
                scalarOffsetOuter[sf] += offset_1st_exterior;
                scalarOffsetInner[sf] += offset_1st_exterior + offset_2nd_exterior;
            }

            zScalarArray finalField = scalarOffsetOuter;
            zObjGraph flatBracingGraph;
            zObjGraph bracingSlotsGraph;
            bool hasBracingField = false;

            if (bracingGraphs && i < static_cast<int>(bracingGraphs->size())) {
                flatBracingGraph = (*bracingGraphs)[i];
                zFnGraph fnInputBracing(flatBracingGraph);
                if (fnInputBracing.numVertices() > 0 && fnInputBracing.numEdges() > 0) {
                    const int snappedBracingVertices = snapGraphVerticesToClosestMesh(flatBracingGraph, sectionMeshes[i], 1e-5);
                    if (snappedBracingVertices > 0) {
                        std::cout << "[computeSDF] section " << i
                            << " snapped " << snappedBracingVertices
                            << " interpolated bracing vertices to current section mesh before flatten projection"
                            << std::endl;
                    }
                    if (barycentericProjection_triMesh(flatBracingGraph, sectionMeshes[i], flattenedMesh)) {
                        zFnGraph fnFlatBracing(flatBracingGraph);
                        // flattenedMesh has already been shifted to the same local origin as flatGraph.
                        // Projected bracing vertices are therefore already in SDF-local XY space.
                        fnFlatBracing.setEdgeColor(zColor(0, 0.75, 1, 1));
                        fnFlatBracing.setEdgeWeight(4);
                        if (flatBracingGraphs) (*flatBracingGraphs)[i] = flatBracingGraph;

                        zObjGraph trimSlotsBracingFlat;
                        zObjGraph trimSlotsBoundaryFlat;
                        zObjGraph boundaryCornerEdge;
                        const float trimLength = 
                            printBracingWidth + SlicingParameters::trimSlotLengthExtra
                        ;
                        createPerpendicularTrimSlots(flatBracingGraph, trimSlotsBracingFlat, i % 2 == 0, trimLength);
                        if (!makeFirstCornerEdgeGraph(flatGraph, boundaryCornerEdge)) {
                            std::cout << "[computeSDF] section " << i
                                << " WARNING no corner-color boundary edge found; using edge 0 for boundary trim debug"
                                << std::endl;
                            makeSingleEdgeGraph(flatGraph, 0, boundaryCornerEdge);
                        }
                        createPerpendicularTrimSlots(boundaryCornerEdge, trimSlotsBoundaryFlat, i % 2 == 0, trimLength);

                        zObjGraphArray trimSlotSources;
                        trimSlotSources.push_back(trimSlotsBracingFlat);
                        trimSlotSources.push_back(trimSlotsBoundaryFlat);
                        combineGraphObjects(trimSlotSources, bracingSlotsGraph);
                        zFnGraph fnBracingSlots(bracingSlotsGraph);
                        fnBracingSlots.setEdgeColor(zColor(0.1, 0.2, 1, 1));
                        fnBracingSlots.setEdgeWeight(4);
                        if (bracingSlotGraphs) (*bracingSlotGraphs)[i] = bracingSlotsGraph;

                        zScalarArray scalarBracing;
                        zScalarArray scalarBracingSlots;
                        zScalarArray scalarBoundarySlots;
                        zScalarArray scalarInteriorBracing;
                        zScalarArray scalarBooleanBracing;
                        zScalarArray scalarOffsetOuterOpened;
                        zScalarArray booleanField;
                        fnField.getScalarsAsEdgeDistance(scalarBracing, flatBracingGraph, printBracingDistanceWidth*0.5f, false);
                        fnField.getScalarsAsEdgeDistance(scalarBracingSlots, bracingSlotsGraph, trimSlotWidth * 0.5f, false);
                        fnField.getScalarsAsEdgeDistance(scalarBoundarySlots, trimSlotsBoundaryFlat, edgeTrimSlotWidth * 0.5f, false);
                        if (scalarBracing.size() == polyField.size() && scalarBracingSlots.size() == polyField.size()) {
                            fnField.boolean_subtract(scalarBracing, scalarBracingSlots, scalarInteriorBracing, false);
                            fnField.boolean_subtract(scalarOffsetInner, scalarInteriorBracing, scalarBooleanBracing, false);
                            fnField.boolean_subtract(scalarOffsetOuter, scalarBoundarySlots, scalarOffsetOuterOpened, false);
                            fnField.boolean_subtract(scalarOffsetOuterOpened, scalarBooleanBracing, booleanField, false);
                            if (booleanField.size() == polyField.size()) {
                                finalField = booleanField;
                                hasBracingField = true;
                            }
                        }
                    }
                    else {
                        std::cout << "[computeSDF] section " << i
                            << " WARNING bracing projection failed; using outer-offset field"
                            << std::endl;
                    }
                }
            }

            int finalFiniteCount = 0;
            double finalMinScalar = std::numeric_limits<double>::max();
            double finalMaxScalar = -std::numeric_limits<double>::max();
            for (double value : finalField) {
                if (!std::isfinite(value)) continue;
                finalMinScalar = std::min(finalMinScalar, value);
                finalMaxScalar = std::max(finalMaxScalar, value);
                finalFiniteCount++;
            }
            std::cout << "[computeSDF] section " << i
                << " finalField mode=" << (hasBracingField ? "carbcomn_func5_outer_minus_bracing" : "outer_offset_no_bracing")
                << " finite=" << finalFiniteCount << "/" << finalField.size()
                << " range=[" << finalMinScalar << "," << finalMaxScalar << "]"
                << std::endl;

            fnField.setFieldValues(finalField, zFieldSDF, sdfWidth);
            colorSDFFieldFromValues(field, finalField, sdfWidth);
            if (debugData) {
                debugData->finalFields[i] = finalField;
                debugData->localFlattenedMeshes[i] = flattenedMesh;
                debugData->fieldMeshes[i] = field;
            }
            fnField.getIsocontour(contourGraphs[i], 0.0, 3, 0.001);
            cleanContourGraphForToolpath(i, contourGraphs[i], SlicingParameters::contourCleanupMergeTolerance);
            if (debugData) debugData->flatContourGraphs[i] = contourGraphs[i];
            if (zFnGraph(contourGraphs[i]).numVertices() > 0) {
                zVectorArray contourNormals;
                if (!barycentericProjection_triMesh(contourGraphs[i], flattenedMesh, sectionMeshes[i], &contourNormals)) {
                    std::cout << "[computeSDF] section " << i
                        << " WARNING contour projection to section mesh failed; keeping flat contour"
                        << std::endl;
                    if (debugData) contourGraphs[i] = debugData->flatContourGraphs[i];
                }
            }
            
            zFnGraph fnGraph(contourGraphs[i]);
            fnGraph.setEdgeColor(zColor(1, 0, 1, 1));
            fnGraph.setEdgeWeight(4);

            std::cout << "[computeSDF] section " << i
                << " flatGraph vertices=" << fnFlatGraph.numVertices()
                << " edges=" << fnFlatGraph.numEdges()
                << " origin=(" << origin.x << "," << origin.y << "," << origin.z << ")"
                << std::endl;
            std::cout << "[computeSDF] section " << i
                << " field bounds min=(" << layerFieldBB.min.x << "," << layerFieldBB.min.y << "," << layerFieldBB.min.z << ")"
                << " max=(" << layerFieldBB.max.x << "," << layerFieldBB.max.y << "," << layerFieldBB.max.z << ")"
                << " res=" << fieldResX << "x" << fieldResY
                << " sdfWidth=" << sdfWidth
                << std::endl;

            zFnMesh fnFieldMesh(field);
            std::cout << "[computeSDF] section " << i
                << " fieldMesh vertices=" << fnFieldMesh.numVertices()
                << " faces=" << fnFieldMesh.numPolygons()
                << std::endl;
            std::cout << "[computeSDF] section " << i
                << " contourGraph vertices=" << fnGraph.numVertices()
                << " edges=" << fnGraph.numEdges()
                << std::endl;
        }
    }

    void computeSDFPostProcess(zObjMeshArray& sectionMeshes, zObjGraphArray& contourGraphs,
        SDFLayerDebugData& debugData, SDFPostProcessResult& result, float sampleLength,
        float featureAngleThreshold)
    {
        result.toolpathGraphs.clear();
        result.flatToolpathGraphs.clear();
        result.toolpathTargetPoints.clear();
        result.flatToolpathTargetPoints.clear();
        result.toolpathPrintHeights.clear();
        result.toolpathPrintWidths.clear();
        result.toolpathFeatureFlags.clear();
        result.toolpathNormals.clear();

        const int layerCount = static_cast<int>(contourGraphs.size());
        if (layerCount <= 0) return;

        result.toolpathGraphs.assign(layerCount, zObjGraph());
        result.flatToolpathGraphs.assign(layerCount, zObjGraph());
        result.toolpathTargetPoints.assign(layerCount, zPointArray());
        result.flatToolpathTargetPoints.assign(layerCount, zPointArray());
        result.toolpathPrintHeights.assign(layerCount, zFloatArray());
        result.toolpathPrintWidths.assign(layerCount, zFloatArray());
        result.toolpathFeatureFlags.assign(layerCount, zIntArray());
        result.toolpathNormals.assign(layerCount, zVectorArray());

        constexpr float printBoundaryWidth = SlicingParameters::printBoundaryWidth;
        constexpr float printBracingWidth = SlicingParameters::printBracingWidth;
        constexpr float boundarySdfTarget = SlicingParameters::boundarySdfTarget;
        constexpr float bracingSdfTarget = SlicingParameters::bracingSdfTarget;
        const float searchRadius = std::max(boundarySdfTarget, bracingSdfTarget)
            + SlicingParameters::postProcessSdfSearchPadding;
        const float searchRadius2 = searchRadius * searchRadius;
        const double angleThresholdRad = featureAngleThreshold * 3.14159265358979323846 / 180.0;

        auto buildClosedContourSequence = [](int graphId, zObjGraph& graph, zIntArray& sequence, bool& closed) {
            zFnGraph fnGraph(graph);
            zPointArray positions;
            zIntArray edgeConnects;
            fnGraph.getVertexPositions(positions);
            fnGraph.getEdgeData(edgeConnects);
            sequence.clear();
            closed = false;
            if (positions.size() < 3 || edgeConnects.size() < 4) return false;

            std::vector<zIntArray> adjacency(positions.size(), zIntArray());
            int invalidEdgeCount = 0;
            int zeroLengthEdgeCount = 0;
            int validEdgeCount = 0;
            for (int e = 0; e + 1 < static_cast<int>(edgeConnects.size()); e += 2) {
                const int a = edgeConnects[e];
                const int b = edgeConnects[e + 1];
                if (a < 0 || b < 0 || a >= static_cast<int>(positions.size()) || b >= static_cast<int>(positions.size())) {
                    invalidEdgeCount++;
                    continue;
                }
                zVector edge = positions[b] - positions[a];
                if (edge.length() < 1e-6) {
                    zeroLengthEdgeCount++;
                    continue;
                }
                adjacency[a].push_back(b);
                adjacency[b].push_back(a);
                validEdgeCount++;
            }

            int isolatedCount = 0;
            int degreeOneCount = 0;
            int degreeTwoCount = 0;
            int degreeMoreCount = 0;
            for (int v = 0; v < static_cast<int>(adjacency.size()); v++) {
                if (adjacency[v].empty()) isolatedCount++;
                else if (adjacency[v].size() == 1) degreeOneCount++;
                else if (adjacency[v].size() == 2) degreeTwoCount++;
                else degreeMoreCount++;
            }

            std::vector<bool> visited(positions.size(), false);
            int componentCount = 0;
            int closedComponentCount = 0;
            int largestComponentVertices = 0;
            int largestComponentEdges = 0;
            double bestPerimeter = -1.0;
            zIntArray bestSequence;

            for (int seed = 0; seed < static_cast<int>(positions.size()); seed++) {
                if (visited[seed] || adjacency[seed].empty()) continue;

                zIntArray componentVertices;
                std::vector<int> stack;
                stack.push_back(seed);
                visited[seed] = true;

                while (!stack.empty()) {
                    const int v = stack.back();
                    stack.pop_back();
                    componentVertices.push_back(v);
                    for (int neighbor : adjacency[v]) {
                        if (neighbor < 0 || neighbor >= static_cast<int>(positions.size())) continue;
                        if (visited[neighbor]) continue;
                        visited[neighbor] = true;
                        stack.push_back(neighbor);
                    }
                }

                componentCount++;
                int componentDegreeSum = 0;
                bool isValenceTwo = true;
                for (int v : componentVertices) {
                    componentDegreeSum += static_cast<int>(adjacency[v].size());
                    if (adjacency[v].size() != 2) isValenceTwo = false;
                }
                const int componentEdges = componentDegreeSum / 2;
                if (static_cast<int>(componentVertices.size()) > largestComponentVertices) {
                    largestComponentVertices = static_cast<int>(componentVertices.size());
                    largestComponentEdges = componentEdges;
                }

                if (!isValenceTwo || componentVertices.size() < 3) continue;

                zIntArray candidateSequence;
                int previous = -1;
                int current = componentVertices[0];
                bool candidateClosed = false;
                for (int safety = 0; safety < static_cast<int>(componentVertices.size()) + 2; safety++) {
                    candidateSequence.push_back(current);
                    const int next = (adjacency[current][0] == previous) ? adjacency[current][1] : adjacency[current][0];
                    previous = current;
                    current = next;
                    if (current == candidateSequence.front()) {
                        candidateClosed = true;
                        break;
                    }
                }

                if (!candidateClosed || candidateSequence.size() < 3) continue;

                closedComponentCount++;
                double perimeter = 0.0;
                for (int i = 0; i < static_cast<int>(candidateSequence.size()); i++) {
                    const int a = candidateSequence[i];
                    const int b = candidateSequence[(i + 1) % candidateSequence.size()];
                    zVector edge = positions[b] - positions[a];
                    perimeter += edge.length();
                }

                if (perimeter > bestPerimeter) {
                    bestPerimeter = perimeter;
                    bestSequence = candidateSequence;
                }
            }

            if (!bestSequence.empty()) {
                sequence = bestSequence;
                closed = true;
                const bool usedCleanup = componentCount != 1
                    || closedComponentCount != 1
                    || invalidEdgeCount > 0
                    || zeroLengthEdgeCount > 0
                    || degreeOneCount > 0
                    || degreeMoreCount > 0
                    || isolatedCount > 0;
                if (usedCleanup) {
                    std::cout << "[computeSDFPostProcess] graph " << graphId
                        << " using largest closed contour component vertices=" << sequence.size()
                        << " perimeter=" << bestPerimeter
                        << " components=" << componentCount
                        << " closedComponents=" << closedComponentCount
                        << " validEdges=" << validEdgeCount
                        << " zeroEdges=" << zeroLengthEdgeCount
                        << " invalidEdges=" << invalidEdgeCount
                        << " degree(0/1/2/>2)=" << isolatedCount << "/"
                        << degreeOneCount << "/" << degreeTwoCount << "/" << degreeMoreCount
                        << std::endl;
                }
                return true;
            }

            std::cout << "[computeSDFPostProcess] skipped graph " << graphId
                << ": no closed valence-2 contour component"
                << " vertices=" << positions.size()
                << " rawEdges=" << (edgeConnects.size() / 2)
                << " validEdges=" << validEdgeCount
                << " zeroEdges=" << zeroLengthEdgeCount
                << " invalidEdges=" << invalidEdgeCount
                << " components=" << componentCount
                << " closedComponents=" << closedComponentCount
                << " largestComponent(vertices/edges)=" << largestComponentVertices << "/" << largestComponentEdges
                << " degree(0/1/2/>2)=" << isolatedCount << "/"
                << degreeOneCount << "/" << degreeTwoCount << "/" << degreeMoreCount
                << std::endl;
            return false;
        };

        zFloatArray validHeights;
        for (int graphId = 0; graphId < layerCount; graphId++) {
            zObjGraph& sourceContourGraph = (graphId < static_cast<int>(debugData.flatContourGraphs.size()))
                ? debugData.flatContourGraphs[graphId]
                : contourGraphs[graphId];
            zFnGraph fnContour(sourceContourGraph);
            if (fnContour.numVertices() == 0 || fnContour.numEdges() == 0) continue;
            if (graphId >= static_cast<int>(debugData.finalFields.size())) continue;
            if (graphId >= static_cast<int>(debugData.fieldMeshes.size())) continue;
            if (graphId >= static_cast<int>(debugData.localFlattenedMeshes.size())) continue;

            zIntArray sequence;
            bool closed = false;
            if (!buildClosedContourSequence(graphId, sourceContourGraph, sequence, closed)) {
                continue;
            }

            zPointArray contourPositions;
            fnContour.getVertexPositions(contourPositions);

            zIntArray featureByContourVertex;
            featureByContourVertex.assign(contourPositions.size(), 0);
            for (int i = 0; i < static_cast<int>(sequence.size()); i++) {
                const int prevId = sequence[(i - 1 + sequence.size()) % sequence.size()];
                const int curId = sequence[i];
                const int nextId = sequence[(i + 1) % sequence.size()];
                zVector v0 = contourPositions[prevId] - contourPositions[curId];
                zVector v1 = contourPositions[nextId] - contourPositions[curId];
                if (v0.length() < 1e-6 || v1.length() < 1e-6) continue;
                v0.normalize();
                v1.normalize();
                double dot = (v0.x * v1.x) + (v0.y * v1.y) + (v0.z * v1.z);
                dot = std::max(-1.0, std::min(1.0, dot));
                const double turnAngle = 3.14159265358979323846 - acos(dot);
                if (turnAngle >= angleThresholdRad) featureByContourVertex[curId] = 1;
            }

            zPointArray fieldPositions;
            zFnMesh fnFieldMesh(debugData.fieldMeshes[graphId]);
            zPoint* rawFieldPositions = fnFieldMesh.getRawVertexPositions();
            for (int i = 0; i < fnFieldMesh.numVertices(); i++) fieldPositions.push_back(rawFieldPositions[i]);
            zScalarArray& fieldValues = debugData.finalFields[graphId];

            auto getPrintWidth = [&](zPoint& p) -> float {
                float minValue = 0.0f;
                bool foundNegative = false;
                const int n = std::min(static_cast<int>(fieldPositions.size()), static_cast<int>(fieldValues.size()));
                for (int i = 0; i < n; i++) {
                    if ((fieldPositions[i] - p).length2() > searchRadius2) continue;
                    if (fieldValues[i] < minValue) {
                        minValue = static_cast<float>(fieldValues[i]);
                        foundNegative = true;
                    }
                }
                if (!foundNegative) return 0.0f;

                const float sdfMagnitude = fabs(minValue);
                const float dBoundary = fabs(sdfMagnitude - boundarySdfTarget);
                const float dBracing = fabs(sdfMagnitude - bracingSdfTarget);
                return (dBoundary <= dBracing) ? printBoundaryWidth : printBracingWidth;
            };

            zPointArray sampledPoints;
            zFloatArray sampledWidths;
            zIntArray sampledFeatureFlags;
            auto appendSample = [&](zPoint p, int featureFlag) {
                if (!sampledPoints.empty() && sampledPoints.back().distanceTo(p) < 0.0001) {
                    sampledFeatureFlags.back() = std::max(sampledFeatureFlags.back(), featureFlag);
                    return;
                }
                const float printWidth = getPrintWidth(p);
                if (printWidth == 0.0f) {
                    std::cout << "[computeSDFPostProcess] graph " << graphId
                        << " no negative SDF width sample near target" << std::endl;
                }
                sampledPoints.push_back(p);
                sampledWidths.push_back(printWidth);
                sampledFeatureFlags.push_back(featureFlag);
            };

            appendSample(contourPositions[sequence[0]], featureByContourVertex[sequence[0]]);
            float distanceSinceLastSample = 0.0f;
            const int segmentCount = closed ? static_cast<int>(sequence.size()) : static_cast<int>(sequence.size()) - 1;
            for (int i = 0; i < segmentCount; i++) {
                const int id0 = sequence[i];
                const int id1 = sequence[(i + 1) % sequence.size()];
                zPoint p0 = contourPositions[id0];
                zPoint p1 = contourPositions[id1];
                zVector segVec = p1 - p0;
                const float segLen = segVec.length();
                if (segLen < 1e-6) continue;
                segVec.normalize();

                float walkedOnSegment = 0.0f;
                while (sampleLength > 0.0f && distanceSinceLastSample + (segLen - walkedOnSegment) >= sampleLength) {
                    const float step = sampleLength - distanceSinceLastSample;
                    walkedOnSegment += step;
                    appendSample(p0 + (segVec * walkedOnSegment), 0);
                    distanceSinceLastSample = 0.0f;
                }

                distanceSinceLastSample += (segLen - walkedOnSegment);
                if (featureByContourVertex[id1] == 1) {
                    appendSample(p1, 1);
                    distanceSinceLastSample = 0.0f;
                }
            }

            if (closed && sampledPoints.size() > 1 && sampledPoints.front().distanceTo(sampledPoints.back()) < 0.0001) {
                sampledPoints.pop_back();
                sampledWidths.pop_back();
                sampledFeatureFlags.pop_back();
            }
            if (sampledPoints.size() < 2) continue;

            if (sampledWidths.size() > 2) {
                for (int i = 0; i < static_cast<int>(sampledWidths.size()); i++) {
                    const int prevId = (i == 0) ? (closed ? static_cast<int>(sampledWidths.size()) - 1 : 0) : i - 1;
                    const int nextId = (i == static_cast<int>(sampledWidths.size()) - 1) ? (closed ? 0 : static_cast<int>(sampledWidths.size()) - 1) : i + 1;
                    if (prevId == i || nextId == i) continue;
                    const float prevWidth = sampledWidths[prevId];
                    const float currentWidth = sampledWidths[i];
                    const float nextWidth = sampledWidths[nextId];
                    if (prevWidth <= 0.0f || nextWidth <= 0.0f) continue;
                    if (fabs(prevWidth - nextWidth) > 0.0001f) continue;
                    if (fabs(currentWidth - prevWidth) <= 0.0001f) continue;
                    sampledWidths[i] = prevWidth;
                }
            }

            zIntArray edgeConnects;
            for (int i = 0; i + 1 < static_cast<int>(sampledPoints.size()); i++) {
                edgeConnects.push_back(i);
                edgeConnects.push_back(i + 1);
            }
            if (closed && sampledPoints.size() > 2) {
                edgeConnects.push_back(static_cast<int>(sampledPoints.size()) - 1);
                edgeConnects.push_back(0);
            }

            zFnGraph fnToolpath(result.toolpathGraphs[graphId]);
            fnToolpath.clear();
            fnToolpath.create(sampledPoints, edgeConnects);
            zFnGraph fnFlatToolpath(result.flatToolpathGraphs[graphId]);
            fnFlatToolpath.clear();
            fnFlatToolpath.create(sampledPoints, edgeConnects);
            result.flatToolpathTargetPoints[graphId] = sampledPoints;

            zVectorArray mappedNormals;
            if (graphId < static_cast<int>(sectionMeshes.size())) {
                barycentericProjection_triMesh(result.toolpathGraphs[graphId], debugData.localFlattenedMeshes[graphId], sectionMeshes[graphId], &mappedNormals);
                zFnGraph fnMappedToolpath(result.toolpathGraphs[graphId]);
                fnMappedToolpath.getVertexPositions(sampledPoints);
            }
            if (mappedNormals.size() != sampledPoints.size()) mappedNormals.assign(sampledPoints.size(), zVector(0, 0, 1));

            result.toolpathTargetPoints[graphId] = sampledPoints;
            result.toolpathPrintWidths[graphId] = sampledWidths;
            result.toolpathFeatureFlags[graphId] = sampledFeatureFlags;
            result.toolpathNormals[graphId] = mappedNormals;
            result.toolpathPrintHeights[graphId].assign(sampledPoints.size(), 0.0f);

            const int nextId = graphId + 1;
            if (nextId >= static_cast<int>(sectionMeshes.size())) continue;
            zFloatArray& heights = result.toolpathPrintHeights[graphId];
            for (int sampleId = 0; sampleId < static_cast<int>(sampledPoints.size()); sampleId++) {
                zVector baseRayDir = mappedNormals[sampleId];
                if (baseRayDir.length() < 1e-6) continue;
                baseRayDir.normalize();

                float closestDistance = std::numeric_limits<float>::max();
                bool found = false;
                for (int dId = 0; dId < 2; dId++) {
                    zVector rayDir = (dId == 0) ? baseRayDir : baseRayDir * -1.0f;
                    for (zItMeshFace f(sectionMeshes[nextId]); !f.end(); f++) {
                        zPointArray fVerts;
                        f.getVertexPositions(fVerts);
                        if (fVerts.size() < 3) continue;
                        for (int tri = 1; tri < static_cast<int>(fVerts.size()) - 1; tri++) {
                            zPoint cP;
                            bool hit = coreUtils().ray_triangleIntersection(fVerts[0], fVerts[tri], fVerts[tri + 1], rayDir, sampledPoints[sampleId], cP);
                            if (!hit) continue;
                            const float d = cP.distanceTo(sampledPoints[sampleId]);
                            if (d < closestDistance) {
                                closestDistance = d;
                                found = true;
                            }
                        }
                    }
                }
                if (found) {
                    heights[sampleId] = closestDistance;
                    if (closestDistance > 0.0f) validHeights.push_back(closestDistance);
                }
            }
        }

        float averageHeight = 0.0f;
        if (!validHeights.empty()) {
            for (float h : validHeights) averageHeight += h;
            averageHeight /= static_cast<float>(validHeights.size());
        }
        for (int i = 0; i < layerCount; i++) {
            if (i + 1 < static_cast<int>(sectionMeshes.size())) continue;
            result.toolpathPrintHeights[i].assign(result.toolpathTargetPoints[i].size(), averageHeight);
        }
    }

} // namespace alice2

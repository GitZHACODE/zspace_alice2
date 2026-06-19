#include "zUnroller.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

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
        if (medialIds.size() < 2 || featuredNumStrides.empty()) return false;

        zIntArray medial = medialIds;
        zIntArray strides = featuredNumStrides;
        zVector normal(0, 0, 1);
        computeVLoops(mesh, medial, strides, normal, loops, topMesh, bottomMesh);
        computeGeodesicScalars(mesh, loops, scalars, true);
        computeGeodesicContours(loops, scalars, 0.01f, topMesh, bottomMesh, sectionMeshes);
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

        zItMeshHalfEdge startHe;
        bool found = false;
        for (zItMeshHalfEdge he(mesh); !he.end(); he++) {
            if (he.onBoundary()) {
                startHe = he;
                found = true;
                break;
            }
        }
        if (!found) return;

        zItMeshHalfEdge he = startHe;
        positions.push_back(he.getStartVertex().getPosition());
        colors.push_back(he.getStartVertex().getColor());
        do {
            positions.push_back(he.getVertex().getPosition());
            colors.push_back(he.getVertex().getColor());
            edgeConnects.push_back(static_cast<int>(positions.size()) - 2);
            edgeConnects.push_back(static_cast<int>(positions.size()) - 1);
            he = he.getNext();
        } while (he != startHe);

        if (closeGraph && positions.size() > 1) {
            edgeConnects.push_back(static_cast<int>(positions.size()) - 1);
            edgeConnects.push_back(0);
        }

        zFnGraph fnGraph(graph);
        fnGraph.clear();
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

    void barycentericProjection_triMesh(zObjGraph& graph, zObjMesh& inMesh, zObjMesh& projectionMesh)
    {
        zFnGraph fnGraph(graph);
        zPointArray positions;
        fnGraph.getVertexPositions(positions);

        for (auto& p : positions) {
            for (zItMeshFace f(inMesh); !f.end(); f++) {
                zPointArray faceVerts;
                f.getVertexPositions(faceVerts);
                if (faceVerts.size() < 3) continue;
                if (!coreUtils().pointInTriangle(p, faceVerts[0], faceVerts[1], faceVerts[2])) continue;

                zPoint bary;
                getBaryCentricCoordinates_triangle(p, faceVerts[0], faceVerts[1], faceVerts[2], bary);

                zItMeshFace projectionFace(projectionMesh, f.getId());
                zPointArray projectionVerts;
                projectionFace.getVertexPositions(projectionVerts);
                if (projectionVerts.size() >= 3) getProjectionPoint_triangle(bary, projectionVerts[0], projectionVerts[1], projectionVerts[2], p);
                break;
            }
        }

        fnGraph.setVertexPositions(positions);
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
        computeVLoops(mesh, medialIds, featuredNumStrides, norm, loops, topMeshObj, bottomMeshObj);
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

    // void computeVLoops(zObjMesh& mesh, zIntArray& medialIds, zIntArray& featuredNumStrides, zVector& norm, std::vector<zItMeshHalfEdgeArray>& loops, zObjMesh& topMeshObj, zObjMesh& bottomMeshObj)
    // {
    //     loops.clear();
    //     if (medialIds.size() < 2 || featuredNumStrides.empty()) return;

    //     const int stride = std::max(1, featuredNumStrides[0]);
    //     const int startVID = medialIds[0];
    //     const int endVID = medialIds[1];

    //     zItMeshVertex vStart(mesh, startVID);
    //     zItMeshVertex vEnd(mesh, endVID);
    //     zVector dir = vEnd.getPosition() - vStart.getPosition();

    //     zItMeshHalfEdgeArray hEdgesStart;
    //     vStart.getConnectedHalfEdges(hEdgesStart);
    //     if (hEdgesStart.empty()) return;

    //     float minAngle = std::numeric_limits<float>::max();
    //     zItMeshHalfEdge heStart = hEdgesStart[0];
    //     for (auto& he : hEdgesStart) {
    //         const float angle = he.getVector().angle(dir);
    //         if (angle < minAngle) {
    //             minAngle = angle;
    //             heStart = he;
    //         }
    //     }

    //     zItMeshHalfEdge he = heStart;
    //     norm.normalize();

    //     zItMeshHalfEdge heBottom;
    //     zItMeshHalfEdge heTop;
    //     int vCounter = 0;
    //     int tempCounter = 0;
    //     do {
    //         zVector faceNormal = he.getFace().getNormal();
    //         faceNormal.normalize();

    //         if (norm * faceNormal > 0.98) {
    //             vCounter = tempCounter;
    //             heTop = he;
    //         }

    //         if (norm * faceNormal < -0.98) {
    //             heBottom = he;
    //         }

    //         he = he.getNext().getSym().getNext();
    //         tempCounter++;
    //     } while (he != heStart);

    //     if (vCounter <= 0) return;

    //     zPointArray positionsTop;
    //     zPointArray positionsBottom;
    //     zIntArray countsTop;
    //     zIntArray countsBottom;
    //     zIntArray connectsTop;
    //     zIntArray connectsBottom;

    //     zFnMesh fnMesh(mesh);
    //     zIntArray pMapBottom(fnMesh.numVertices(), -1);
    //     zIntArray pMapTop(fnMesh.numVertices(), -1);

    //     bool corner = true;
    //     for (int i = 0; i < stride; i++) {
    //         heTop = heTop.getNext().getNext();
    //         heBottom = heBottom.getNext().getNext();

    //         if ((i + 1) % stride != 0) {
    //             heBottom = heBottom.getSym();
    //             heTop = heTop.getSym();
    //         }
    //     }

    //     zItMeshHalfEdge heWalkBottom = heBottom;
    //     int walkCounter = 0;
    //     do {
    //         if (corner) {
    //             const int loopId = static_cast<int>(loops.size());
    //             getLoop(heWalkBottom, true, corner, vCounter, loops);
    //             corner = false;

    //             if (!loops[loopId].empty()) {
    //                 pMapBottom[loops[loopId][0].getVertex().getId()] = static_cast<int>(positionsBottom.size());
    //                 positionsBottom.push_back(loops[loopId][0].getVertex().getPosition());

    //                 pMapTop[loops[loopId].back().getStartVertex().getId()] = static_cast<int>(positionsTop.size());
    //                 positionsTop.push_back(loops[loopId].back().getStartVertex().getPosition());
    //             }
    //         }

    //         const int loopId = static_cast<int>(loops.size());
    //         getLoop(heWalkBottom, true, corner, vCounter, loops);
    //         if (!loops[loopId].empty()) {
    //             pMapBottom[loops[loopId][0].getVertex().getId()] = static_cast<int>(positionsBottom.size());
    //             positionsBottom.push_back(loops[loopId][0].getVertex().getPosition());

    //             pMapTop[loops[loopId].back().getStartVertex().getId()] = static_cast<int>(positionsTop.size());
    //             positionsTop.push_back(loops[loopId].back().getStartVertex().getPosition());
    //         }

    //         heWalkBottom = heWalkBottom.getNext().getNext();
    //         if ((walkCounter + 1) % (2 * stride) != 0) heWalkBottom = heWalkBottom.getSym();
    //         else corner = true;
    //         walkCounter++;
    //     } while (heWalkBottom != heBottom);

    //     for (int i = 0; i < stride * 2; i++) {
    //         zIntArray faceVerts;
    //         getFaceVerticesFromHalfedge(heBottom, true, faceVerts);
    //         bool valid = true;
    //         for (auto id : faceVerts) {
    //             if (id < 0 || id >= static_cast<int>(pMapBottom.size()) || pMapBottom[id] < 0) valid = false;
    //         }
    //         if (valid) {
    //             for (auto id : faceVerts) connectsBottom.push_back(pMapBottom[id]);
    //             countsBottom.push_back(static_cast<int>(faceVerts.size()));
    //         }

    //         faceVerts.clear();
    //         getFaceVerticesFromHalfedge(heTop, true, faceVerts);
    //         valid = true;
    //         for (auto id : faceVerts) {
    //             if (id < 0 || id >= static_cast<int>(pMapTop.size()) || pMapTop[id] < 0) valid = false;
    //         }
    //         if (valid) {
    //             for (auto id : faceVerts) connectsTop.push_back(pMapTop[id]);
    //             countsTop.push_back(static_cast<int>(faceVerts.size()));
    //         }

    //         heBottom = heBottom.getNext().getNext().getSym();
    //         heTop = heTop.getNext().getNext().getSym();
    //     }

    //     zFnMesh fnTop(topMeshObj);
    //     fnTop.clear();
    //     if (!positionsTop.empty()) fnTop.create(positionsTop, countsTop, connectsTop);

    //     zFnMesh fnBottom(bottomMeshObj);
    //     fnBottom.clear();
    //     if (!positionsBottom.empty()) fnBottom.create(positionsBottom, countsBottom, connectsBottom);
    // }
    
    void computeVLoops(zObjMesh& mesh, zIntArray& longitudeCornerVIds, zVector& norm, std::vector<zItMeshHalfEdgeArray>& loops, zObjMesh& topMeshObj, zObjMesh& bottomMeshObj)
    {
        loops.clear();
        if (longitudeCornerVIds.size() < 2 ) return;

        // const int stride = std::max(1, featuredNumStrides[0]);
        const int startVID = longitudeCornerVIds[0];
        const int endVID = longitudeCornerVIds[1];

        zItMeshVertex vStart(mesh, startVID);
        zItMeshVertex vEnd(mesh, endVID);
        zVector dir = vEnd.getPosition() - vStart.getPosition();

        zItMeshHalfEdgeArray hEdgesStart;
        vStart.getConnectedHalfEdges(hEdgesStart);
        if (hEdgesStart.empty()) return;

        float maxAngle = std::numeric_limits<float>::min();
        zItMeshHalfEdge heStart = hEdgesStart[0];
        for (auto& he : hEdgesStart) {
            //should be checking abs(cos) --> get the longtitude
            const float dot = he.getVector() * dir;
            const float angle = std::acos(dot / (he.getVector().length() * dir.length()));
            // const float angle = he.getVector().angle(dir);
            if (angle > maxAngle) {
                maxAngle = angle;
                heStart = he;
            }
        }

        zItMeshHalfEdge he = heStart;
        norm.normalize();

        zItMeshHalfEdge heBottom;
        zItMeshHalfEdge heTop;
        int vCounter = 0;
        int tempCounter = 0;
        do {
            zVector faceNormal = he.getFace().getNormal();
            faceNormal.normalize();

            if (norm * faceNormal > 0.98) {
                vCounter = tempCounter;
                heTop = he;
            }

            if (norm * faceNormal < -0.98) {
                heBottom = he;
            }

            he = he.getNext().getSym().getNext();
            tempCounter++;
        } while (he != heStart);

        if (vCounter <= 0) return;

        zPointArray positionsTop;
        zPointArray positionsBottom;
        zIntArray countsTop;
        zIntArray countsBottom;
        zIntArray connectsTop;
        zIntArray connectsBottom;

        zFnMesh fnMesh(mesh);
        zIntArray pMapBottom(fnMesh.numVertices(), -1);
        zIntArray pMapTop(fnMesh.numVertices(), -1);

        bool corner = true;
        for (int i = 0; i < stride; i++) {
            heTop = heTop.getNext().getNext();
            heBottom = heBottom.getNext().getNext();

            if ((i + 1) % stride != 0) {
                heBottom = heBottom.getSym();
                heTop = heTop.getSym();
            }
        }

        zItMeshHalfEdge heWalkBottom = heBottom;
        int walkCounter = 0;
        do {
            if (corner) {
                const int loopId = static_cast<int>(loops.size());
                getLoop(heWalkBottom, true, corner, vCounter, loops);
                corner = false;

                if (!loops[loopId].empty()) {
                    pMapBottom[loops[loopId][0].getVertex().getId()] = static_cast<int>(positionsBottom.size());
                    positionsBottom.push_back(loops[loopId][0].getVertex().getPosition());

                    pMapTop[loops[loopId].back().getStartVertex().getId()] = static_cast<int>(positionsTop.size());
                    positionsTop.push_back(loops[loopId].back().getStartVertex().getPosition());
                }
            }

            const int loopId = static_cast<int>(loops.size());
            getLoop(heWalkBottom, true, corner, vCounter, loops);
            if (!loops[loopId].empty()) {
                pMapBottom[loops[loopId][0].getVertex().getId()] = static_cast<int>(positionsBottom.size());
                positionsBottom.push_back(loops[loopId][0].getVertex().getPosition());

                pMapTop[loops[loopId].back().getStartVertex().getId()] = static_cast<int>(positionsTop.size());
                positionsTop.push_back(loops[loopId].back().getStartVertex().getPosition());
            }

            heWalkBottom = heWalkBottom.getNext().getNext();
            if ((walkCounter + 1) % (2 * stride) != 0) heWalkBottom = heWalkBottom.getSym();
            else corner = true;
            walkCounter++;
        } while (heWalkBottom != heBottom);

        for (int i = 0; i < stride * 2; i++) {
            zIntArray faceVerts;
            getFaceVerticesFromHalfedge(heBottom, true, faceVerts);
            bool valid = true;
            for (auto id : faceVerts) {
                if (id < 0 || id >= static_cast<int>(pMapBottom.size()) || pMapBottom[id] < 0) valid = false;
            }
            if (valid) {
                for (auto id : faceVerts) connectsBottom.push_back(pMapBottom[id]);
                countsBottom.push_back(static_cast<int>(faceVerts.size()));
            }

            faceVerts.clear();
            getFaceVerticesFromHalfedge(heTop, true, faceVerts);
            valid = true;
            for (auto id : faceVerts) {
                if (id < 0 || id >= static_cast<int>(pMapTop.size()) || pMapTop[id] < 0) valid = false;
            }
            if (valid) {
                for (auto id : faceVerts) connectsTop.push_back(pMapTop[id]);
                countsTop.push_back(static_cast<int>(faceVerts.size()));
            }

            heBottom = heBottom.getNext().getNext().getSym();
            heTop = heTop.getNext().getNext().getSym();
        }

        zFnMesh fnTop(topMeshObj);
        fnTop.clear();
        if (!positionsTop.empty()) fnTop.create(positionsTop, countsTop, connectsTop);

        zFnMesh fnBottom(bottomMeshObj);
        fnBottom.clear();
        if (!positionsBottom.empty()) fnBottom.create(positionsBottom, countsBottom, connectsBottom);
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

    void computeSDF(zObjGraphArray& sectionGraphs, zObjMeshArray& sectionMeshes, zObjGraphArray& contourGraphs)
    {
        contourGraphs.clear();
        contourGraphs.assign(sectionGraphs.size(), zObjGraph());

        for (int i = 0; i < static_cast<int>(sectionGraphs.size()) && i < static_cast<int>(sectionMeshes.size()); i++) {
            zObjMesh projectionMesh;
            getPokeMesh(sectionMeshes[i], projectionMesh);

            zObjMesh flattenedMesh;
            zInt2DArray oriVertexUnrollVertexMap;
            std::unordered_map<zIntPair, int, zPairHash> oriFaceVertexUnrollVertex;
            zItGraphVertexArray bsfVertices;
            zIntPairArray bsfVertexPairs;
            zObjGraph dualGraph;

            creatUnrollMesh(projectionMesh, flattenedMesh, dualGraph, oriVertexUnrollVertexMap, oriFaceVertexUnrollVertex, bsfVertices, bsfVertexPairs);
            unrollMesh(projectionMesh, flattenedMesh, dualGraph, oriVertexUnrollVertexMap, oriFaceVertexUnrollVertex, bsfVertexPairs);
            mergeMesh(flattenedMesh);

            zObjMesh offsetMesh;
            getBoundaryOffset(flattenedMesh, false, 0.03f, offsetMesh);
            createBoundaryEdgeGraph(offsetMesh, true, contourGraphs[i]);
            barycentericProjection_triMesh(contourGraphs[i], flattenedMesh, projectionMesh);

            zFnGraph fnGraph(contourGraphs[i]);
            fnGraph.setEdgeColor(zColor(0, 0, 1, 1));
            fnGraph.setEdgeWeight(3);
        }
    }

} // namespace alice2

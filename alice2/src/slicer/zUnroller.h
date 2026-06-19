#pragma once

#ifndef ALICE2_ZUNROLLER_H
#define ALICE2_ZUNROLLER_H

#include <zspace/interface.h>
#include <zspace/io.h>
#include <zspace/zInterface/objects/zObjGraph.h>
#include <zspace/zInterface/objects/zObjMesh.h>

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace alice2 {

    struct zPairHash {
        template <class T1, class T2>
        std::size_t operator()(const std::pair<T1, T2>& p) const
        {
            const auto h1 = std::hash<T1>{}(p.first);
            const auto h2 = std::hash<T2>{}(p.second);
            return (h1 != h2) ? (h1 ^ h2) : h1;
        }
    };

    bool loadMesh(const std::string& path, zSpace::zObjMesh& mesh, std::string* message = nullptr);
    bool buildSections(zSpace::zObjMesh& mesh, const zSpace::zIntArray& medialIds, const zSpace::zIntArray& featuredNumStrides,
        std::vector<zSpace::zItMeshHalfEdgeArray>& loops, zSpace::zObjMesh& topMesh, zSpace::zObjMesh& bottomMesh,
        zSpace::zScalarArray& scalars, zSpace::zObjMeshArray& sectionMeshes,
        zSpace::zObjGraphArray& sectionGraphs, zSpace::zObjGraphArray& contourGraphs);

    void getBoundaryOffset(zSpace::zObjMesh& inMesh, bool keepExistingFaces, float offset, zSpace::zObjMesh& outMesh);
    void setPtGraph(zSpace::zObjGraph& graph, zSpace::zPoint& refPt, bool setX, bool setY, bool setZ);
    void setPtMesh(zSpace::zObjMesh& mesh, zSpace::zPoint& refPt, bool setX, bool setY, bool setZ);
    void getFaceVerticesFromHalfedge(zSpace::zItMeshHalfEdge& heStart, bool forward, zSpace::zPointArray& faceVerts);
    void getFaceVerticesFromHalfedge(zSpace::zItMeshHalfEdge& heStart, bool forward, zSpace::zIntArray& faceVerts);
    void getLoop(zSpace::zItMeshHalfEdge& heStart, bool forward, bool corner, int vCounter, std::vector<zSpace::zItMeshHalfEdgeArray>& loops);
    void colorMesh(zSpace::zObjMesh& mesh, zSpace::zFloatArray& scalars);
    zSpace::zPoint getContourPosition(float threshold, zSpace::zVector& vertexLower, zSpace::zVector& vertexHigher, float thresholdLow, float thresholdHigh);
    void getPokeMesh(zSpace::zObjMesh& mesh, zSpace::zObjMesh& triMesh);
    void closestPointsToMesh(zSpace::zPointArray& inPoints, zSpace::zObjMesh mesh, zSpace::zIntArray& faceIds, zSpace::zPointArray& closestPoints);
    void createBoundaryEdgeGraph(zSpace::zObjMesh& mesh, bool closeGraph, zSpace::zObjGraph& graph);
    void UVParametrisation(zSpace::zObjMesh mesh, zSpace::zObjMesh& paramMesh);
    void getBaryCentricCoordinates_triangle(zSpace::zPoint& pt, zSpace::zPoint& t0, zSpace::zPoint& t1, zSpace::zPoint& t2, zSpace::zPoint& baryCoordinates);
    void getProjectionPoint_triangle(zSpace::zPoint& baryCoordinates, zSpace::zPoint& t0, zSpace::zPoint& t1, zSpace::zPoint& t2, zSpace::zPoint& projectionPt);
    void barycentericProjection_triMesh(zSpace::zObjGraph& graph, zSpace::zObjMesh& inMesh, zSpace::zObjMesh& projectionMesh);
    void computeDualGraph_BST(zSpace::zObjMesh& mesh, zSpace::zObjGraph& graph, zSpace::zItGraphVertexArray& bsfVertices, zSpace::zIntPairArray& bsfVertexPairs);
    zSpace::zIntPair getCommonEdge(zSpace::zItMeshFace& f1, zSpace::zItMeshFace& f2);
    void creatUnrollMesh(zSpace::zObjMesh& mesh, zSpace::zObjMesh& unrollMesh, zSpace::zObjGraph& dualGraph, zSpace::zInt2DArray& oriVertexUnrollVertexMap, std::unordered_map<zSpace::zIntPair, int, zPairHash>& oriFaceVertexUnrollVertex, zSpace::zItGraphVertexArray& bsfVertices, zSpace::zIntPairArray& bsfVertexPairs);
    void unrollMesh(zSpace::zObjMesh& mesh, zSpace::zObjMesh& unrollMesh, zSpace::zObjGraph& dualGraph, zSpace::zInt2DArray& oriVertexUnrollVertexMap, std::unordered_map<zSpace::zIntPair, int, zPairHash>& oriFaceVertexUnrollVertex, zSpace::zIntPairArray& bsfVertexPairs);
    void mergeMesh(zSpace::zObjMesh& mesh);
    void createShapes(zSpace::zObjMesh& mesh, zSpace::zIntArray& medialIds, zSpace::zIntArray& featuredNumStrides, zSpace::zVector& norm, float spacing, int& numFrames, zSpace::zObjMesh& topMesh, zSpace::zObjMesh& bottomMesh);
    void blendShapes(zSpace::zObjMesh& shape0, zSpace::zObjMesh& shape1, int numFrames, zSpace::zObjMeshArray& meshes);
    void computeVLoops(zSpace::zObjMesh& mesh, zSpace::zIntArray& medialIds, zSpace::zIntArray& featuredNumStrides, zSpace::zVector& norm, std::vector<zSpace::zItMeshHalfEdgeArray>& loops, zSpace::zObjMesh& topMesh, zSpace::zObjMesh& bottomMesh);
    void computeGeodesicScalars(zSpace::zObjMesh& mesh, std::vector<zSpace::zItMeshHalfEdgeArray>& loops, zSpace::zScalarArray& scalars, bool normalise);
    void computeGeodesicContours(std::vector<zSpace::zItMeshHalfEdgeArray>& loops, zSpace::zScalarArray& scalars, float spacing, zSpace::zObjMesh& topMesh, zSpace::zObjMesh& bottomMesh, zSpace::zObjMeshArray& meshes);
    void computeGeodesicContours(zSpace::zObjMesh& mesh, zSpace::zFloatArray& scalars, float spacing, zSpace::zObjGraphArray& contourGraphs);
    void createSectionGraphs(zSpace::zObjMeshArray& meshes, zSpace::zObjGraphArray& sectionGraphs);
    void computeSDF(zSpace::zObjGraphArray& sectionGraphs, zSpace::zObjMeshArray& sectionMeshes, zSpace::zObjGraphArray& contourGraphs);

} // namespace alice2

#endif // ALICE2_ZUNROLLER_H

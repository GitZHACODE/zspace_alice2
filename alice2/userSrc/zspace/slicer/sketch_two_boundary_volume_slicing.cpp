#define __MAIN__
#ifdef __MAIN__

#include <zspace/interface.h>

#include <alice2.h>
#include <sketches/SketchRegistry.h>
#include <slicer/zUnroller.h>

#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <Eigen/SparseCholesky>
#include <tetgen.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace alice2;

namespace {

enum class BoundaryTag : int {
    Bottom = 10,
    Top = 20,
    Side = 30
};

struct TestTetMesh {
    zSpace::zPointArray vertices;
    std::vector<std::array<int, 4>> tets;
    zSpace::zIntArray bottomVertexIds;
    zSpace::zIntArray topVertexIds;
};

struct VolumeSliceDebug {
    zSpace::zFloatArray phi;
    zSpace::zObjGraph tetWireGraph;
    zSpace::zObjGraph scalarPointGraph;
    zSpace::zObjMeshArray isoMeshes;
};

using EdgeKey = std::pair<int, int>;

EdgeKey sortedEdge(int a, int b)
{
    return (a < b) ? EdgeKey(a, b) : EdgeKey(b, a);
}

} // namespace

class TwoBoundaryVolumeSlicingSketch : public ISketch {
public:
    std::string getName() const override { return "Two-Boundary Volume Slicing Test"; }
    std::string getDescription() const override { return "Builds a conforming TetGen volume and harmonic field between computed boundaries."; }
    std::string getAuthor() const override { return "alice2 + zspace_core"; }

    void setup() override
    {
        scene().setBackgroundColor(Color(1.0f, 1.0f, 1.0f, 1.0f));
        scene().setShowGrid(true);
        scene().setGridSize(8.0f);
        scene().setGridDivisions(8);
        scene().setShowAxes(true);
        scene().setAxesLength(1.5f);
        readInputMesh();

        const char* autoRun = std::getenv("ALICE2_TWO_BOUNDARY_AUTORUN");
        if (autoRun && std::string(autoRun) == "1") {
            std::cout << "[TwoBoundaryVolume][autorun] running t, v, h, m" << std::endl;
            deriveBoundaries();
            buildTetVolume();
            computeScalarField();
            extractIsoMeshes();
        }
    }

    void update(float) override {}

    void draw(Renderer& renderer, Camera&) override
    {
        switch (m_debugMode) {
        case 0: drawInput(); break;
        case 1: drawBoundaries(); break;
        case 2: drawLongitudeLoops(); break;
        case 3: drawTetWire(); break;
        case 4: drawScalarPoints(renderer); break;
        case 5: drawCurrentIso(); break;
        case 6: drawAllIsos(); break;
        case 7: drawComparison(); break;
        default: break;
        }

        renderer.setColor(Color(0.08f, 0.09f, 0.12f, 1.0f));
        renderer.drawString(getName(), 10, 28);
        renderer.drawString("Mode " + std::to_string(m_debugMode) + ": " + modeName(), 10, 50);
        renderer.drawString(m_status, 10, 72);
        renderer.drawString("r reset | t boundaries | v tet volume | h scalar | m marching tets | d debug mode | [ ] slice", 10, 94);
    }

    bool onKeyPress(unsigned char key, int, int) override
    {
        switch (key) {
        case 'r': case 'R': readInputMesh(); return true;
        case 't': case 'T': deriveBoundaries(); return true;
        case 'v': case 'V': buildTetVolume(); return true;
        case 'h': case 'H': computeScalarField(); return true;
        case 'm': case 'M': extractIsoMeshes(); return true;
        case 'd': case 'D': cycleDebugMode(); return true;
        case '[': case '{': changeSlice(-1); return true;
        case ']': case '}': changeSlice(1); return true;
        default: return false;
        }
    }

private:
    zSpace::zObjMesh m_inputMesh;
    zSpace::zObjMesh m_topMesh;
    zSpace::zObjMesh m_bottomMesh;
    std::vector<zSpace::zItMeshHalfEdgeArray> m_loops;
    zSpace::zObjGraph m_longitudeGraph;
    alice2::SliceMetadata m_metadata;
    zSpace::zFloatArray m_oldScalars;
    zSpace::zObjMeshArray m_oldSectionMeshes;
    TestTetMesh m_tetMesh;
    VolumeSliceDebug m_debug;
    zSpace::zIntArray m_boundaryTags;
    std::vector<float> m_isoValues;
    std::string m_status = "Waiting for input.";
    int m_debugMode = 0;
    int m_currentSlice = 0;
    bool m_loaded = false;
    bool m_haveBoundaries = false;
    bool m_haveVolume = false;
    bool m_havePhi = false;

    static Vec3 toVec3(const zSpace::zVector& p)
    {
        return Vec3(static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z));
    }

    static double triangleArea2(zSpace::zPoint a, zSpace::zPoint b, zSpace::zPoint c)
    {
        return ((b - a) ^ (c - a)).length();
    }

    void clearGraph(zSpace::zObjGraph& graph)
    {
        zSpace::zFnGraph fn(graph);
        fn.clear();
    }

    void clearMesh(zSpace::zObjMesh& mesh)
    {
        zSpace::zFnMesh fn(mesh);
        fn.clear();
    }

    void clearDerived()
    {
        m_loops.clear();
        m_metadata = alice2::SliceMetadata();
        m_oldScalars.clear();
        m_oldSectionMeshes.clear();
        clearMesh(m_topMesh);
        clearMesh(m_bottomMesh);
        clearGraph(m_longitudeGraph);
        m_tetMesh = TestTetMesh();
        m_debug.phi.clear();
        clearGraph(m_debug.tetWireGraph);
        clearGraph(m_debug.scalarPointGraph);
        m_debug.isoMeshes.clear();
        m_boundaryTags.clear();
        m_isoValues.clear();
        m_currentSlice = 0;
        m_haveBoundaries = false;
        m_haveVolume = false;
        m_havePhi = false;
    }

    void readInputMesh()
    {
        clearDerived();
        clearMesh(m_inputMesh);
        std::string message;
        m_loaded = alice2::loadMesh(alice2::SlicingParameters::inputMeshPath, m_inputMesh, &message);
        if (!m_loaded) {
            m_status = "Input load failed: " + message;
            std::cout << "[TwoBoundaryVolume] " << m_status << std::endl;
            return;
        }

        zSpace::zFnMesh fn(m_inputMesh);
        std::ostringstream out;
        out << "Input vertices=" << fn.numVertices() << " faces=" << fn.numPolygons();
        m_status = out.str();
        m_debugMode = 0;
        std::cout << "[TwoBoundaryVolume][r] " << m_status
            << " path=" << alice2::SlicingParameters::inputMeshPath << std::endl;

        zSpace::zPoint minBB;
        zSpace::zPoint maxBB;
        fn.getBounds(minBB, maxBB);
        Application::getInstance()->getCameraController().focusOnBounds(toVec3(minBB), toVec3(maxBB));
    }

    void buildLongitudeGraph()
    {
        clearGraph(m_longitudeGraph);
        zSpace::zPointArray positions;
        zSpace::zIntArray edgeConnects;
        for (auto& loop : m_loops) {
            for (zSpace::zItMeshHalfEdge& halfEdge : loop) {
                const int base = static_cast<int>(positions.size());
                positions.push_back(halfEdge.getStartVertex().getPosition());
                positions.push_back(halfEdge.getVertex().getPosition());
                edgeConnects.push_back(base);
                edgeConnects.push_back(base + 1);
            }
        }
        if (!positions.empty()) {
            zSpace::zFnGraph fn(m_longitudeGraph);
            fn.create(positions, edgeConnects);
        }
    }

    static void printIds(const char* label, const zSpace::zIntArray& ids)
    {
        std::cout << "[TwoBoundaryVolume][t] " << label << " (" << ids.size() << "):";
        for (int id : ids) std::cout << " " << id;
        std::cout << std::endl;
    }

    void deriveBoundaries()
    {
        if (!m_loaded) {
            m_status = "No input mesh. Press 'r' first.";
            std::cout << "[TwoBoundaryVolume][t] " << m_status << std::endl;
            return;
        }

        clearDerived();
        zSpace::zIntArray medialIds = {
            alice2::SlicingParameters::longitudeCornerStartVertexId,
            alice2::SlicingParameters::longitudeCornerEndVertexId
        };
        alice2::computeVLoops(m_inputMesh, medialIds, m_loops, m_topMesh, m_bottomMesh, &m_metadata);

        zSpace::zFnMesh fnTop(m_topMesh);
        zSpace::zFnMesh fnBottom(m_bottomMesh);
        m_haveBoundaries = !m_loops.empty() && fnTop.numVertices() > 0 && fnBottom.numVertices() > 0;
        buildLongitudeGraph();

        std::cout << "[TwoBoundaryVolume][t] top vertices/faces=" << fnTop.numVertices() << "/" << fnTop.numPolygons()
            << " bottom vertices/faces=" << fnBottom.numVertices() << "/" << fnBottom.numPolygons() << std::endl;
        std::cout << "[TwoBoundaryVolume][t] longitude loops=" << m_loops.size() << " lengths:";
        for (const auto& loop : m_loops) std::cout << " " << loop.size();
        std::cout << std::endl;
        printIds("metadata corner original vertex ids", m_metadata.cornerVertexIds);
        printIds("metadata corner longitude ids", m_metadata.cornerLongitudeIds);
        std::cout << "[TwoBoundaryVolume][t] metadata has no direct top/bottom original-id map; boundary mesh ids are temporary." << std::endl;

        if (!m_haveBoundaries) {
            m_status = "Boundary derivation failed; see computeVLoops diagnostics.";
            std::cout << "[TwoBoundaryVolume][t] " << m_status << std::endl;
            return;
        }

        // Retain the current surface-walk sections solely for debug mode 7.
        alice2::computeGeodesicScalars(m_inputMesh, m_loops, m_oldScalars, true);
        alice2::computeGeodesicContours(m_loops, m_oldScalars,
            alice2::SlicingParameters::printHeight, m_topMesh, m_bottomMesh, m_oldSectionMeshes);

        std::ostringstream out;
        out << "Boundaries ready: top " << fnTop.numVertices() << "/" << fnTop.numPolygons()
            << ", bottom " << fnBottom.numVertices() << "/" << fnBottom.numPolygons()
            << ", loops " << m_loops.size() << ", old sections " << m_oldSectionMeshes.size();
        m_status = out.str();
        m_debugMode = 1;
        std::cout << "[TwoBoundaryVolume][t] " << m_status << std::endl;
    }

    static std::vector<zSpace::zIntArray> getFaces(zSpace::zObjMesh& mesh)
    {
        zSpace::zFnMesh fn(mesh);
        zSpace::zIntArray connects;
        zSpace::zIntArray counts;
        fn.getPolygonData(connects, counts);
        std::vector<zSpace::zIntArray> faces;
        faces.reserve(counts.size());
        int cursor = 0;
        for (int count : counts) {
            zSpace::zIntArray face;
            for (int i = 0; i < count; ++i) face.push_back(connects[cursor++]);
            faces.push_back(face);
        }
        return faces;
    }

    static void appendFaceTriangles(const zSpace::zIntArray& face, int offset,
        std::vector<std::array<int, 3>>& triangles)
    {
        for (int i = 1; i + 1 < static_cast<int>(face.size()); ++i) {
            triangles.push_back({ face[0] + offset, face[i] + offset, face[i + 1] + offset });
        }
    }

    static void setTetgenFacet(tetgenio::facet& facet, const std::array<int, 3>& triangle)
    {
        facet.numberofpolygons = 1;
        facet.polygonlist = new tetgenio::polygon[1];
        facet.numberofholes = 0;
        facet.holelist = nullptr;
        tetgenio::polygon& polygon = facet.polygonlist[0];
        polygon.numberofvertices = 3;
        polygon.vertexlist = new int[3];
        polygon.vertexlist[0] = triangle[0];
        polygon.vertexlist[1] = triangle[1];
        polygon.vertexlist[2] = triangle[2];
    }

    void buildTetWireGraph()
    {
        clearGraph(m_debug.tetWireGraph);
        static constexpr int tetEdges[6][2] = { {0,1}, {0,2}, {0,3}, {1,2}, {1,3}, {2,3} };
        std::set<EdgeKey> uniqueEdges;
        for (const auto& tet : m_tetMesh.tets) {
            for (const auto& edge : tetEdges) uniqueEdges.insert(sortedEdge(tet[edge[0]], tet[edge[1]]));
        }
        zSpace::zIntArray edgeConnects;
        edgeConnects.reserve(uniqueEdges.size() * 2);
        for (const EdgeKey& edge : uniqueEdges) {
            edgeConnects.push_back(edge.first);
            edgeConnects.push_back(edge.second);
        }
        if (!edgeConnects.empty()) {
            zSpace::zFnGraph fn(m_debug.tetWireGraph);
            zSpace::zPointArray positions = m_tetMesh.vertices;
            fn.create(positions, edgeConnects);
        }
    }

    void buildTetVolume()
    {
        if (!m_haveBoundaries) {
            m_status = "No boundaries. Press 't' first.";
            std::cout << "[TwoBoundaryVolume][v] " << m_status << std::endl;
            return;
        }

        m_tetMesh = TestTetMesh();
        m_debug.phi.clear();
        m_debug.isoMeshes.clear();
        m_isoValues.clear();
        m_boundaryTags.clear();
        clearGraph(m_debug.tetWireGraph);
        clearGraph(m_debug.scalarPointGraph);
        m_haveVolume = false;
        m_havePhi = false;

        zSpace::zFnMesh fnBottom(m_bottomMesh);
        zSpace::zFnMesh fnTop(m_topMesh);
        zSpace::zPointArray bottomPositions;
        zSpace::zPointArray topPositions;
        fnBottom.getVertexPositions(bottomPositions);
        fnTop.getVertexPositions(topPositions);
        const std::vector<zSpace::zIntArray> bottomFaces = getFaces(m_bottomMesh);
        const std::vector<zSpace::zIntArray> topFaces = getFaces(m_topMesh);

        if (bottomPositions.size() != topPositions.size()) {
            m_status = "Vertex-order assumption failed: top/bottom vertex counts differ.";
            std::cout << "[TwoBoundaryVolume][v] ERROR " << m_status << std::endl;
            return;
        }

        const int topOffset = static_cast<int>(bottomPositions.size());

        zSpace::zPointArray plcPositions = bottomPositions;
        plcPositions.insert(plcPositions.end(), topPositions.begin(), topPositions.end());
        zSpace::zIntArray plcMarkers(plcPositions.size(), static_cast<int>(BoundaryTag::Bottom));
        for (int i = topOffset; i < static_cast<int>(plcMarkers.size()); ++i)
            plcMarkers[i] = static_cast<int>(BoundaryTag::Top);

        std::vector<std::array<int, 3>> boundaryTriangles;
        for (const zSpace::zIntArray& face : bottomFaces) appendFaceTriangles(face, 0, boundaryTriangles);
        for (const zSpace::zIntArray& face : topFaces) appendFaceTriangles(face, topOffset, boundaryTriangles);

        // Close the PLC by joining corresponding top/bottom vertices along every
        // boundary edge. This triangulation is shared by the single global volume.
        std::set<EdgeKey> boundaryEdges;
        for (zSpace::zItMeshHalfEdge halfEdge(m_bottomMesh); !halfEdge.end(); halfEdge++) {
            if (!halfEdge.onBoundary()) continue;
            zSpace::zIntArray edgeVertices;
            halfEdge.getVertices(edgeVertices);
            if (edgeVertices.size() < 2) continue;
            boundaryEdges.insert(sortedEdge(edgeVertices[0], edgeVertices[1]));
        }

        if (m_loops.size() != bottomPositions.size()) {
            m_status = "Cannot build conforming side walls: longitude-loop and cap-vertex counts differ.";
            std::cout << "[TwoBoundaryVolume][v] ERROR loops=" << m_loops.size()
                << " cap vertices=" << bottomPositions.size() << std::endl;
            return;
        }

        // Each cap vertex owns a top-to-bottom halfedge walk. Retain all
        // intermediate stations so the PLC side wall follows the source mesh.
        std::vector<zSpace::zIntArray> sideGrid(bottomPositions.size());
        int stationCount = -1;
        for (int vertexId = 0; vertexId < static_cast<int>(m_loops.size()); ++vertexId) {
            auto& loop = m_loops[vertexId];
            if (loop.empty()) {
                m_status = "Cannot build conforming side walls: empty longitude loop " + std::to_string(vertexId);
                std::cout << "[TwoBoundaryVolume][v] ERROR " << m_status << std::endl;
                return;
            }
            zSpace::zPointArray path;
            path.push_back(loop.front().getStartVertex().getPosition());
            for (zSpace::zItMeshHalfEdge& halfEdge : loop) path.push_back(halfEdge.getVertex().getPosition());
            if (stationCount < 0) stationCount = static_cast<int>(path.size());
            if (static_cast<int>(path.size()) != stationCount) {
                m_status = "Cannot build conforming side walls: longitude loop lengths differ.";
                std::cout << "[TwoBoundaryVolume][v] ERROR loop=" << vertexId
                    << " stations=" << path.size() << " expected=" << stationCount << std::endl;
                return;
            }

            auto squaredDistance = [](const zSpace::zPoint& a, const zSpace::zPoint& b) {
                const double dx = a.x - b.x;
                const double dy = a.y - b.y;
                const double dz = a.z - b.z;
                return dx * dx + dy * dy + dz * dz;
            };
            if (squaredDistance(path.front(), topPositions[vertexId]) >
                squaredDistance(path.back(), topPositions[vertexId])) std::reverse(path.begin(), path.end());

            sideGrid[vertexId].push_back(topOffset + vertexId);
            for (int station = 1; station + 1 < stationCount; ++station) {
                sideGrid[vertexId].push_back(static_cast<int>(plcPositions.size()));
                plcPositions.push_back(path[station]);
                plcMarkers.push_back(static_cast<int>(BoundaryTag::Side));
            }
            sideGrid[vertexId].push_back(vertexId);
        }

        for (const EdgeKey& edge : boundaryEdges) {
            const zSpace::zIntArray& a = sideGrid[edge.first];
            const zSpace::zIntArray& b = sideGrid[edge.second];
            for (int station = 0; station + 1 < stationCount; ++station) {
                boundaryTriangles.push_back({ a[station], b[station], b[station + 1] });
                boundaryTriangles.push_back({ a[station], b[station + 1], a[station + 1] });
            }
        }

        tetgenio input;
        tetgenio output;
        input.firstnumber = 0;
        input.numberofpoints = static_cast<int>(plcPositions.size());
        input.pointlist = new REAL[input.numberofpoints * 3];
        input.pointmarkerlist = new int[input.numberofpoints];
        for (int i = 0; i < input.numberofpoints; ++i) {
            const zSpace::zPoint& point = plcPositions[i];
            input.pointlist[i * 3 + 0] = point.x;
            input.pointlist[i * 3 + 1] = point.y;
            input.pointlist[i * 3 + 2] = point.z;
            input.pointmarkerlist[i] = plcMarkers[i];
        }

        input.numberoffacets = static_cast<int>(boundaryTriangles.size());
        input.facetlist = new tetgenio::facet[input.numberoffacets];
        input.facetmarkerlist = new int[input.numberoffacets];
        for (int i = 0; i < input.numberoffacets; ++i) {
            setTetgenFacet(input.facetlist[i], boundaryTriangles[i]);
            input.facetmarkerlist[i] = static_cast<int>(BoundaryTag::Side);
        }

        std::cout << "[TwoBoundaryVolume][v] PLC points=" << input.numberofpoints
            << " triangles=" << input.numberoffacets << " boundary edges=" << boundaryEdges.size() << std::endl;
        try {
            char switches[] = "pYQ";
            tetrahedralize(switches, &input, &output);
        }
        catch (...) {
            m_status = "TetGen failed while tetrahedralizing the closed boundary PLC.";
            std::cout << "[TwoBoundaryVolume][v] ERROR " << m_status << std::endl;
            return;
        }

        if (output.numberofpoints <= 0 || output.numberoftetrahedra <= 0 || output.numberofcorners < 4) {
            m_status = "TetGen returned an empty or invalid tetrahedralization.";
            std::cout << "[TwoBoundaryVolume][v] ERROR " << m_status << std::endl;
            return;
        }

        m_tetMesh.vertices.reserve(output.numberofpoints);
        m_boundaryTags.assign(output.numberofpoints, static_cast<int>(BoundaryTag::Side));
        for (int i = 0; i < output.numberofpoints; ++i) {
            m_tetMesh.vertices.emplace_back(
                output.pointlist[i * 3 + 0], output.pointlist[i * 3 + 1], output.pointlist[i * 3 + 2]);
            const int marker = output.pointmarkerlist ? output.pointmarkerlist[i] : 0;
            m_boundaryTags[i] = marker;
            if (marker == static_cast<int>(BoundaryTag::Bottom)) m_tetMesh.bottomVertexIds.push_back(i);
            else if (marker == static_cast<int>(BoundaryTag::Top)) m_tetMesh.topVertexIds.push_back(i);
        }
        m_tetMesh.tets.reserve(output.numberoftetrahedra);
        for (int i = 0; i < output.numberoftetrahedra; ++i) {
            const int* tet = output.tetrahedronlist + (i * output.numberofcorners);
            m_tetMesh.tets.push_back({ tet[0], tet[1], tet[2], tet[3] });
        }

        buildTetWireGraph();
        m_haveVolume = !m_tetMesh.tets.empty();
        const int sidePointCount = static_cast<int>(std::count(
            m_boundaryTags.begin(), m_boundaryTags.end(), static_cast<int>(BoundaryTag::Side)));
        const int volumeInteriorCount = static_cast<int>(m_tetMesh.vertices.size())
            - static_cast<int>(m_tetMesh.bottomVertexIds.size())
            - static_cast<int>(m_tetMesh.topVertexIds.size()) - sidePointCount;
        std::ostringstream out;
        out << "Tet vertices=" << m_tetMesh.vertices.size() << " tets=" << m_tetMesh.tets.size()
            << " constrained bottom/top=" << m_tetMesh.bottomVertexIds.size() << "/" << m_tetMesh.topVertexIds.size()
            << " side/interior points=" << sidePointCount << "/" << volumeInteriorCount;
        m_status = out.str();
        m_debugMode = 3;
        std::cout << "[TwoBoundaryVolume][v] " << m_status << std::endl;
    }

    void computeScalarField()
    {
        if (!m_haveVolume) {
            m_status = "No tet volume. Press 'v' first.";
            std::cout << "[TwoBoundaryVolume][h] " << m_status << std::endl;
            return;
        }

        const int vertexCount = static_cast<int>(m_tetMesh.vertices.size());
        m_debug.phi.assign(vertexCount, 0.0f);
        std::vector<double> fixedValue(vertexCount, -1.0);
        for (int id : m_tetMesh.bottomVertexIds) fixedValue[id] = 0.0;
        for (int id : m_tetMesh.topVertexIds) fixedValue[id] = 1.0;

        std::vector<int> freeIndex(vertexCount, -1);
        int freeCount = 0;
        for (int i = 0; i < vertexCount; ++i) {
            if (fixedValue[i] < 0.0) freeIndex[i] = freeCount++;
            else m_debug.phi[i] = static_cast<float>(fixedValue[i]);
        }

        std::vector<Eigen::Triplet<double>> triplets;
        Eigen::VectorXd rhs = Eigen::VectorXd::Zero(freeCount);
        int degenerateTets = 0;
        for (const std::array<int, 4>& tet : m_tetMesh.tets) {
            Eigen::Matrix4d interpolation;
            Eigen::Matrix3d edgeMatrix;
            for (int local = 0; local < 4; ++local) {
                const zSpace::zPoint& point = m_tetMesh.vertices[tet[local]];
                interpolation.row(local) << 1.0, point.x, point.y, point.z;
                if (local > 0) {
                    const zSpace::zPoint& origin = m_tetMesh.vertices[tet[0]];
                    edgeMatrix.col(local - 1) << point.x - origin.x, point.y - origin.y, point.z - origin.z;
                }
            }
            const double determinant = edgeMatrix.determinant();
            if (std::fabs(determinant) < 1e-14) {
                ++degenerateTets;
                continue;
            }

            const double volume = std::fabs(determinant) / 6.0;
            const Eigen::Matrix4d coefficients = interpolation.inverse();
            Eigen::Matrix<double, 3, 4> gradients;
            for (int local = 0; local < 4; ++local) gradients.col(local) = coefficients.block<3, 1>(1, local);
            const Eigen::Matrix4d localStiffness = volume * gradients.transpose() * gradients;

            for (int i = 0; i < 4; ++i) {
                const int globalI = tet[i];
                const int row = freeIndex[globalI];
                if (row < 0) continue;
                for (int j = 0; j < 4; ++j) {
                    const int globalJ = tet[j];
                    const double value = localStiffness(i, j);
                    const int column = freeIndex[globalJ];
                    if (column >= 0) triplets.emplace_back(row, column, value);
                    else rhs[row] -= value * fixedValue[globalJ];
                }
            }
        }

        if (freeCount > 0) {
            Eigen::SparseMatrix<double> stiffness(freeCount, freeCount);
            stiffness.setFromTriplets(triplets.begin(), triplets.end());
            Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> solver;
            solver.compute(stiffness);
            if (solver.info() != Eigen::Success) {
                m_status = "Harmonic solve factorization failed.";
                std::cout << "[TwoBoundaryVolume][h] ERROR " << m_status << std::endl;
                return;
            }
            const Eigen::VectorXd solution = solver.solve(rhs);
            if (solver.info() != Eigen::Success || !solution.allFinite()) {
                m_status = "Harmonic solve returned an invalid field.";
                std::cout << "[TwoBoundaryVolume][h] ERROR " << m_status << std::endl;
                return;
            }
            for (int i = 0; i < vertexCount; ++i) {
                if (freeIndex[i] >= 0) m_debug.phi[i] = std::clamp(static_cast<float>(solution[freeIndex[i]]), 0.0f, 1.0f);
            }
        }

        clearGraph(m_debug.scalarPointGraph);
        zSpace::zIntArray noEdges;
        zSpace::zPointArray positions = m_tetMesh.vertices;
        zSpace::zFnGraph fn(m_debug.scalarPointGraph);
        fn.create(positions, noEdges);

        const auto range = std::minmax_element(m_debug.phi.begin(), m_debug.phi.end());
        m_havePhi = !m_debug.phi.empty();
        std::ostringstream out;
        out << "phi min/max=" << *range.first << "/" << *range.second
            << " constrained bottom/top=" << m_tetMesh.bottomVertexIds.size() << "/" << m_tetMesh.topVertexIds.size()
            << " harmonic unknowns=" << freeCount << " degenerate tets skipped=" << degenerateTets;
        m_status = out.str();
        m_debugMode = 4;
        std::cout << "[TwoBoundaryVolume][h] " << m_status << std::endl;
    }

    int crossingVertex(int a, int b, float iso, std::map<EdgeKey, int>& edgeVertices,
        zSpace::zPointArray& outPositions)
    {
        const EdgeKey key = sortedEdge(a, b);
        const auto found = edgeVertices.find(key);
        if (found != edgeVertices.end()) return found->second;
        const float aPhi = m_debug.phi[a];
        const float bPhi = m_debug.phi[b];
        const float denominator = bPhi - aPhi;
        const float t = (std::fabs(denominator) < 1e-8f) ? 0.5f : (iso - aPhi) / denominator;
        const zSpace::zPoint point = m_tetMesh.vertices[a] +
            (m_tetMesh.vertices[b] - m_tetMesh.vertices[a]) * std::clamp(t, 0.0f, 1.0f);
        const int id = static_cast<int>(outPositions.size());
        outPositions.push_back(point);
        edgeVertices[key] = id;
        return id;
    }

    bool appendTriangle(zSpace::zIntArray& connects, int a, int b, int c,
        const zSpace::zPointArray& positions, const zSpace::zVector& lowToHigh,
        std::set<std::array<int, 3>>& uniqueTriangles, int& degenerateCount, int& duplicateCount)
    {
        if (a == b || b == c || c == a || triangleArea2(positions[a], positions[b], positions[c]) < 1e-9) {
            ++degenerateCount;
            return false;
        }

        // zObjMesh requires consistently directed shared halfedges. Marching each
        // tet independently does not guarantee that, so orient every triangle
        // from the low-phi boundary toward the high-phi boundary before creation.
        zSpace::zPoint pa = positions[a];
        zSpace::zPoint pb = positions[b];
        zSpace::zPoint pc = positions[c];
        zSpace::zVector normal = (pb - pa) ^ (pc - pa);
        zSpace::zVector direction = lowToHigh;
        if ((normal * direction) < 0.0f) std::swap(b, c);

        std::array<int, 3> triangleKey = { a, b, c };
        std::sort(triangleKey.begin(), triangleKey.end());
        if (!uniqueTriangles.insert(triangleKey).second) {
            ++duplicateCount;
            return false;
        }

        connects.push_back(a);
        connects.push_back(b);
        connects.push_back(c);
        return true;
    }

    float shortestLongitudeLength()
    {
        float shortest = std::numeric_limits<float>::max();
        for (auto& loop : m_loops) {
            float length = 0.0f;
            for (auto& halfedge : loop) length += static_cast<float>(halfedge.getLength());
            if (length > 1e-6f) shortest = std::min(shortest, length);
        }
        return shortest;
    }

    void extractIsoMeshes()
    {
        if (!m_havePhi) {
            m_status = "No scalar field. Press 'h' first.";
            std::cout << "[TwoBoundaryVolume][m] " << m_status << std::endl;
            return;
        }

        m_debug.isoMeshes.clear();
        m_isoValues.clear();
        const float printHeight = alice2::SlicingParameters::printHeight;
        const float shortestLength = shortestLongitudeLength();
        if (printHeight <= 0.0f || shortestLength == std::numeric_limits<float>::max()) {
            m_status = "Cannot determine slice layers: invalid print height or longitude walks.";
            std::cout << "[TwoBoundaryVolume][m] " << m_status
                << " printHeight=" << printHeight << std::endl;
            return;
        }

        const double layerRatio = static_cast<double>(shortestLength) / static_cast<double>(printHeight);
        // Ceil ensures the interval along the shortest longitude does not
        // exceed the requested print height. The relative tolerance avoids
        // adding a layer when an integral ratio has only float noise.
        const double ratioTolerance = 1e-9 * std::max(1.0, layerRatio);
        const int layerCount = std::max(2, static_cast<int>(std::ceil(layerRatio - ratioTolerance)));
        std::cout << "[TwoBoundaryVolume][m] shortestLongitudeLength=" << shortestLength
            << " printHeight=" << printHeight
            << " ratio=" << layerRatio
            << " layerIntervals=" << layerCount
            << " interiorSlices=" << (layerCount - 1) << std::endl;
        // Construct into fixed slots. zObjMesh owns internal pointer-based
        // topology, so moving already-created meshes during vector growth is unsafe.
        m_debug.isoMeshes.assign(layerCount - 1, zSpace::zObjMesh());
        m_isoValues.reserve(layerCount - 1);
        int totalTriangles = 0;
        for (int k = 1; k < layerCount; ++k) {
            const float iso = static_cast<float>(k) / static_cast<float>(layerCount);
            zSpace::zPointArray positions;
            zSpace::zIntArray connects;
            zSpace::zIntArray counts;
            std::map<EdgeKey, int> edgeVertices;
            std::set<std::array<int, 3>> uniqueTriangles;
            int degenerateCount = 0;
            int duplicateCount = 0;

            for (const auto& tet : m_tetMesh.tets) {
                zSpace::zIntArray inside;
                zSpace::zIntArray outside;
                for (int localId = 0; localId < 4; ++localId) {
                    if (m_debug.phi[tet[localId]] < iso) inside.push_back(tet[localId]);
                    else outside.push_back(tet[localId]);
                }
                if (inside.empty() || outside.empty()) continue;

                zSpace::zVector lowToHigh;
                for (int lowId : inside) {
                    for (int highId : outside) {
                        lowToHigh += m_tetMesh.vertices[highId] - m_tetMesh.vertices[lowId];
                    }
                }

                if (inside.size() == 1 || outside.size() == 1) {
                    const bool oneInside = inside.size() == 1;
                    const int lone = oneInside ? inside[0] : outside[0];
                    const zSpace::zIntArray& other = oneInside ? outside : inside;
                    const int a = crossingVertex(lone, other[0], iso, edgeVertices, positions);
                    const int b = crossingVertex(lone, other[1], iso, edgeVertices, positions);
                    const int c = crossingVertex(lone, other[2], iso, edgeVertices, positions);
                    if (appendTriangle(connects, a, oneInside ? b : c, oneInside ? c : b,
                        positions, lowToHigh, uniqueTriangles, degenerateCount, duplicateCount))
                        counts.push_back(3);
                }
                else {
                    const int a = crossingVertex(inside[0], outside[0], iso, edgeVertices, positions);
                    const int b = crossingVertex(inside[0], outside[1], iso, edgeVertices, positions);
                    const int c = crossingVertex(inside[1], outside[1], iso, edgeVertices, positions);
                    const int d = crossingVertex(inside[1], outside[0], iso, edgeVertices, positions);
                    if (appendTriangle(connects, a, b, c, positions, lowToHigh,
                        uniqueTriangles, degenerateCount, duplicateCount)) counts.push_back(3);
                    if (appendTriangle(connects, a, c, d, positions, lowToHigh,
                        uniqueTriangles, degenerateCount, duplicateCount)) counts.push_back(3);
                }
            }

            std::cout << "[TwoBoundaryVolume][m] building iso=" << iso
                << " vertices=" << positions.size() << " triangles=" << counts.size() << std::endl;
            if (!counts.empty()) {
                zSpace::zFnMesh fn(m_debug.isoMeshes[k - 1]);
                fn.create(positions, counts, connects);
            }
            m_isoValues.push_back(iso);
            totalTriangles += static_cast<int>(counts.size());
            std::cout << "[TwoBoundaryVolume][m] iso=" << iso << " vertices=" << positions.size()
                << " triangles=" << counts.size() << " degenerate skipped=" << degenerateCount
                << " duplicate skipped=" << duplicateCount << std::endl;
        }

        m_currentSlice = m_debug.isoMeshes.empty() ? 0 : std::min(m_currentSlice, static_cast<int>(m_debug.isoMeshes.size()) - 1);
        std::ostringstream out;
        out << "Extracted " << m_debug.isoMeshes.size() << " interior iso layers, triangles=" << totalTriangles;
        m_status = out.str();
        m_debugMode = 5;
        std::cout << "[TwoBoundaryVolume][m] " << m_status << std::endl;
    }

    void cycleDebugMode()
    {
        m_debugMode = (m_debugMode + 1) % 8;
        m_status = "Debug mode " + std::to_string(m_debugMode) + ": " + modeName();
        std::cout << "[TwoBoundaryVolume][d] " << m_status << std::endl;
    }

    void changeSlice(int delta)
    {
        if (m_debug.isoMeshes.empty()) {
            m_status = "No volume slices. Press 'm' first.";
            std::cout << "[TwoBoundaryVolume][slice] " << m_status << std::endl;
            return;
        }
        const int count = static_cast<int>(m_debug.isoMeshes.size());
        m_currentSlice = (m_currentSlice + delta + count) % count;

        // Slice navigation must produce a visible change. Modes such as
        // "all iso slices" draw every layer, so changing only the index is
        // otherwise impossible to see. Keep comparison mode because it also
        // displays the selected layer; enter current-slice mode from all
        // other views.
        if (m_debugMode != 7) m_debugMode = 5;

        std::ostringstream out;
        out << "Slice " << (m_currentSlice + 1) << "/" << count
            << " iso=" << m_isoValues[m_currentSlice]
            << " (mode " << m_debugMode << ")";
        m_status = out.str();
        std::cout << "[TwoBoundaryVolume][slice] " << m_status << std::endl;
    }

    std::string modeName() const
    {
        static const char* names[] = {
            "input mesh", "derived top/bottom", "longitude loops", "tet volume wire",
            "scalar field points", "current iso slice", "all iso slices", "old/new comparison"
        };
        return names[m_debugMode];
    }

    void drawInput()
    {
        if (!m_loaded) return;
        zDisplayMeshSetting display;
        display.showFaces = true;
        display.showEdges = true;
        display.showVertices = false;
        display.faceColor = Color(0.58f, 0.60f, 0.64f, 0.26f);
        display.edgeColor = Color(0.28f, 0.30f, 0.34f, 0.82f);
        display.edgeWidth = 0.8f;
        scene().draw(m_inputMesh, display);
    }

    static zDisplayMeshSetting boundaryDisplay(const Color& face, const Color& edge)
    {
        zDisplayMeshSetting display;
        display.showFaces = true;
        display.showEdges = true;
        display.showVertices = false;
        display.faceColor = face;
        display.edgeColor = edge;
        display.edgeWidth = 2.0f;
        return display;
    }

    void drawBoundaries()
    {
        if (!m_haveBoundaries) return;
        zDisplayMeshSetting bottom = boundaryDisplay(Color(0.02f, 0.25f, 1.0f, 0.38f), Color(0.1f, 0.45f, 1.0f, 1.0f));
        zDisplayMeshSetting top = boundaryDisplay(Color(1.0f, 0.04f, 0.08f, 0.38f), Color(1.0f, 0.16f, 0.12f, 1.0f));
        scene().draw(m_bottomMesh, bottom);
        scene().draw(m_topMesh, top);
    }

    void drawLongitudeLoops()
    {
        if (!m_haveBoundaries) return;
        scene().draw(m_longitudeGraph, Display::lines(Color(1.0f, 0.42f, 0.02f, 1.0f), 3.0f));
    }

    void drawTetWire()
    {
        if (!m_haveVolume) return;
        scene().draw(m_debug.tetWireGraph, Display::lines(Color(0.0f, 0.9f, 1.0f, 0.8f), 1.1f));
    }

    void drawScalarPoints(Renderer& renderer)
    {
        if (!m_havePhi) return;
        for (int i = 0; i < static_cast<int>(m_tetMesh.vertices.size()); ++i) {
            const float phi = std::clamp(m_debug.phi[i], 0.0f, 1.0f);
            renderer.drawPoint(toVec3(m_tetMesh.vertices[i]), Color(phi, 0.08f, 1.0f - phi, 1.0f), 7.0f);
        }
    }

    static zDisplayMeshSetting isoDisplay(float alpha, const Color& edge)
    {
        zDisplayMeshSetting display;
        display.showFaces = true;
        display.showEdges = true;
        display.showVertices = false;
        display.faceColor = Color(1.0f, 0.05f, 0.72f, alpha);
        display.edgeColor = edge;
        display.edgeWidth = 1.2f;
        return display;
    }

    void drawCurrentIso()
    {
        if (m_currentSlice < 0 || m_currentSlice >= static_cast<int>(m_debug.isoMeshes.size())) return;
        zDisplayMeshSetting display = isoDisplay(0.72f, Color(0.42f, 0.0f, 0.30f, 1.0f));
        scene().draw(m_debug.isoMeshes[m_currentSlice], display);
    }

    void drawAllIsos()
    {
        zDisplayMeshSetting display = isoDisplay(0.28f, Color(0.62f, 0.08f, 0.45f, 0.82f));
        for (zSpace::zObjMesh& mesh : m_debug.isoMeshes) scene().draw(mesh, display);
    }

    void drawComparison()
    {
        drawCurrentIso();
        if (m_oldSectionMeshes.empty() || m_debug.isoMeshes.empty()) return;
        const float fraction = (m_debug.isoMeshes.size() <= 1)
            ? 0.0f
            : static_cast<float>(m_currentSlice) / static_cast<float>(m_debug.isoMeshes.size() - 1);
        const int oldId = std::clamp(static_cast<int>(std::round(fraction * (m_oldSectionMeshes.size() - 1))),
            0, static_cast<int>(m_oldSectionMeshes.size()) - 1);
        scene().draw(m_oldSectionMeshes[oldId], Display::wireframe(Color(0.2f, 1.0f, 0.25f, 1.0f), 2.2f));
    }
};

ALICE2_REGISTER_SKETCH_AUTO(TwoBoundaryVolumeSlicingSketch)

#endif // __MAIN__

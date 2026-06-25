#define __MAIN__
#ifdef __MAIN__

#include <zspace/interface.h>

#include <alice2.h>
#include <sketches/SketchRegistry.h>
#include <slicer/zUnroller.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <vector>

using namespace alice2;

class zSpaceBlendImportSketch : public ISketch {
public:
    std::string getName() const override { return "zSpace Blend Mesh Import"; }
    std::string getDescription() const override { return "Imports and slices the block mesh used by the old zSpace Blend sketch."; }
    std::string getAuthor() const override { return "alice2 + zspace_core"; }

    void setup() override
    {
        scene().setBackgroundColor(Color(0.94f, 0.94f, 0.92f, 1.0f));
        scene().setShowGrid(true);
        scene().setGridSize(8.0f);
        scene().setGridDivisions(8);
        scene().setShowAxes(true);
        scene().setAxesLength(1.5f);

        loadMesh();
    }

    void update(float) override {}

    void draw(Renderer& renderer, Camera&) override
    {
        if (m_loaded) {
            if (m_showBlockMesh) {
                zDisplayMeshSetting meshDisplay;
                meshDisplay.showFaces = true;
                meshDisplay.showEdges = true;
                meshDisplay.showVertices = false;
                meshDisplay.faceColor = Color(0.76f, 0.82f, 0.88f, 0.62f);
                meshDisplay.edgeColor = Color(0.05f, 0.05f, 0.06f, 1.0f);
                meshDisplay.edgeWidth = 1.2f;
                scene().draw(m_mesh, meshDisplay);
            }

            drawSections(renderer);
        }

        renderer.setColor(Color(0.02f, 0.02f, 0.02f, 1.0f));
        renderer.drawString(getName(), 10, 28);
        renderer.drawString("Mesh: " + m_meshPath, 10, 50);
        renderer.drawString(m_status, 10, 72);
        renderer.drawString("'r' read mesh, 'p' slices, 'o' all SDF/post, 'i' print mesh, 'x' toggle mesh/print, 'f' focus, 'd' toggle sections, 'w/s' or '['/']' section", 10, 94);
    }

    bool onKeyPress(unsigned char key, int, int) override
    {
        if (key == 'r' || key == 'R') {
            loadMesh();
            return true;
        }

        if (key == 'p' || key == 'P') {
            computeSlices();
            return true;
        }

        if (key == 'o' || key == 'O') {
            computeSdfField();
            return true;
        }

        if (key == 'i' || key == 'I') {
            computePrintMeshes();
            return true;
        }

        if (key == 'x' || key == 'X') {
            toggleMeshAndPrintDisplay();
            return true;
        }

        if ((key == 'f' || key == 'F') && m_loaded && m_showBlockMesh) {
            focusOnMesh();
            return true;
        }

        if (key == 'd' || key == 'D') {
            m_displaySections = !m_displaySections;
            std::cout << "[zSpaceBlendImport] sections display: " << (m_displaySections ? "on" : "off") << std::endl;
            return true;
        }

        if (key == 'g' || key == 'G') {
            m_debugComputeVLoops = !m_debugComputeVLoops;
            std::cout << "[zSpaceBlendImport] computeVLoops debug: "
                << (m_debugComputeVLoops ? "on" : "off") << std::endl;
            return true;
        }

        if (key == '[' || key == 's' || key == 'S') {
            return changeSection(-1);
        }

        if (key == ']' || key == 'w' || key == 'W') {
            return changeSection(1);
        }

        return false;
    }

private:
    struct BracingLineGroup {
        int sequence = -1;
        Color color;
        zSpace::zObjGraph graph;
        int segmentCount = 0;
    };

    zSpace::zObjMesh m_mesh;
    std::vector<zSpace::zItMeshHalfEdgeArray> m_loops;
    zSpace::zObjMesh m_topMesh;
    zSpace::zObjMesh m_bottomMesh;
    zSpace::zScalarArray m_scalars;
    zSpace::zObjMeshArray m_sectionMeshes;
    zSpace::zObjGraphArray m_sectionGraphs;
    zSpace::zObjGraphArray m_contourGraphs;
    zSpace::zObjGraphArray m_sdfFlatGraphs;
    zSpace::zObjGraphArray m_layerBracingGraphs;
    zSpace::zObjGraphArray m_flatBracingGraphs;
    zSpace::zObjGraphArray m_bracingSlotGraphs;
    zSpace::zObjMeshScalarFieldArray m_sdfFields;
    alice2::SDFLayerDebugData m_sdfDebugData;
    alice2::SDFPostProcessResult m_postProcessResult;
    zSpace::zObjMeshArray m_printMeshes;
    alice2::SliceMetadata m_sliceMetadata;
    std::vector<BracingLineGroup> m_bracingGroups;
    std::string m_meshPath = alice2::SlicingParameters::inputMeshPath;
    std::string m_bracingGraphPath = alice2::SlicingParameters::bracingGraphPath;
    std::string m_status = "Waiting for mesh.";
    bool m_loaded = false;
    bool m_showBlockMesh = false;
    bool m_showPrintMeshes = true;
    bool m_displaySections = true;
    bool m_debugComputeVLoops = true;
    int m_currentSection = 0;
    int m_debugSdfLayerCount = 3;

    static Vec3 toVec3(const zSpace::zVector& p)
    {
        return Vec3(static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z));
    }

    static Color fallbackBracingColor(int index)
    {
        static const Color colors[] = {
            Color(1.0f, 0.42f, 0.05f, 1.0f),
            Color(0.0f, 0.58f, 1.0f, 1.0f),
            Color(0.1f, 0.85f, 0.2f, 1.0f),
            Color(0.95f, 0.1f, 0.75f, 1.0f),
            Color(0.95f, 0.85f, 0.05f, 1.0f)
        };
        return colors[index % (sizeof(colors) / sizeof(colors[0]))];
    }

    static bool readIntArrayAttribute(const std::string& path, const char* key, zSpace::zIntArray& values)
    {
        std::ifstream in(path);
        if (!in.is_open()) return false;

        nlohmann::json j;
        in >> j;
        if (!j.contains(key) || !j[key].is_array()) return false;

        values.clear();
        for (const auto& item : j[key]) {
            if (item.is_number_integer()) values.push_back(item.get<int>());
        }
        return !values.empty();
    }

    static bool getSingleEdgeEndpoints(zSpace::zObjGraph& graph, zSpace::zPoint& a, zSpace::zPoint& b)
    {
        zSpace::zFnGraph fnGraph(graph);
        zSpace::zPointArray positions;
        zSpace::zIntArray edgeConnects;
        fnGraph.getVertexPositions(positions);
        fnGraph.getEdgeData(edgeConnects);
        if (positions.empty() || edgeConnects.size() < 2) return false;

        const int startId = edgeConnects[0];
        const int endId = edgeConnects[1];
        if (startId < 0 || endId < 0 ||
            startId >= static_cast<int>(positions.size()) ||
            endId >= static_cast<int>(positions.size())) return false;

        a = positions[startId];
        b = positions[endId];
        return true;
    }

    void clearSdfDebugData()
    {
        m_contourGraphs.clear();
        m_sdfFlatGraphs.clear();
        m_layerBracingGraphs.clear();
        m_flatBracingGraphs.clear();
        m_bracingSlotGraphs.clear();
        m_sdfFields.clear();
        m_sdfDebugData = alice2::SDFLayerDebugData();
        m_postProcessResult = alice2::SDFPostProcessResult();
        m_printMeshes.clear();
    }

    void markSectionMeshCornerVerticesById()
    {
        m_sliceMetadata.sectionVertexOriginalIds.clear();
        m_sliceMetadata.sectionVertexOriginalIds.assign(m_sectionMeshes.size(), zSpace::zIntArray());

        for (int layer = 0; layer < static_cast<int>(m_sectionMeshes.size()); layer++) {
            zSpace::zFnMesh fnMesh(m_sectionMeshes[layer]);
            m_sliceMetadata.sectionVertexOriginalIds[layer].reserve(fnMesh.numVertices());
            for (int v = 0; v < fnMesh.numVertices(); v++) m_sliceMetadata.sectionVertexOriginalIds[layer].push_back(v);

            zSpace::zColor* colors = fnMesh.getRawVertexColors();
            if (!colors) continue;

            for (int v = 0; v < fnMesh.numVertices(); v++) colors[v] = zSpace::zColor(0.0, 1.0, 0.0, 1.0);
            for (int loopId : m_sliceMetadata.cornerLongitudeIds) {
                if (loopId < 0 || loopId >= fnMesh.numVertices()) continue;
                colors[loopId] = zSpace::zColor(1.0, 0.45, 0.0, 1.0);
            }
        }

        std::cout << "[zSpaceBlendImport][corners] marked section mesh corner vertex ids:";
        for (int loopId : m_sliceMetadata.cornerLongitudeIds) std::cout << " " << loopId;
        std::cout << std::endl;
    }

    void buildLayerBracingGraphs()
    {
        m_layerBracingGraphs.clear();
        if (m_sectionMeshes.empty()) return;
        m_layerBracingGraphs.assign(m_sectionMeshes.size(), zSpace::zObjGraph());

        if (m_bracingGroups.size() < 4) {
            std::cout << "[zSpaceBlendImport][bracing] WARNING need 4 input bracing graphs for pairs 0->2 and 1->3; got "
                << m_bracingGroups.size() << std::endl;
            return;
        }

        const int pairs[2][2] = { {0, 2}, {1, 3} };
        for (int layer = 0; layer < static_cast<int>(m_sectionMeshes.size()); layer++) {
            const float t = (m_sectionMeshes.size() <= 1)
                ? 0.0f
                : static_cast<float>(layer) / static_cast<float>(m_sectionMeshes.size() - 1);

            zSpace::zPointArray positions;
            zSpace::zIntArray edgeConnects;
            for (int pairId = 0; pairId < 2; pairId++) {
                const int aId = pairs[pairId][0];
                const int bId = pairs[pairId][1];
                zSpace::zPoint a0, a1, b0, b1;
                if (!getSingleEdgeEndpoints(m_bracingGroups[aId].graph, a0, a1)) continue;
                if (!getSingleEdgeEndpoints(m_bracingGroups[bId].graph, b0, b1)) continue;

                const int id = static_cast<int>(positions.size());
                positions.push_back(a0 * (1.0f - t) + b0 * t);
                positions.push_back(a1 * (1.0f - t) + b1 * t);
                edgeConnects.push_back(id);
                edgeConnects.push_back(id + 1);
            }

            zSpace::zFnGraph fnLayerGraph(m_layerBracingGraphs[layer]);
            fnLayerGraph.clear();
            if (!positions.empty()) fnLayerGraph.create(positions, edgeConnects);
            fnLayerGraph.setEdgeColor(zSpace::zColor(0.0, 0.75, 1.0, 1.0));
            fnLayerGraph.setEdgeWeight(4);
        }

        std::cout << "[zSpaceBlendImport][bracing] built layer bracing graphs=" << m_layerBracingGraphs.size()
            << " using pairs 0->2 and 1->3" << std::endl;
    }

    bool loadBracingGraph()
    {
        m_bracingGroups.clear();

        auto appendBracingSegment = [&](const zSpace::zPoint& a, const zSpace::zPoint& b, int sequence, const std::string& sourceLabel) {
            BracingLineGroup group;
            group.sequence = sequence;
            group.color = fallbackBracingColor(sequence);
            group.segmentCount = 1;

            zSpace::zPointArray positions = { a, b };
            zSpace::zIntArray edgeConnects = { 0, 1 };
            zSpace::zFnGraph fnGraph(group.graph);
            fnGraph.clear();
            fnGraph.create(positions, edgeConnects);
            fnGraph.setEdgeColor(zSpace::zColor(group.color.r, group.color.g, group.color.b, group.color.a));
            fnGraph.setEdgeWeight(3);

            std::cout << "[zSpaceBlendImport][bracing] sequence " << sequence
                << " source=" << sourceLabel
                << " graph=" << m_bracingGroups.size()
                << " p0=(" << a.x << "," << a.y << "," << a.z << ")"
                << " p1=(" << b.x << "," << b.y << "," << b.z << ")"
                << std::endl;

            m_bracingGroups.push_back(group);
        };

        zSpace::zObjGraph loadedGraph;
        auto graphResult = zSpace::zIO::readGraph(m_bracingGraphPath, loadedGraph);
        if (graphResult) {
            zSpace::zFnGraph fnLoadedGraph(loadedGraph);
            zSpace::zPointArray positions;
            zSpace::zIntArray edgeConnects;
            fnLoadedGraph.getVertexPositions(positions);
            fnLoadedGraph.getEdgeData(edgeConnects);

            for (int e = 0; e + 1 < static_cast<int>(edgeConnects.size()); e += 2) {
                const int a = edgeConnects[e];
                const int b = edgeConnects[e + 1];
                if (a < 0 || b < 0 || a >= static_cast<int>(positions.size()) || b >= static_cast<int>(positions.size())) continue;
                appendBracingSegment(positions[a], positions[b], static_cast<int>(m_bracingGroups.size()), "zIO::readGraph");
            }

            if (!m_bracingGroups.empty()) {
                std::cout << "[zSpaceBlendImport][bracing] zIO::readGraph loaded "
                    << m_bracingGroups.size() << " separate graphs from " << m_bracingGraphPath << std::endl;
                return true;
            }

            std::cout << "[zSpaceBlendImport][bracing] zIO::readGraph returned no edges; falling back to Rhino curv parser." << std::endl;
        }
        else {
            std::cout << "[zSpaceBlendImport][bracing] zIO::readGraph failed: "
                << graphResult.message() << " | falling back to Rhino curv parser." << std::endl;
        }

        std::ifstream in(m_bracingGraphPath);
        if (!in.is_open()) {
            std::cout << "[zSpaceBlendImport][bracing] Could not open " << m_bracingGraphPath << std::endl;
            return false;
        }

        std::vector<zSpace::zPoint> objVertices;

        std::string line;
        int objCurveCount = 0;
        while (std::getline(in, line)) {
            const size_t commentPos = line.find('#');
            if (commentPos != std::string::npos) line = line.substr(0, commentPos);
            std::istringstream iss(line);

            std::string tag;
            iss >> tag;
            if (tag.empty()) continue;

            if (tag == "v") {
                double x = 0.0;
                double y = 0.0;
                double z = 0.0;
                iss >> x >> y >> z;
                objVertices.emplace_back(x, y, z);
                continue;
            }

            if (tag == "usemtl") {
                continue;
            }

            if (tag == "curv") {
                double u0 = 0.0;
                double u1 = 0.0;
                iss >> u0 >> u1;

                std::vector<int> vertexIds;
                int objVertexId = 0;
                while (iss >> objVertexId) vertexIds.push_back(objVertexId - 1);
                if (vertexIds.size() < 2) continue;

                for (int i = 0; i + 1 < static_cast<int>(vertexIds.size()); i++) {
                    const int a = vertexIds[i];
                    const int b = vertexIds[i + 1];
                    if (a < 0 || b < 0 || a >= static_cast<int>(objVertices.size()) || b >= static_cast<int>(objVertices.size())) continue;

                    appendBracingSegment(objVertices[a], objVertices[b], objCurveCount, "Rhino curv");
                    objCurveCount++;
                }
            }
        }

        std::cout << "[zSpaceBlendImport][bracing] loaded " << objCurveCount
            << " curve segments from " << m_bracingGraphPath
            << " into " << m_bracingGroups.size() << " separate graphs" << std::endl;
        for (int i = 0; i < static_cast<int>(m_bracingGroups.size()); i++) {
            std::cout << "[zSpaceBlendImport][bracing] graph[" << i << "] sequence="
                << m_bracingGroups[i].sequence
                << " color=(" << m_bracingGroups[i].color.r << "," << m_bracingGroups[i].color.g << ","
                << m_bracingGroups[i].color.b << "," << m_bracingGroups[i].color.a << ")"
                << " segments=" << m_bracingGroups[i].segmentCount
                << std::endl;
        }

        return objCurveCount > 0;
    }

    void debugPrintMeshStats(const char* label, zSpace::zObjMesh& mesh)
    {
        if (!m_debugComputeVLoops) return;

        zSpace::zFnMesh fn(mesh);
        std::cout << "[zSpaceBlendImport][computeVLoops] " << label
            << " vertices=" << fn.numVertices()
            << " edges=" << fn.numEdges()
            << " halfEdges=" << fn.numHalfEdges()
            << " faces=" << fn.numPolygons()
            << std::endl;
    }

    void debugPrintGraphStats(const char* label, zSpace::zObjGraph& graph)
    {
        if (!m_debugComputeVLoops) return;

        zSpace::zFnGraph fn(graph);
        std::cout << "[zSpaceBlendImport][computeVLoops] " << label
            << " vertices=" << fn.numVertices()
            << " edges=" << fn.numEdges()
            << std::endl;
    }

    void debugPrintHalfEdge(const char* label, zSpace::zItMeshHalfEdge he)
    {
        if (!m_debugComputeVLoops) return;

        zSpace::zItMeshVertex start = he.getStartVertex();
        zSpace::zItMeshVertex end = he.getVertex();
        zSpace::zVector p0 = start.getPosition();
        zSpace::zVector p1 = end.getPosition();
        zSpace::zVector edge = he.getVector();
        zSpace::zVector normal = he.getFace().getNormal();

        std::cout << "[zSpaceBlendImport][computeVLoops] " << label
            << " he=" << he.getId()
            << " face=" << he.getFace().getId()
            << " edge=" << start.getId() << "->" << end.getId()
            << " valence=" << start.getValence() << "->" << end.getValence()
            << " length=" << edge.length()
            << " p0=(" << p0.x << "," << p0.y << "," << p0.z << ")"
            << " p1=(" << p1.x << "," << p1.y << "," << p1.z << ")"
            << " faceNormal=(" << normal.x << "," << normal.y << "," << normal.z << ")"
            << std::endl;
    }

    void debugPrintVertex(int vertexId)
    {
        if (!m_debugComputeVLoops) return;

        zSpace::zItMeshVertex v(m_mesh, vertexId);
        zSpace::zVector p = v.getPosition();
        std::cout << "[zSpaceBlendImport][computeVLoops] vertex=" << vertexId
            << " valence=" << v.getValence()
            << " position=(" << p.x << "," << p.y << "," << p.z << ")"
            << std::endl;

        zSpace::zItMeshHalfEdgeArray connected;
        v.getConnectedHalfEdges(connected);
        for (int i = 0; i < static_cast<int>(connected.size()); i++) {
            std::ostringstream label;
            label << "  connectedHalfEdge[" << i << "]";
            debugPrintHalfEdge(label.str().c_str(), connected[i]);
        }
    }

    void debugPrintBeforeComputeVLoops(const zSpace::zIntArray& medialIds)
    {
        if (!m_debugComputeVLoops) return;

        std::cout << "[zSpaceBlendImport][computeVLoops] ===== begin call =====" << std::endl;
        debugPrintMeshStats("input mesh", m_mesh);

        std::cout << "[zSpaceBlendImport][computeVLoops] medialIds/longitudeCornerVIds:";
        for (int id : medialIds) std::cout << " " << id;
        std::cout << std::endl;

        if (medialIds.size() < 2) {
            std::cout << "[zSpaceBlendImport][computeVLoops] ERROR: need at least 2 ids." << std::endl;
            return;
        }

        debugPrintVertex(medialIds[0]);
        debugPrintVertex(medialIds[1]);

        zSpace::zFnMesh fn(m_mesh);
        zSpace::zItMeshHalfEdge he;
        if (fn.halfEdgeExists(medialIds[0], medialIds[1], he)) {
            debugPrintHalfEdge("corner edge forward", he);
            debugPrintHalfEdge("  forward.next", he.getNext());
            debugPrintHalfEdge("  forward.prev.sym", he.getPrev().getSym());
        }
        else {
            std::cout << "[zSpaceBlendImport][computeVLoops] missing halfEdge "
                << medialIds[0] << "->" << medialIds[1] << std::endl;
        }

        if (fn.halfEdgeExists(medialIds[1], medialIds[0], he)) {
            debugPrintHalfEdge("corner edge reverse", he);
            debugPrintHalfEdge("  reverse.next", he.getNext());
            debugPrintHalfEdge("  reverse.prev.sym", he.getPrev().getSym());
        }
        else {
            std::cout << "[zSpaceBlendImport][computeVLoops] missing halfEdge "
                << medialIds[1] << "->" << medialIds[0] << std::endl;
        }
    }

    void debugPrintLoopsAfterComputeVLoops()
    {
        if (!m_debugComputeVLoops) return;

        std::cout << "[zSpaceBlendImport][computeVLoops] output loops=" << m_loops.size() << std::endl;
        for (int i = 0; i < static_cast<int>(m_loops.size()); i++) {
            const auto& loop = m_loops[i];
            std::cout << "[zSpaceBlendImport][computeVLoops] loop[" << i << "] size=" << loop.size();

            if (!loop.empty()) {
                zSpace::zItMeshHalfEdge first = loop.front();
                zSpace::zItMeshHalfEdge last = loop.back();
                std::cout << " first=" << first.getStartVertex().getId() << "->" << first.getVertex().getId()
                    << " last=" << last.getStartVertex().getId() << "->" << last.getVertex().getId();
            }

            std::cout << std::endl;
        }

        debugPrintMeshStats("top mesh after computeVLoops", m_topMesh);
        debugPrintMeshStats("bottom mesh after computeVLoops", m_bottomMesh);
    }

    void debugPrintAfterScalarAndSections()
    {
        if (!m_debugComputeVLoops) return;

        double minScalar = std::numeric_limits<double>::max();
        double maxScalar = -std::numeric_limits<double>::max();
        int validScalarCount = 0;
        for (double s : m_scalars) {
            if (s < 0.0) continue;
            minScalar = std::min(minScalar, s);
            maxScalar = std::max(maxScalar, s);
            validScalarCount++;
        }

        std::cout << "[zSpaceBlendImport][computeVLoops] scalars total=" << m_scalars.size()
            << " valid=" << validScalarCount;
        if (validScalarCount > 0) std::cout << " range=[" << minScalar << "," << maxScalar << "]";
        std::cout << std::endl;

        std::cout << "[zSpaceBlendImport][computeVLoops] sectionMeshes=" << m_sectionMeshes.size()
            << " sectionGraphs=" << m_sectionGraphs.size()
            << " contourGraphs=" << m_contourGraphs.size()
            << std::endl;

        for (int i = 0; i < static_cast<int>(m_sectionMeshes.size()) && i < 8; i++) {
            std::ostringstream label;
            label << "sectionMesh[" << i << "]";
            debugPrintMeshStats(label.str().c_str(), m_sectionMeshes[i]);
        }
        std::cout << "[zSpaceBlendImport][computeVLoops] ===== end call =====" << std::endl;
    }

    void loadMesh()
    {
        std::string message;
        if (!alice2::loadMesh(m_meshPath, m_mesh, &message)) {
            m_loaded = false;
            m_status = "Could not read mesh: " + message;
            std::cout << "[zSpaceBlendImport] " << m_status << std::endl;
            return;
        }

        m_loops.clear();
        m_scalars.clear();
        m_sectionMeshes.clear();
        m_sectionGraphs.clear();
        m_sliceMetadata = alice2::SliceMetadata();
        clearSdfDebugData();
        loadBracingGraph();
        zSpace::zFnMesh fnTop(m_topMesh);
        zSpace::zFnMesh fnBottom(m_bottomMesh);
        fnTop.clear();
        fnBottom.clear();

        std::ostringstream out;
        zSpace::zFnMesh fn(m_mesh);
        out << "Read mesh vertices/faces: " << fn.numVertices() << "/" << fn.numPolygons()
            << " | bracing graphs=" << m_bracingGroups.size()
            << " | press 'p' for vloops/slices, 'o' for SDF after slices";

        m_status = out.str();
        m_loaded = true;
        m_showBlockMesh = true;
        m_currentSection = 0;
        std::cout << "[zSpaceBlendImport] " << m_status << std::endl;
        focusOnMesh();
    }

    bool computeSlices()
    {
        if (!m_loaded) {
            m_status = "No mesh loaded. Press 'r' first.";
            std::cout << "[zSpaceBlendImport] " << m_status << std::endl;
            return false;
        }

        zSpace::zIntArray medialIds = {
            alice2::SlicingParameters::longitudeCornerStartVertexId,
            alice2::SlicingParameters::longitudeCornerEndVertexId
        };
        m_loops.clear();
        m_scalars.clear();
        m_sectionMeshes.clear();
        m_sectionGraphs.clear();
        m_sliceMetadata = alice2::SliceMetadata();
        clearSdfDebugData();
        zSpace::zFnMesh fnTop(m_topMesh);
        zSpace::zFnMesh fnBottom(m_bottomMesh);
        fnTop.clear();
        fnBottom.clear();

        debugPrintBeforeComputeVLoops(medialIds);
        alice2::computeVLoops(m_mesh, medialIds, m_loops, m_topMesh, m_bottomMesh, &m_sliceMetadata);
        debugPrintLoopsAfterComputeVLoops();

        if (m_loops.empty()) {
            m_status = "computeVLoops failed: no longitude loops.";
            std::cout << "[zSpaceBlendImport] " << m_status << std::endl;
            return false;
        }

        alice2::computeGeodesicScalars(m_mesh, m_loops, m_scalars, true);
        alice2::computeGeodesicContours(m_loops, m_scalars,
            alice2::SlicingParameters::longitudeLayerSpacing,
            m_topMesh, m_bottomMesh, m_sectionMeshes);
        alice2::populateSliceMetadata(m_mesh, m_loops, m_sectionGraphs, m_sliceMetadata);
        markSectionMeshCornerVerticesById();
        alice2::createSectionGraphs(m_sectionMeshes, m_sectionGraphs);
        alice2::populateSliceMetadata(m_mesh, m_loops, m_sectionGraphs, m_sliceMetadata);
        buildLayerBracingGraphs();
        debugPrintAfterScalarAndSections();

        if (m_sectionGraphs.empty()) {
            m_status = "Slice compute failed: no section graphs.";
            std::cout << "[zSpaceBlendImport] " << m_status << std::endl;
            return false;
        }

        m_currentSection = 0;
        std::ostringstream out;
        out << "Computed slices: loops=" << m_loops.size()
            << " sectionMeshes=" << m_sectionMeshes.size()
            << " sectionGraphs=" << m_sectionGraphs.size()
            << " cornerLongitudes=" << m_sliceMetadata.cornerLongitudeIds.size()
            << " layerBracing=" << m_layerBracingGraphs.size()
            << " | press 'o' for SDF";
        m_status = out.str();
        std::cout << "[zSpaceBlendImport] " << m_status << std::endl;
        return true;
    }

    bool computeSdfField()
    {
        if (m_sectionMeshes.empty() || m_sectionGraphs.empty()) {
            m_status = "No slices available. Press 'p' before 'o'.";
            std::cout << "[zSpaceBlendImport] " << m_status << std::endl;
            return false;
        }

        clearSdfDebugData();
        if (m_layerBracingGraphs.empty()) buildLayerBracingGraphs();
        const int sdfLayerCount = static_cast<int>(std::min(m_sectionGraphs.size(), m_sectionMeshes.size()));
        m_debugSdfLayerCount = sdfLayerCount;
        alice2::computeSDFLayers(m_sectionGraphs, m_sectionMeshes, sdfLayerCount,
            m_contourGraphs, &m_sdfFields, &m_sdfFlatGraphs,
            &m_layerBracingGraphs, &m_flatBracingGraphs, &m_bracingSlotGraphs, &m_sdfDebugData);
        alice2::computeSDFPostProcess(m_sectionMeshes, m_contourGraphs, m_sdfDebugData, m_postProcessResult);

        int contourEdgeCount = 0;
        int fieldFaceCount = 0;
        int toolpathSampleCount = 0;
        for (int i = 0; i < static_cast<int>(m_contourGraphs.size()); i++) {
            std::ostringstream label;
            label << "contourGraph[" << i << "]";
            debugPrintGraphStats(label.str().c_str(), m_contourGraphs[i]);

            zSpace::zFnGraph fn(m_contourGraphs[i]);
            contourEdgeCount += fn.numEdges();
        }
        for (int i = 0; i < static_cast<int>(m_sdfFlatGraphs.size()); i++) {
            std::ostringstream label;
            label << "sdfFlatGraph[" << i << "]";
            debugPrintGraphStats(label.str().c_str(), m_sdfFlatGraphs[i]);
        }
        for (int i = 0; i < static_cast<int>(m_flatBracingGraphs.size()); i++) {
            std::ostringstream label;
            label << "flatBracingGraph[" << i << "]";
            debugPrintGraphStats(label.str().c_str(), m_flatBracingGraphs[i]);
        }
        for (int i = 0; i < static_cast<int>(m_bracingSlotGraphs.size()); i++) {
            std::ostringstream label;
            label << "bracingSlotGraph[" << i << "]";
            debugPrintGraphStats(label.str().c_str(), m_bracingSlotGraphs[i]);
        }
        for (int i = 0; i < static_cast<int>(m_sdfDebugData.flatBoundaryFeatureGraphs.size()); i++) {
            std::ostringstream label;
            label << "flatBoundaryFeatureGraph[" << i << "]";
            debugPrintGraphStats(label.str().c_str(), m_sdfDebugData.flatBoundaryFeatureGraphs[i]);
        }
        for (int i = 0; i < static_cast<int>(m_sdfDebugData.flatBracingFeatureGraphs.size()); i++) {
            std::ostringstream label;
            label << "flatBracingFeatureGraph[" << i << "]";
            debugPrintGraphStats(label.str().c_str(), m_sdfDebugData.flatBracingFeatureGraphs[i]);
        }

        for (int i = 0; i < static_cast<int>(m_sdfFields.size()); i++) {
            std::ostringstream label;
            label << "sdfField[" << i << "]";
            debugPrintMeshStats(label.str().c_str(), m_sdfFields[i]);

            zSpace::zFnMesh fn(m_sdfFields[i]);
            fieldFaceCount += fn.numPolygons();
        }
        for (int i = 0; i < static_cast<int>(m_postProcessResult.toolpathTargetPoints.size()); i++) {
            toolpathSampleCount += static_cast<int>(m_postProcessResult.toolpathTargetPoints[i].size());
            std::cout << "[zSpaceBlendImport][post] toolpath[" << i << "] samples="
                << m_postProcessResult.toolpathTargetPoints[i].size()
                << " widths=" << ((i < static_cast<int>(m_postProcessResult.toolpathPrintWidths.size())) ? m_postProcessResult.toolpathPrintWidths[i].size() : 0)
                << " heights=" << ((i < static_cast<int>(m_postProcessResult.toolpathPrintHeights.size())) ? m_postProcessResult.toolpathPrintHeights[i].size() : 0)
                << std::endl;
        }

        std::ostringstream out;
        out << "Computed SDF fields=" << m_sdfFields.size()
            << " debugLayers=" << m_debugSdfLayerCount
            << " fieldFaces=" << fieldFaceCount
            << " contourGraphs=" << m_contourGraphs.size()
            << " flatGraphs=" << m_sdfFlatGraphs.size()
            << " flatBracing=" << m_flatBracingGraphs.size()
            << " bracingSlots=" << m_bracingSlotGraphs.size()
            << " toolpathSamples=" << toolpathSampleCount
            << " contourEdges=" << contourEdgeCount;
        if (contourEdgeCount == 0) out << " | WARNING no contour edges";
        m_status = out.str();
        std::cout << "[zSpaceBlendImport] " << m_status << std::endl;
        return contourEdgeCount > 0 || fieldFaceCount > 0;
    }

    bool computePrintMeshes()
    {
        m_printMeshes.clear();
        if (m_postProcessResult.toolpathTargetPoints.empty()) {
            m_status = "No toolpath samples. Press 'o' before 'i'.";
            std::cout << "[zSpaceBlendImport] " << m_status << std::endl;
            return false;
        }

        m_printMeshes.assign(m_postProcessResult.toolpathTargetPoints.size(), zSpace::zObjMesh());

        auto safePerpendicular = [](zSpace::zVector dir) {
            zSpace::zVector ref = (std::fabs(dir.z) < 0.9f) ? zSpace::zVector(0, 0, 1) : zSpace::zVector(1, 0, 0);
            zSpace::zVector out = ref ^ dir;
            if (out.length() < 1e-6) out = zSpace::zVector(1, 0, 0);
            out.normalize();
            return out;
        };

        int meshCount = 0;
        int faceCount = 0;
        for (int graphId = 0; graphId < static_cast<int>(m_postProcessResult.toolpathTargetPoints.size()); graphId++) {
            const zSpace::zPointArray& points = m_postProcessResult.toolpathTargetPoints[graphId];
            const zSpace::zFloatArray& heights = m_postProcessResult.toolpathPrintHeights[graphId];
            const zSpace::zFloatArray& widths = m_postProcessResult.toolpathPrintWidths[graphId];
            const zSpace::zVectorArray& normals = m_postProcessResult.toolpathNormals[graphId];
            if (points.size() < 2 || heights.size() != points.size() || widths.size() != points.size()) continue;

            bool closed = false;
            if (graphId < static_cast<int>(m_postProcessResult.toolpathGraphs.size())) {
                zSpace::zFnGraph fnGraph(m_postProcessResult.toolpathGraphs[graphId]);
                zSpace::zIntArray edges;
                fnGraph.getEdgeData(edges);
                for (int e = 0; e + 1 < static_cast<int>(edges.size()); e += 2) {
                    if ((edges[e] == 0 && edges[e + 1] == static_cast<int>(points.size()) - 1) ||
                        (edges[e + 1] == 0 && edges[e] == static_cast<int>(points.size()) - 1)) {
                        closed = true;
                        break;
                    }
                }
            }

            zSpace::zPointArray meshPositions;
            zSpace::zIntArray polyCounts;
            zSpace::zIntArray polyConnects;
            meshPositions.reserve(points.size() * 4);

            for (int i = 0; i < static_cast<int>(points.size()); i++) {
                zSpace::zPoint p = points[i];
                zSpace::zVector pathDir;
                if (i == 0) {
                    zSpace::zPoint nextP = points[i + 1];
                    pathDir = p - nextP;
                }
                else if (i == static_cast<int>(points.size()) - 1) {
                    zSpace::zPoint prevP = points[i - 1];
                    pathDir = prevP - p;
                }
                else {
                    zSpace::zPoint prevP = points[i - 1];
                    zSpace::zPoint nextP = points[i + 1];
                    zSpace::zVector pre = prevP - p;
                    zSpace::zVector next = p - nextP;
                    pathDir = (pre + next) * 0.5f;
                }
                if (pathDir.length() < 1e-6) {
                    if (i + 1 < static_cast<int>(points.size())) {
                        zSpace::zPoint nextP = points[i + 1];
                        pathDir = nextP - p;
                    }
                    else {
                        zSpace::zPoint prevP = points[i - 1];
                        pathDir = p - prevP;
                    }
                }
                if (pathDir.length() < 1e-6) pathDir = zSpace::zVector(1, 0, 0);
                pathDir.normalize();

                zSpace::zVector yAxis = (i < static_cast<int>(normals.size())) ? normals[i] : zSpace::zVector(0, 0, 1);
                if (yAxis.length() < 1e-6) yAxis = zSpace::zVector(0, 0, 1);
                yAxis.normalize();
                yAxis -= pathDir * (yAxis * pathDir);
                if (yAxis.length() < 1e-6) yAxis = safePerpendicular(pathDir);
                yAxis.normalize();

                zSpace::zVector xAxis = yAxis ^ pathDir;
                if (xAxis.length() < 1e-6) xAxis = safePerpendicular(pathDir);
                xAxis.normalize();

                const float width = std::max(0.0f, widths[i]);
                const float height = std::max(0.0f, heights[i]);
                zSpace::zPoint origin = points[i];
                meshPositions.push_back(origin + (xAxis * (-width * 0.5f)));
                meshPositions.push_back(origin + (xAxis * (width * 0.5f)));
                meshPositions.push_back(origin + (xAxis * (width * 0.5f)) + (yAxis * height));
                meshPositions.push_back(origin + (xAxis * (-width * 0.5f)) + (yAxis * height));
            }

            const int segmentCount = closed ? static_cast<int>(points.size()) : static_cast<int>(points.size()) - 1;
            for (int i = 0; i < segmentCount; i++) {
                const int next = (i + 1) % static_cast<int>(points.size());
                for (int k = 0; k < 4; k++) {
                    polyCounts.push_back(4);
                    polyConnects.push_back((i * 4) + k);
                    polyConnects.push_back((next * 4) + k);
                    polyConnects.push_back((next * 4) + ((k + 1) % 4));
                    polyConnects.push_back((i * 4) + ((k + 1) % 4));
                }
            }

            if (!closed) {
                polyCounts.push_back(4);
                polyConnects.push_back(3);
                polyConnects.push_back(2);
                polyConnects.push_back(1);
                polyConnects.push_back(0);

                const int last = (static_cast<int>(points.size()) - 1) * 4;
                polyCounts.push_back(4);
                polyConnects.push_back(last);
                polyConnects.push_back(last + 1);
                polyConnects.push_back(last + 2);
                polyConnects.push_back(last + 3);
            }

            zSpace::zFnMesh fnMesh(m_printMeshes[graphId]);
            fnMesh.clear();
            fnMesh.create(meshPositions, polyCounts, polyConnects);
            meshCount++;
            faceCount += static_cast<int>(polyCounts.size());
        }

        std::ostringstream out;
        out << "Computed print meshes=" << meshCount << " faces=" << faceCount;
        m_status = out.str();
        std::cout << "[zSpaceBlendImport] " << m_status << std::endl;
        return meshCount > 0;
    }

    void toggleMeshAndPrintDisplay()
    {
        m_showBlockMesh = !m_showBlockMesh;
        m_showPrintMeshes = !m_showPrintMeshes;
        std::ostringstream out;
        out << "Display toggled: input mesh=" << (m_showBlockMesh ? "on" : "off")
            << " print meshes=" << (m_showPrintMeshes ? "on" : "off");
        m_status = out.str();
        std::cout << "[zSpaceBlendImport] " << m_status << std::endl;
    }

    bool changeSection(int delta)
    {
        const int sectionCount = static_cast<int>(m_sectionMeshes.size());
        if (sectionCount <= 0) {
            m_status = "No sections available.";
            std::cout << "[zSpaceBlendImport] " << m_status << std::endl;
            return true;
        }

        const int nextSection = m_currentSection + delta;
        if (nextSection < 0 || nextSection >= sectionCount) {
            std::cout << "[zSpaceBlendImport] section unchanged: " << (m_currentSection + 1)
                << "/" << sectionCount << std::endl;
            return true;
        }

        m_currentSection = nextSection;
        std::ostringstream out;
        out << "Section " << (m_currentSection + 1) << "/" << sectionCount
            << " | section graphs: " << m_sectionGraphs.size()
            << " | contour graphs: " << m_contourGraphs.size()
            << " | sdf flat graphs: " << m_sdfFlatGraphs.size()
            << " | flat bracing: " << m_flatBracingGraphs.size()
            << " | bracing slots: " << m_bracingSlotGraphs.size()
            << " | toolpath samples: " << ((m_currentSection < static_cast<int>(m_postProcessResult.toolpathTargetPoints.size())) ? m_postProcessResult.toolpathTargetPoints[m_currentSection].size() : 0)
            << " | print meshes: " << m_printMeshes.size()
            << " | sdf fields: " << m_sdfFields.size();
        m_status = out.str();
        std::cout << "[zSpaceBlendImport] " << m_status << std::endl;
        return true;
    }

    void focusOnMesh()
    {
        zSpace::zFnMesh fn(m_mesh);
        zSpace::zPoint minBB;
        zSpace::zPoint maxBB;
        fn.getBounds(minBB, maxBB);
        Application::getInstance()->getCameraController().focusOnBounds(toVec3(minBB), toVec3(maxBB));
    }

    void drawToolpathSamples(Renderer& renderer, int graphId)
    {
        if (graphId < 0 || graphId >= static_cast<int>(m_postProcessResult.toolpathTargetPoints.size())) return;

        const zSpace::zPointArray& points = m_postProcessResult.flatToolpathTargetPoints[graphId];
        const zSpace::zFloatArray& widths = m_postProcessResult.toolpathPrintWidths[graphId];
        const zSpace::zIntArray& featureFlags = m_postProcessResult.toolpathFeatureFlags[graphId];
        zSpace::zVectorArray flatNormals;
        flatNormals.assign(points.size(), zSpace::zVector(0, 0, 1));
        const zSpace::zVectorArray& normals = flatNormals;

        constexpr float fallbackPrintWidth = 0.048f;
        const int circleSegments = 24;
        for (int sampleId = 0; sampleId < static_cast<int>(points.size()); sampleId++) {
            zSpace::zPoint center = points[sampleId];
            float printWidth = (sampleId < static_cast<int>(widths.size())) ? widths[sampleId] : 0.0f;
            if (printWidth <= 0.0f) printWidth = fallbackPrintWidth;

            zSpace::zVector n = (sampleId < static_cast<int>(normals.size())) ? normals[sampleId] : zSpace::zVector(0, 0, 1);
            if (n.length() < 1e-6) n = zSpace::zVector(0, 0, 1);
            n.normalize();

            zSpace::zVector ref = (std::fabs(n.z) < 0.9f) ? zSpace::zVector(0, 0, 1) : zSpace::zVector(1, 0, 0);
            zSpace::zVector x = ref ^ n;
            if (x.length() < 1e-6) x = zSpace::zVector(1, 0, 0);
            x.normalize();
            zSpace::zVector y = n ^ x;
            y.normalize();

            const bool isFeature = sampleId < static_cast<int>(featureFlags.size()) && featureFlags[sampleId] == 1;
            const Color color = isFeature ? Color(1.0f, 0.45f, 0.0f, 1.0f) : Color(0.0f, 0.85f, 0.15f, 1.0f);
            const float radius = printWidth * 0.5f;
            for (int c = 0; c < circleSegments; c++) {
                const float a0 = (6.28318530718f * c) / static_cast<float>(circleSegments);
                const float a1 = (6.28318530718f * (c + 1)) / static_cast<float>(circleSegments);
                zSpace::zPoint p0 = center + (x * (std::cos(a0) * radius)) + (y * (std::sin(a0) * radius));
                zSpace::zPoint p1 = center + (x * (std::cos(a1) * radius)) + (y * (std::sin(a1) * radius));
                renderer.drawLine(toVec3(p0), toVec3(p1), color, isFeature ? 2.0f : 1.0f);
            }
            renderer.drawPoint(toVec3(center), color, isFeature ? 7.0f : 4.0f);
        }
    }

    void drawSections(Renderer& renderer)
    {
        if (!m_displaySections) return;

        for (BracingLineGroup& group : m_bracingGroups) {
            scene().draw(group.graph, Display::lines(group.color, 3.0f));
        }

        if (m_currentSection >= 0 && m_currentSection < static_cast<int>(m_sdfFields.size())) {
            zDisplayMeshSetting fieldDisplay;
            fieldDisplay.showFaces = true;
            fieldDisplay.showEdges = true;
            fieldDisplay.showVertices = false;
            fieldDisplay.useMeshColors = true;
            fieldDisplay.faceColor = Color(1.0f, 1.0f, 1.0f, 0.85f);
            fieldDisplay.edgeColor = Color(0.08f, 0.08f, 0.08f, 0.18f);
            fieldDisplay.edgeWidth = 0.2f;
            scene().draw(m_sdfFields[m_currentSection], fieldDisplay);
        }
        if (m_currentSection >= 0 && m_currentSection < static_cast<int>(m_sdfDebugData.flatContourGraphs.size())) {
            scene().draw(m_sdfDebugData.flatContourGraphs[m_currentSection], Display::lines(Color(1.0f, 0.0f, 0.85f, 1.0f), 5.0f));
        }
        if (m_currentSection >= 0 && m_currentSection < static_cast<int>(m_sdfFlatGraphs.size())) {
            scene().draw(m_sdfFlatGraphs[m_currentSection], Display::lines(Color(1.0f, 0.55f, 0.0f, 1.0f), 4.0f));
        }
        if (m_currentSection >= 0 && m_currentSection < static_cast<int>(m_flatBracingGraphs.size())) {
            scene().draw(m_flatBracingGraphs[m_currentSection], Display::lines(Color(0.0f, 0.75f, 1.0f, 1.0f), 4.0f));
        }
        if (m_currentSection >= 0 && m_currentSection < static_cast<int>(m_bracingSlotGraphs.size())) {
            scene().draw(m_bracingSlotGraphs[m_currentSection], Display::lines(Color(0.1f, 0.2f, 1.0f, 1.0f), 4.0f));
        }
        if (m_currentSection >= 0 && m_currentSection < static_cast<int>(m_layerBracingGraphs.size())) {
            scene().draw(m_layerBracingGraphs[m_currentSection], Display::lines(Color(0.0f, 0.45f, 1.0f, 0.75f), 2.5f));
        }
        if (m_currentSection >= 0 && m_currentSection < static_cast<int>(m_sectionMeshes.size())) {
            scene().draw(m_sectionMeshes[m_currentSection], Display::wireframe(Color(0.0f, 0.55f, 0.12f, 1.0f), 1.2f));
        }
        if (m_currentSection >= 0 && m_currentSection < static_cast<int>(m_sectionGraphs.size())) {
            scene().draw(m_sectionGraphs[m_currentSection], Display::lines(Color(0.0f, 0.9f, 0.1f, 1.0f), 2.0f));
        }
        if (m_currentSection >= 0 && m_currentSection < static_cast<int>(m_contourGraphs.size())) {
            scene().draw(m_contourGraphs[m_currentSection], Display::lines(Color(1.0f, 0.0f, 0.85f, 1.0f), 5.0f));
        }
        if (m_showPrintMeshes) {
            zDisplayMeshSetting printDisplay;
            printDisplay.showFaces = true;
            printDisplay.showEdges = true;
            printDisplay.showVertices = false;
            printDisplay.faceColor = Color(0.95f, 0.75f, 0.18f, 0.42f);
            printDisplay.edgeColor = Color(0.35f, 0.18f, 0.02f, 1.0f);
            printDisplay.edgeWidth = 0.8f;
            for (int i = 0; i < static_cast<int>(m_printMeshes.size()); i++) {
                scene().draw(m_printMeshes[i], printDisplay);
            }
        }
        drawToolpathSamples(renderer, m_currentSection);
    }
};

ALICE2_REGISTER_SKETCH_AUTO(zSpaceBlendImportSketch)

#endif // __MAIN__

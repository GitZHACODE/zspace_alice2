#define __MAIN__
#ifdef __MAIN__

#include <zspace/interface.h>

#include <alice2.h>
#include <sketches/SketchRegistry.h>
#include <slicer/zUnroller.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>

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

            drawSections();
        }

        renderer.setColor(Color(0.02f, 0.02f, 0.02f, 1.0f));
        renderer.drawString(getName(), 10, 28);
        renderer.drawString("Mesh: " + m_meshPath, 10, 50);
        renderer.drawString(m_status, 10, 72);
        renderer.drawString("'r' read mesh, 'p' compute vloops/slices, 'o' compute SDF, 'x' remove mesh, 'f' focus, 'd' toggle sections, 'w/s' or '['/']' section", 10, 94);
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

        if (key == 'x' || key == 'X') {
            removeBlockMesh();
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
    zSpace::zObjMesh m_mesh;
    std::vector<zSpace::zItMeshHalfEdgeArray> m_loops;
    zSpace::zObjMesh m_topMesh;
    zSpace::zObjMesh m_bottomMesh;
    zSpace::zScalarArray m_scalars;
    zSpace::zObjMeshArray m_sectionMeshes;
    zSpace::zObjGraphArray m_sectionGraphs;
    zSpace::zObjGraphArray m_contourGraphs;
    zSpace::zObjGraphArray m_sdfFlatGraphs;
    zSpace::zObjMeshScalarFieldArray m_sdfFields;
    std::string m_meshPath = "data/carbcomn/carbMesh.obj";
    std::string m_status = "Waiting for mesh.";
    bool m_loaded = false;
    bool m_showBlockMesh = false;
    bool m_displaySections = true;
    bool m_debugComputeVLoops = true;
    int m_currentSection = 0;

    static Vec3 toVec3(const zSpace::zVector& p)
    {
        return Vec3(static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z));
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
        m_contourGraphs.clear();
        m_sdfFlatGraphs.clear();
        m_sdfFields.clear();
        zSpace::zFnMesh fnTop(m_topMesh);
        zSpace::zFnMesh fnBottom(m_bottomMesh);
        fnTop.clear();
        fnBottom.clear();

        std::ostringstream out;
        zSpace::zFnMesh fn(m_mesh);
        out << "Read mesh vertices/faces: " << fn.numVertices() << "/" << fn.numPolygons()
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

        zSpace::zIntArray medialIds = {43, 66};
        m_loops.clear();
        m_scalars.clear();
        m_sectionMeshes.clear();
        m_sectionGraphs.clear();
        m_contourGraphs.clear();
        m_sdfFlatGraphs.clear();
        m_sdfFields.clear();
        zSpace::zFnMesh fnTop(m_topMesh);
        zSpace::zFnMesh fnBottom(m_bottomMesh);
        fnTop.clear();
        fnBottom.clear();

        debugPrintBeforeComputeVLoops(medialIds);
        alice2::computeVLoops(m_mesh, medialIds, m_loops, m_topMesh, m_bottomMesh);
        debugPrintLoopsAfterComputeVLoops();

        if (m_loops.empty()) {
            m_status = "computeVLoops failed: no longitude loops.";
            std::cout << "[zSpaceBlendImport] " << m_status << std::endl;
            return false;
        }

        alice2::computeGeodesicScalars(m_mesh, m_loops, m_scalars, true);
        alice2::computeGeodesicContours(m_loops, m_scalars, 0.01f, m_topMesh, m_bottomMesh, m_sectionMeshes);
        alice2::createSectionGraphs(m_sectionMeshes, m_sectionGraphs);
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

        m_contourGraphs.clear();
        m_sdfFlatGraphs.clear();
        m_sdfFields.clear();
        alice2::computeSDF(m_sectionGraphs, m_sectionMeshes, m_contourGraphs, &m_sdfFields, &m_sdfFlatGraphs);

        int contourEdgeCount = 0;
        int fieldFaceCount = 0;
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

        for (int i = 0; i < static_cast<int>(m_sdfFields.size()); i++) {
            std::ostringstream label;
            label << "sdfField[" << i << "]";
            debugPrintMeshStats(label.str().c_str(), m_sdfFields[i]);

            zSpace::zFnMesh fn(m_sdfFields[i]);
            fieldFaceCount += fn.numPolygons();
        }

        std::ostringstream out;
        out << "Computed SDF fields=" << m_sdfFields.size()
            << " fieldFaces=" << fieldFaceCount
            << " contourGraphs=" << m_contourGraphs.size()
            << " flatGraphs=" << m_sdfFlatGraphs.size()
            << " contourEdges=" << contourEdgeCount;
        if (contourEdgeCount == 0) out << " | WARNING no contour edges";
        m_status = out.str();
        std::cout << "[zSpaceBlendImport] " << m_status << std::endl;
        return contourEdgeCount > 0 || fieldFaceCount > 0;
    }

    void removeBlockMesh()
    {
        zSpace::zFnMesh fnMesh(m_mesh);
        fnMesh.clear();

        m_showBlockMesh = false;
        m_status = "Block mesh removed. Sections remain visible. Press 'r' to read again.";
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

    void drawSections()
    {
        if (!m_displaySections) return;

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
        if (m_currentSection >= 0 && m_currentSection < static_cast<int>(m_sdfFlatGraphs.size())) {
            scene().draw(m_sdfFlatGraphs[m_currentSection], Display::lines(Color(1.0f, 0.55f, 0.0f, 1.0f), 4.0f));
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
    }
};

ALICE2_REGISTER_SKETCH_AUTO(zSpaceBlendImportSketch)

#endif // __MAIN__

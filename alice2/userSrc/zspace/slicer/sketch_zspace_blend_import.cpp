#define __MAIN__
#ifdef __MAIN__

#include <zspace/interface.h>

#include <alice2.h>
#include <sketches/SketchRegistry.h>
#include <slicer/zUnroller.h>

#include <fstream>
#include <iostream>
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
        renderer.drawString("'r' reload, 'x' remove mesh, 'f' focus, 'd' toggle sections, 'w/s' or '['/']' section", 10, 94);
    }

    bool onKeyPress(unsigned char key, int, int) override
    {
        if (key == 'r' || key == 'R') {
            loadMesh();
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
    std::string m_meshPath = "data/carbcomn/carbMesh.obj";
    std::string m_status = "Waiting for mesh.";
    bool m_loaded = false;
    bool m_showBlockMesh = false;
    bool m_displaySections = true;
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

    void loadMesh()
    {
        std::string message;
        if (!alice2::loadMesh(m_meshPath, m_mesh, &message)) {
            m_loaded = false;
            m_status = "Could not read mesh: " + message;
            return;
        }

        zSpace::zIntArray medialIds;
        zSpace::zIntArray featuredNumStrides;
        // const bool hasMedialIds = readIntArrayAttribute(m_meshPath, "MedialStartEnd", medialIds);
        // const bool hasStrides = readIntArrayAttribute(m_meshPath, "FeaturedNumStrides", featuredNumStrides);
        const bool hasMedialIds = false;
        const bool hasStrides = false;
        m_loops.clear();
        m_scalars.clear();
        m_sectionMeshes.clear();
        m_sectionGraphs.clear();
        m_contourGraphs.clear();

        bool builtSections = false;
        if (hasMedialIds && hasStrides) {
            zSpace::zVector norm(0, 0, 1);
            alice2::computeVLoops(m_mesh, medialIds, featuredNumStrides, norm, m_loops, m_topMesh, m_bottomMesh);
            alice2::computeGeodesicScalars(m_mesh, m_loops, m_scalars, true);
            alice2::computeGeodesicContours(m_loops, m_scalars, 0.01f, m_topMesh, m_bottomMesh, m_sectionMeshes);
            alice2::createSectionGraphs(m_sectionMeshes, m_sectionGraphs);
            alice2::computeSDF(m_sectionGraphs, m_sectionMeshes, m_contourGraphs);
            builtSections = !m_sectionGraphs.empty();
        }

        zSpace::zFnMesh fn(m_mesh);
        std::ostringstream out;
        out << "Loaded vertices/faces: " << fn.numVertices() << "/" << fn.numPolygons();
        if (builtSections) {
            out << " | sections: " << m_sectionGraphs.size();
            out << " | section meshes: " << m_sectionMeshes.size();
        }
        else {
            out << " | sections unavailable";
            if (!hasMedialIds) out << " | missing MedialStartEnd";
            if (!hasStrides) out << " | missing FeaturedNumStrides";
        }

        m_status = out.str();
        m_loaded = true;
        m_showBlockMesh = true;
        m_currentSection = 0;
        std::cout << "[zSpaceBlendImport] " << m_status << std::endl;
        focusOnMesh();
    }

    void removeBlockMesh()
    {
        zSpace::zFnMesh fnMesh(m_mesh);
        fnMesh.clear();

        m_showBlockMesh = false;
        m_status = "Block mesh removed. Sections remain visible. Press 'r' to reload.";
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
            << " | contour graphs: " << m_contourGraphs.size();
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

        if (m_currentSection >= 0 && m_currentSection < static_cast<int>(m_sectionMeshes.size())) {
            scene().draw(m_sectionMeshes[m_currentSection], Display::wireframe(Color(0.0f, 0.7f, 0.15f, 1.0f), 2.0f));
        }
        if (m_currentSection >= 0 && m_currentSection < static_cast<int>(m_sectionGraphs.size())) {
            scene().draw(m_sectionGraphs[m_currentSection], Display::lines(Color(0.0f, 0.9f, 0.1f, 1.0f), 3.0f));
        }
        if (m_currentSection >= 0 && m_currentSection < static_cast<int>(m_contourGraphs.size())) {
            scene().draw(m_contourGraphs[m_currentSection], Display::lines(Color(0.0f, 0.2f, 1.0f, 1.0f), 3.0f));
        }
    }
};

ALICE2_REGISTER_SKETCH_AUTO(zSpaceBlendImportSketch)

#endif // __MAIN__

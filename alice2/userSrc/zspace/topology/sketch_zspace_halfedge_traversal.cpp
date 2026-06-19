// #define __MAIN__
#ifdef __MAIN__

#include <zspace/interface.h>
#include <zspace/io.h>

#include <alice2.h>
#include <sketches/SketchRegistry.h>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

using namespace alice2;

class zSpaceHalfedgeTraversalSketch : public ISketch {
public:
    std::string getName() const override { return "zSpace Halfedge Traversal"; }
    std::string getDescription() const override { return "Loads data/halfedge_input.obj and traverses mesh halfedges with n, p, and s."; }
    std::string getAuthor() const override { return "alice2 + zspace_core"; }

    void setup() override
    {
        scene().setBackgroundColor(Color(0.96f, 0.96f, 0.94f, 1.0f));
        scene().setShowGrid(true);
        scene().setGridSize(4.0f);
        scene().setGridDivisions(8);
        scene().setShowAxes(false);
        scene().setAxesLength(1.2f);

        camera().setOrbitCenter(Vec3(0.0f, 0.0f, 0.0f));
        camera().setOrbitDistance(4.5f);
        camera().updateCamera();

        loadMesh();
    }

    void update(float) override
    {
        if (m_loaded && !m_screenshotTaken) {
            m_frameCount++;
            if (m_frameCount > 10) {
                // Focus the camera on bounds to make sure the mesh is centered and big
                zSpace::zPoint minBB;
                zSpace::zPoint maxBB;
                zSpace::zFnMesh fn(m_mesh);
                fn.getBounds(minBB, maxBB);
                Vec3 bMin = toVec3(minBB);
                Vec3 bMax = toVec3(maxBB);
                Application::getInstance()->getCameraController().focusOnBounds(bMin, bMax);

                // Take screenshot
                Application::getInstance()->takeScreenshot();
                m_screenshotTaken = true;
            }
        }
    }

    void draw(Renderer& renderer, Camera&) override
    {
        if (m_loaded) {
            zDisplayMeshSetting meshDisplay;
            meshDisplay.showFaces = true;
            meshDisplay.showEdges = true;
            meshDisplay.showVertices = true;
            //meshDisplay.faceColor = Color(0.82f, 0.84f, 0.86f, 0.55f);
            meshDisplay.edgeColor = Color(0.02f, 0.02f, 0.02f, 1.0f);
            meshDisplay.vertexColor = Color(0.02f, 0.02f, 0.02f, 1.0f);
            meshDisplay.edgeWidth = 1.5f;
            meshDisplay.vertexSize = 4.0f;
            scene().draw(m_mesh, meshDisplay);

            drawHalfedgeSet(renderer);
        }

        renderer.drawString("zSpace halfedge traversal", 10, 18);
        renderer.drawString("USD: " + m_objPath, 10, 36);
        renderer.drawString("n: next   p: previous   s: symmetry/twin   e: export USD", 10, 54);
        renderer.drawString(m_status, 10, 72);
    }

    bool onKeyPress(unsigned char key, int, int) override
    {
        if (!m_loaded) return false;

        try {
            zSpace::zItMeshHalfEdge current(m_mesh, m_currentHalfedgeId);
            if (key == 'n' || key == 'N') {
                setCurrent(current.getNext(), "next");
                return true;
            }
            if (key == 'p' || key == 'P') {
                setCurrent(current.getPrev(), "previous");
                return true;
            }
            if (key == 's' || key == 'S') {
                setCurrent(current.getSym(), "symmetry");
                return true;
            }
            if (key == 'e' || key == 'E') {
                std::string exportPath = "data/halfedge_export.usda";
                auto result = zSpace::zIO::writeMesh(exportPath, m_mesh);
                if (result) {
                    m_status = "Exported mesh to " + exportPath;
                } else {
                    m_status = "Export failed: " + result.message();
                }
                return true;
            }
        }
        catch (const std::exception& e) {
            m_status = std::string("Traversal failed: ") + e.what();
            return true;
        }

        return false;
    }

private:
    zSpace::zObjectMesh m_mesh;
    std::string m_objPath = "data/halfedge_import.usda";
    std::string m_status = "Waiting for mesh.";
    int m_currentHalfedgeId = 0;
    bool m_loaded = false;
    int m_frameCount = 0;
    bool m_screenshotTaken = false;

    static Vec3 toVec3(const zSpace::zVector& p)
    {
        return Vec3(static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z));
    }

    void loadMesh()
    {
        auto result = zSpace::zIO::readMesh(m_objPath, m_mesh);
        if (!result) {
            m_loaded = false;
            m_status = "Could not read USD: " + result.message();
            return;
        }

        zSpace::zFnMesh fn(m_mesh);
        if (fn.numHalfEdges() <= 0) {
            m_loaded = false;
            m_status = "OBJ loaded, but no halfedges were created.";
            return;
        }

        m_currentHalfedgeId = firstInteriorHalfedge(fn);
        m_loaded = true;
        updateStatus("loaded");

        // Focus the camera on the mesh bounds
        zSpace::zPoint minBB;
        zSpace::zPoint maxBB;
        fn.getBounds(minBB, maxBB);
        Vec3 bMin = toVec3(minBB);
        Vec3 bMax = toVec3(maxBB);
        Application::getInstance()->getCameraController().focusOnBounds(bMin, bMax);
    }

    int firstInteriorHalfedge(zSpace::zFnMesh& fn)
    {
        for (int i = 0; i < fn.numHalfEdges(); ++i) {
            try {
                zSpace::zItMeshHalfEdge he(m_mesh, i);
                zSpace::zItMeshHalfEdge sym = he.getSym();
                if (he.isActive() && sym.isActive() && !he.onBoundary() && !sym.onBoundary()) {
                    return i;
                }
            }
            catch (...) {
            }
        }
        return 0;
    }

    void setCurrent(zSpace::zItMeshHalfEdge he, const std::string& action)
    {
        if (!he.isActive()) {
            m_status = "Target halfedge is inactive.";
            return;
        }

        m_currentHalfedgeId = he.getId();
        updateStatus(action);
    }

    void updateStatus(const std::string& action)
    {
        zSpace::zFnMesh fn(m_mesh);
        std::ostringstream out;
        out << "Action: " << action
            << " | current halfedge: " << m_currentHalfedgeId
            << " | V/E/HE/F: " << fn.numVertices()
            << "/" << fn.numEdges()
            << "/" << fn.numHalfEdges()
            << "/" << fn.numPolygons();
        m_status = out.str();
    }

    bool halfedgePoints(zSpace::zItMeshHalfEdge he, Vec3& a, Vec3& b)
    {
        try {
            std::vector<zSpace::zVector> points;
            he.getVertexPositions(points);
            if (points.size() < 2) return false;

            a = toVec3(points[0]);
            b = toVec3(points[1]);
            return true;
        }
        catch (...) {
            return false;
        }
    }

    Vec3 faceOffset(zSpace::zItMeshHalfEdge he, const Vec3& midpoint, float edgeLength)
    {
        try {
            Vec3 faceCenter = toVec3(he.getFace().getCenter());
            Vec3 offset = faceCenter - midpoint;
            if (offset.length() <= 1e-5f) return Vec3();
            return offset.normalized() * (edgeLength * 0.1f);
        }
        catch (...) {
            return Vec3();
        }
    }

    void drawDirectedHalfedge(Renderer& renderer, zSpace::zItMeshHalfEdge he, const Color& color, float width, const std::string& label)
    {
        Vec3 a;
        Vec3 b;
        if (!halfedgePoints(he, a, b)) return;

        Vec3 dir = b - a;
        const float len = dir.length();
        if (len <= 1e-5f) return;

        dir = dir / len;
        const float segmentLength = len * 0.1f;
        const Vec3 midpoint = (a + b) * 0.5f;
        const Vec3 inwardOffset = faceOffset(he, midpoint, len);
        const Vec3 start = midpoint - dir * (segmentLength * 0.5f) + inwardOffset;
        const Vec3 end = midpoint + dir * (segmentLength * 0.5f) + inwardOffset;

        const Vec3 up(0.0f, 0.0f, 1.0f);
        Vec3 side = up.cross(dir);
        if (side.length() <= 1e-5f) side = Vec3(1.0f, 0.0f, 0.0f);
        side = side.normalized();

        const float headLength = len * 0.03f;
        const float headWidth = len * 0.02f;
        const Vec3 arrowBase = end - dir * headLength;
        const Vec3 wingA = arrowBase + side * headWidth;
        const Vec3 wingB = arrowBase - side * headWidth;
        const Vec3 labelPos = (start + end) * 0.5f + inwardOffset.normalized() * (len * 0.05f);

        renderer.drawLine(start, end, color, width);
        renderer.drawLine(end, wingA, color, width);
        renderer.drawLine(end, wingB, color, width);
        renderer.drawPoint(end, color, 6.0f);
        renderer.drawText(label, labelPos, 0.16f);
    }

    void drawHalfedgeSet(Renderer& renderer)
    {
        try {
            zSpace::zItMeshHalfEdge current(m_mesh, m_currentHalfedgeId);
            drawDirectedHalfedge(renderer, current, Color(0.0f, 0.0f, 0.0f, 1.0f), 5.0f, "C");
            drawDirectedHalfedge(renderer, current.getNext(), Color(0.0f, 1.0f, 1.0f, 1.0f), 4.0f, "N");
            drawDirectedHalfedge(renderer, current.getPrev(), Color(0.0f, 1.0f, 0.0f, 1.0f), 4.0f, "P");
            drawDirectedHalfedge(renderer, current.getSym(), Color(1.0f, 0.0f, 1.0f, 1.0f), 4.0f, "S");
        }
        catch (const std::exception& e) {
            m_status = std::string("Draw traversal failed: ") + e.what();
        }
    }
};

ALICE2_REGISTER_SKETCH_AUTO(zSpaceHalfedgeTraversalSketch)

#endif

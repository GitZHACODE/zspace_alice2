// #define __MAIN__
#ifdef __MAIN__

#include <zspace/interface.h>

#include <alice2.h>
#include <sketches/SketchRegistry.h>

#include <cmath>

using namespace alice2;

class zSpaceBaseSketch : public ISketch {
public:
    std::string getName() const override { return "zSpace Base Sketch"; }
    std::string getDescription() const override { return "Creates zSpace geometry and draws it with scene().draw(...)"; }
    std::string getAuthor() const override { return "alice2 + zspace_core"; }

    void setup() override
    {
        scene().setBackgroundColor(Color(0.12f, 0.12f, 0.14f));
        scene().setShowGrid(true);
        scene().setGridSize(8.0f);
        scene().setGridDivisions(8);
        scene().setShowAxes(true);
        scene().setAxesLength(2.0f);

        zSpace::zPointArray positions;
        positions.push_back(zSpace::zPoint(-1.0f, -1.0f, 0.0f));
        positions.push_back(zSpace::zPoint(1.0f, -1.0f, 0.0f));
        positions.push_back(zSpace::zPoint(1.0f, 1.0f, 0.0f));
        positions.push_back(zSpace::zPoint(-1.0f, 1.0f, 0.0f));
        positions.push_back(zSpace::zPoint(0.0f, 0.0f, 1.4f));

        zSpace::zIntArray faceCounts = {4, 3, 3, 3, 3};
        zSpace::zIntArray faceConnects = {
            0, 3, 2, 1,
            0, 1, 4,
            1, 2, 4,
            2, 3, 4,
            3, 0, 4
        };

        zSpace::zFnMesh meshFn(mesh);
        meshFn.create(positions, faceCounts, faceConnects);

        zSpace::zPointArray graphPositions;
        graphPositions.push_back(zSpace::zPoint(-2.0f, 0.0f, 0.0f));
        graphPositions.push_back(zSpace::zPoint(-2.0f, 1.0f, 0.5f));
        graphPositions.push_back(zSpace::zPoint(-2.0f, -1.0f, 0.5f));

        zSpace::zIntArray edgeConnects = {0, 1, 0, 2, 1, 2};
        zSpace::zFnGraph graphFn(graph);
        graphFn.create(graphPositions, edgeConnects);

        zSpace::zPointArray pointPositions;
        for (int i = 0; i < 16; ++i) {
            const float t = static_cast<float>(i) * 0.39269908f;
            pointPositions.push_back(zSpace::zPoint(2.0f + std::cos(t), std::sin(t), 0.08f * i));
        }

        zSpace::zFnPointCloud pointsFn(points);
        pointsFn.create(pointPositions);
    }

    void update(float /*deltaTime*/) override {}

    void draw(Renderer& renderer, Camera& /*camera*/) override
    {
        zDisplayMeshSetting meshDisplay;
        meshDisplay.showVertices = true;
        meshDisplay.showEdges = true;
        meshDisplay.showFaces = true;
        meshDisplay.faceColor = Color(0.35f, 0.55f, 0.95f, 1.0f);
        meshDisplay.edgeColor = Color(0.02f, 0.02f, 0.03f, 1.0f);
        meshDisplay.vertexColor = Color(1.0f, 0.85f, 0.15f, 1.0f);
        meshDisplay.vertexSize = 6.0f;
        meshDisplay.edgeWidth = 1.5f;

        scene().draw(mesh, meshDisplay);
        scene().draw(graph, Display::lines(Color(0.95f, 0.25f, 0.18f, 1.0f), 2.5f));
        scene().draw(points, Display::points(Color(0.2f, 1.0f, 0.65f, 1.0f), 5.0f));

        renderer.setColor(Color(1.0f, 1.0f, 1.0f));
        renderer.drawString(getName(), 10, 30);
        renderer.drawString("Use zSpace for geometry. Use scene().draw(...) for display.", 10, 52);
        renderer.drawString("'N' next sketch, 'P' previous sketch, 'F' focus", 10, 74);
    }

private:
    zSpace::zObjectMesh mesh;
    zSpace::zObjectGraph graph;
    zSpace::zObjectPointCloud points;
};

ALICE2_REGISTER_SKETCH_AUTO(zSpaceBaseSketch)

#endif // __MAIN__

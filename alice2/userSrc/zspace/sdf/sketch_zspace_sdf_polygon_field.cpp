//#define __MAIN__
#ifdef __MAIN__

#include <zspace/interface.h>
#include <zspace/zInterface/functionsets/zFnMeshField.h>
#include <zspace/zInterface/objects/zObjMeshField.h>

#include <alice2.h>
#include <sketches/SketchRegistry.h>

#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

using namespace alice2;

class zSpaceSdfPolygonFieldSketch : public ISketch {
public:
    std::string getName() const override { return "zSpace SDF Polygon Field"; }
    std::string getDescription() const override { return "Creates a polygon graph, samples zSpace polygon SDF values, and draws the zero isocontour."; }
    std::string getAuthor() const override { return "alice2 + zspace_core"; }

    void setup() override
    {
        scene().setBackgroundColor(Color(0.94f, 0.94f, 0.91f, 1.0f));
        scene().setShowGrid(true);
        scene().setGridSize(4.0f);
        scene().setGridDivisions(8);
        scene().setShowAxes(true);
        scene().setAxesLength(1.2f);

        createPolygon();
        computeSdfField();

        Application::getInstance()->getCameraController().focusOnBounds(Vec3(-1.7f, -1.7f, -0.1f), Vec3(1.7f, 1.7f, 0.1f));
    }

    void update(float) override {}

    void draw(Renderer& renderer, Camera&) override
    {
        zDisplayMeshSetting fieldDisplay;
        fieldDisplay.showFaces = true;
        fieldDisplay.showEdges = false;
        fieldDisplay.showVertices = false;
        fieldDisplay.useMeshColors = true;
        fieldDisplay.faceColor = Color(0.85f, 0.85f, 0.85f, 0.8f);
        scene().draw(m_field, fieldDisplay);

        scene().draw(m_polygon, Display::lines(Color(0.02f, 0.02f, 0.02f, 1.0f), 4.0f));
        scene().draw(m_contour, Display::lines(Color(1.0f, 0.0f, 0.75f, 1.0f), 4.0f));

        renderer.setColor(Color(0.02f, 0.02f, 0.02f, 1.0f));
        renderer.drawString(getName(), 10, 28);
        renderer.drawString(m_status, 10, 50);
        renderer.drawString("Black: input polygon | Magenta: SDF 0.0 isocontour | Field colors: zFieldSDF", 10, 72);
    }

private:
    zSpace::zObjGraph m_polygon;
    zSpace::zObjGraph m_contour;
    zSpace::zObjMeshScalarField m_field;
    std::string m_status;

    void createPolygon()
    {
        zSpace::zPointArray positions = {
            zSpace::zPoint(-0.85, -0.75, 0.0),
            zSpace::zPoint(0.85, -0.65, 0.0),
            zSpace::zPoint(1.05, 0.25, 0.0),
            zSpace::zPoint(0.15, 0.95, 0.0),
            zSpace::zPoint(-0.95, 0.55, 0.0)
        };

        zSpace::zIntArray edgeConnects = {
            0, 1,
            1, 2,
            2, 3,
            3, 4,
            4, 0
        };

        zSpace::zFnGraph fnPolygon(m_polygon);
        fnPolygon.create(positions, edgeConnects);
        fnPolygon.setEdgeColor(zSpace::zColor(0, 0, 0, 1));
        fnPolygon.setEdgeWeight(4);
    }

    void computeSdfField()
    {
        constexpr int fieldResX = 180;
        constexpr int fieldResY = 180;
        constexpr float sdfColorBand = 0.08f;

        zSpace::zFnMeshScalarField fnField(m_field);
        fnField.create(zSpace::zPoint(-1.5, -1.5, 0.0), zSpace::zPoint(1.5, 1.5, 0.0), fieldResX, fieldResY, 1, true, false);
        zSpace::zDomainColor colorDomain(zSpace::zBLUE, zSpace::zRED);
        fnField.setFieldColorDomain(colorDomain);

        zSpace::zScalarArray sdfValues;
        fnField.getScalars_Polygon(sdfValues, m_polygon, false);

        if (sdfValues.size() != fnField.numFieldValues()) {
            std::ostringstream out;
            out << "SDF failed: scalar count " << sdfValues.size() << " != field values " << fnField.numFieldValues();
            m_status = out.str();
            return;
        }

        double minValue = std::numeric_limits<double>::max();
        double maxValue = -std::numeric_limits<double>::max();
        for (double value : sdfValues) {
            minValue = std::min(minValue, value);
            maxValue = std::max(maxValue, value);
        }

        fnField.setFieldValues(sdfValues, zSpace::zFieldSDF, sdfColorBand);
        fnField.getIsocontour(m_contour, 0.0f, 3, 0.001f);

        zSpace::zFnGraph fnContour(m_contour);
        fnContour.setEdgeColor(zSpace::zColor(1, 0, 1, 1));
        fnContour.setEdgeWeight(4);

        std::ostringstream out;
        out << std::fixed << std::setprecision(4)
            << "Field " << fieldResX << "x" << fieldResY
            << " | SDF range [" << minValue << ", " << maxValue << "]"
            << " | contour V/E " << fnContour.numVertices() << "/" << fnContour.numEdges()
            << " | sdfColorBand " << sdfColorBand;
        m_status = out.str();
    }
};

ALICE2_REGISTER_SKETCH_AUTO(zSpaceSdfPolygonFieldSketch)

#endif // __MAIN__

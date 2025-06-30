// alice2 Simple Scalar Field Test
// A minimal test to verify scalar field functionality

#define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>
#include <computeGeom/scalarField.h>
#include <cmath>

using namespace alice2;

class SimpleScalarFieldSketch : public ISketch {
private:
    ScalarField2D m_scalarField;
    ScalarField2D m_scalarField_other;
    std::vector<ContourData> m_isolines;
    float m_time;
    bool m_fieldGenerated;
    bool d_iso = false;
    bool d_values = false;

    float contour_spacing = 0.0f;


public:
    SimpleScalarFieldSketch() 
        : m_time(0.0f)
        , m_fieldGenerated(false)
    {}
    ~SimpleScalarFieldSketch() = default;

    // Sketch information
    std::string getName() const override {
        return "Simple Scalar Field Test";
    }

    std::string getDescription() const override {
        return "Minimal scalar field test with basic circle SDF";
    }

    std::string getAuthor() const override {
        return "alice2 Test";
    }

    // Sketch lifecycle
    void setup() override {
        scene().setBackgroundColor(Vec3(0.1f, 0.1f, 0.1f));
        std::cout << "Simple Scalar Field Test loaded" << std::endl;

        scene().setShowGrid(false);
        scene().setGridSize(25.0f);
        scene().setGridDivisions(6);
        scene().setShowAxes(true);
        scene().setAxesLength(5.0f);

        // Generate a simple circle field using new API
        try {
            std::cout << "Creating ScalarField2D with modern API..." << std::endl;
            // Create field with custom bounds and resolution
            m_scalarField = ScalarField2D(Vec3(-10, -10, 0), Vec3(10, 10, 0), 100, 100);
            m_scalarField_other = ScalarField2D(Vec3(-10, -10, 0), Vec3(10, 10, 0), 100, 100);
            std::cout << "ScalarField2D created successfully" << std::endl;

            std::cout << "Applying circle SDF..." << std::endl;
            // m_scalarField.clear_field();
            // m_scalarField.apply_scalar_circle(Vec3(0, 0, 0), 5.0f);
            std::cout << "Circle SDF applied successfully" << std::endl;

            // Test the new getter methods
            auto [res_x, res_y] = m_scalarField.get_resolution();
            auto [min_bounds, max_bounds] = m_scalarField.get_bounds();
            std::cout << "Field resolution: " << res_x << "x" << res_y << std::endl;
            std::cout << "Field bounds: (" << min_bounds.x << "," << min_bounds.y << ") to ("
                      << max_bounds.x << "," << max_bounds.y << ")" << std::endl;

            m_fieldGenerated = true;
        }
        catch (const std::exception& e) {
            std::cout << "Error in setup: " << e.what() << std::endl;
            m_fieldGenerated = false;
        }
    }

    void update(float deltaTime) override {
        m_time += deltaTime;

        float r = 5.0f + std::sin(m_time) * 2.0f;
        m_scalarField_other.apply_scalar_circle(Vec3(0, 0, 0), r);

        float x = std::sin(m_time) + 5.0f;
        float y = std::cos(m_time) + 5.0f;
        m_scalarField.apply_scalar_rect(Vec3(0, 0, 0), Vec3(x, y, 0), 0.0f);
        m_scalarField.boolean_smin(m_scalarField_other, 0.2f);

        //m_scalarField.boolean_union(m_scalarField_other);
        //m_scalarField.boolean_subtract(m_scalarField_other);


        if (m_isolines.size() < 100 && (int)deltaTime%10 == 0)
        {
            ContourData contour = m_scalarField.get_contours(0.0f);
            for (auto &line : contour.line_segments)
            {
                line.first = Vec3(line.first.x, line.first.y, line.first.z + contour_spacing);
                line.second = Vec3(line.second.x, line.second.y, line.second.z + contour_spacing);
            }
            m_isolines.push_back(contour);
            contour_spacing += 0.2f;
        }
    }

    void draw(Renderer& renderer, Camera& camera) override {
        // Draw a test point to verify rendering works
        renderer.drawPoint(Vec3(0, 0, 0), Vec3(1.0f, 0.0f, 0.0f), 10.0f);
        
        if (m_fieldGenerated) {
            try {
                // Draw field points using new API
                m_scalarField.draw_points(renderer, 1); // Every 6th point for performance

                // Optionally draw some scalar values as text
                if (d_values) { // Every 3 seconds
                    m_scalarField.draw_values(renderer, 10); // Show values at sparse grid
                }
            }
            catch (const std::exception& e) {
                std::cout << "Error drawing field: " << e.what() << std::endl;
            }
        }

        if(d_iso)
        {
            // for (int i = 0; i < 10; ++i) {
            //     m_scalarField.drawIsocontours(renderer, 0.5f + i * i * 0.5f);
            // }
            //m_scalarField.drawIsocontours(renderer, 0.5f);

            for(auto &contour : m_isolines)
            {
                for(auto& line : contour.line_segments)
                renderer.drawLine(line.first,line.second, Vec3(1.0f, 1.0f, 1.0f), 2.0f);
            }
        }


        // 2D text rendering
        renderer.setColor(Vec3(1.0f, 1.0f, 1.0f));
        renderer.drawString(getName(), 10, 30);
        renderer.drawString(getDescription(), 10, 50);

        renderer.setColor(Vec3(0.0f, 1.0f, 1.0f));
        renderer.drawString("FPS: " + std::to_string((Application::getInstance()->getFPS())), 10, 70);

        renderer.setColor(Vec3(1.0f, 1.0f, 0.0f));
        renderer.drawString("Field Generated: " + std::string(m_fieldGenerated ? "YES" : "NO"), 10, 100);

        renderer.setColor(Vec3(0.75f, 0.75f, 0.75f));
        renderer.drawString("Controls:", 10, 140);
        renderer.drawString("'R' - Regenerate field", 10, 160);
        renderer.drawString("'B' - Apply voronoi", 10, 180);
        renderer.drawString("'C' - Regenerate iso contours", 10, 200);
        renderer.drawString("'V' - Shwo field values", 10, 220);
    }

    void cleanup() override {
        std::cout << "Simple Scalar Field Test cleanup" << std::endl;
    }

    // Input handling
    bool onKeyPress(unsigned char key, int x, int y) override {
        switch (key) {
            case 'r':
            case 'R':
                try {
                    std::cout << "Regenerating field with new API..." << std::endl;
                    m_scalarField.clear_field();
                    m_scalarField.apply_scalar_circle(Vec3(0, 0, 0), 1.0f);
                    m_fieldGenerated = true;
                    std::cout << "Field regenerated successfully" << std::endl;
                }
                catch (const std::exception& e) {
                    std::cout << "Error regenerating field: " << e.what() << std::endl;
                    m_fieldGenerated = false;
                }
                return true;

            case 'b':
            case 'B':
                try {
                    m_scalarField.addVoronoi({
                        Vec3(+0, +0, 0),
                        Vec3(+2, +2, 0), 
                        Vec3(-2, -2, 0),
                        Vec3(-1, +1, 0),
                        Vec3(+1, -1, 0),
                        });
                }
                catch (const std::exception& e) {
                    std::cout << "Error in boolean operation: " << e.what() << std::endl;
                }
                return true;
            case 'c':
            case 'C':
            //m_scalarField.computeIsocontours(0.5, m_isolines);
            d_iso = true;
            return true;

            case 'v':
            case 'V':
                d_values = !d_values;
                return true;

        }
        return false;
    }
};

// Register the sketch with alice2
//ALICE2_REGISTER_SKETCH_AUTO(SimpleScalarFieldSketch)

#endif // __MAIN__

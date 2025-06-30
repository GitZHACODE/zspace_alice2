// alice2 Scalar Field Educational Sketch 1: Basic Field Construction
// Demonstrates basic circle and rectangle scalar field generation with animations

#define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>
#include <computeGeom/scalarField.h>
#include <cmath>

using namespace alice2;

class ScalarField01BasicSketch : public ISketch {
private:
    // Scalar fields with consistent dimensions: 100x100 grid, bounds (-50, -50) to (50, 50)
    ScalarField2D m_scalarField;
    
    // Animation and timing
    float m_time;
    
    // Boolean flags for computation controls (prefix with "b_")
    bool b_computeCircle;
    bool b_computeRect;
    
    // Boolean flags for visualization controls (prefix with "d_")
    bool d_drawField;
    bool d_drawValues;
    bool d_drawContours;
    
    // Animation parameters
    float m_circleRadius;
    Vec3 m_rectSize;
    
    // Contour animation
    float m_contourOffset;

public:
    ScalarField01BasicSketch() 
        : m_scalarField(Vec3(-50, -50, 0), Vec3(50, 50, 0), 100, 100)
        , m_time(0.0f)
        , b_computeCircle(true)
        , b_computeRect(false)
        , d_drawField(true)
        , d_drawValues(false)
        , d_drawContours(true)
        , m_circleRadius(15.0f)
        , m_rectSize(20.0f, 15.0f, 0.0f)
        , m_contourOffset(0.0f)
    {}
    
    ~ScalarField01BasicSketch() = default;

    // Sketch information
    std::string getName() const override {
        return "Scalar Field 01: Basic Construction";
    }

    std::string getDescription() const override {
        return "Educational sketch demonstrating basic circle and rectangle scalar field generation with animations";
    }

    std::string getAuthor() const override {
        return "alice2 Educational Series";
    }

    // Sketch lifecycle
    void setup() override {
        scene().setBackgroundColor(Vec3(0.05f, 0.05f, 0.1f));
        scene().setShowGrid(false);
        scene().setGridSize(25.0f);
        scene().setGridDivisions(4);
        scene().setShowAxes(true);
        scene().setAxesLength(10.0f);
        
        std::cout << "Scalar Field 01: Basic Construction loaded" << std::endl;
        std::cout << "Field dimensions: 100x100 grid, bounds (-50,-50) to (50,50)" << std::endl;
        
        // Initialize with circle field
        generateField();
    }

    void update(float deltaTime) override {
        m_time += deltaTime;
        
        // Animate circle radius using smooth sin function
        m_circleRadius = 15.0f + std::sin(m_time * 0.8f) * 8.0f;
        
        // Animate rectangle dimensions using sin/cos for smooth looping
        m_rectSize.x = 20.0f + std::cos(m_time * 0.6f) * 10.0f;
        m_rectSize.y = 15.0f + std::sin(m_time * 0.4f) * 8.0f;
        
        // Animate contour offset for dynamic contour extraction
        m_contourOffset = std::sin(m_time * 1.2f) * 5.0f;
        
        // Regenerate field based on current mode
        generateField();
    }

    void draw(Renderer& renderer, Camera& camera) override {
        // Draw scalar field visualization
        if (d_drawField) {
            m_scalarField.draw_points(renderer, 2); // Every 2nd point for performance
        }
        
        // Draw scalar values as text
        if (d_drawValues) {
            m_scalarField.draw_values(renderer, 12); // Sparse grid for readability
        }
        
        // Draw animated contours
        if (d_drawContours) {
            drawAnimatedContours(renderer);
        }
        
        // Draw geometric centers with distinct colors and 3D text labels
        drawGeometricCenters(renderer);
        
        // Draw UI and controls
        drawUI(renderer);
    }

    void cleanup() override {
        std::cout << "Scalar Field 01: Basic Construction cleanup" << std::endl;
    }

private:
    void generateField() {
        m_scalarField.clear_field();
        
        if (b_computeCircle) {
            // Generate circular scalar field at center (0, 0)
            m_scalarField.apply_scalar_circle(Vec3(0, 0, 0), m_circleRadius);
        } else if (b_computeRect) {
            // Generate rectangular scalar field at center (0, 0)
            m_scalarField.apply_scalar_rect(Vec3(0, 0, 0), m_rectSize, 0.0f);
        }
    }
    
    void drawAnimatedContours(Renderer& renderer) {
        // Draw single contour line
        renderer.setColor(Vec3(1.0f, 1.0f, 1.0f)); // White contours

        float threshold = 0.0f + m_contourOffset;
        m_scalarField.drawIsocontours(renderer, threshold);
    }
    
    void drawGeometricCenters(Renderer& renderer) {
        Vec3 center(0, 0, 0);
        
        if (b_computeCircle) {
            // Draw circle center in red
            renderer.setColor(Vec3(1.0f, 0.2f, 0.2f));
            renderer.drawPoint(center, Vec3(1.0f, 0.2f, 0.2f), 8.0f);
            
            // Draw 3D text label "CIRCLE"
            renderer.setColor(Vec3(1.0f, 0.8f, 0.8f));
            renderer.drawText("CIRCLE", center + Vec3(0, 0, 5), 1.2f);
            
        } else if (b_computeRect) {
            // Draw rectangle center in blue
            renderer.setColor(Vec3(0.2f, 0.2f, 1.0f));
            renderer.drawPoint(center, Vec3(0.2f, 0.2f, 1.0f), 8.0f);
            
            // Draw 3D text label "RECT"
            renderer.setColor(Vec3(0.8f, 0.8f, 1.0f));
            renderer.drawText("RECT", center + Vec3(0, 0, 5), 1.2f);
        }
    }
    
    void drawUI(Renderer& renderer) {
        // Title and description
        renderer.setColor(Vec3(1.0f, 1.0f, 1.0f));
        renderer.drawString(getName(), 10, 30);
        renderer.drawString("Educational sketch: Basic scalar field construction", 10, 50);
        
        // FPS display
        renderer.setColor(Vec3(0.0f, 1.0f, 1.0f));
        renderer.drawString("FPS: " + std::to_string(Application::getInstance()->getFPS()), 10, 80);
        
        // Current mode display
        renderer.setColor(Vec3(1.0f, 1.0f, 0.0f));
        std::string mode = b_computeCircle ? "CIRCLE" : (b_computeRect ? "RECTANGLE" : "NONE");
        renderer.drawString("Current Mode: " + mode, 10, 110);
        
        // Animation parameters
        renderer.setColor(Vec3(0.8f, 0.8f, 0.8f));
        if (b_computeCircle) {
            renderer.drawString("Circle Radius: " + std::to_string(m_circleRadius).substr(0, 5), 10, 140);
        } else if (b_computeRect) {
            renderer.drawString("Rect Size: " + std::to_string(m_rectSize.x).substr(0, 4) + " x " + 
                              std::to_string(m_rectSize.y).substr(0, 4), 10, 140);
        }
        
        // Controls
        renderer.setColor(Vec3(0.7f, 0.7f, 0.7f));
        renderer.drawString("Controls:", 10, 180);
        renderer.drawString("'G' - Toggle Circle/Rectangle", 10, 200);
        renderer.drawString("'C' - Toggle Contours", 10, 220);
        renderer.drawString("'F' - Toggle Field Visualization", 10, 240);
        renderer.drawString("'V' - Toggle Value Display", 10, 260);
        
        // Status indicators
        renderer.setColor(Vec3(0.5f, 1.0f, 0.5f));
        renderer.drawString("Field: " + std::string(d_drawField ? "ON" : "OFF"), 10, 290);
        renderer.drawString("Contours: " + std::string(d_drawContours ? "ON" : "OFF"), 10, 310);
        renderer.drawString("Values: " + std::string(d_drawValues ? "ON" : "OFF"), 10, 330);
    }

public:
    // Input handling
    bool onKeyPress(unsigned char key, int x, int y) override {
        switch (key) {
            case 'g':
            case 'G': // G key - Toggle between circle and rectangle
                if (b_computeCircle) {
                    b_computeCircle = false;
                    b_computeRect = true;
                } else {
                    b_computeCircle = true;
                    b_computeRect = false;
                }
                generateField();
                return true;
                
            case 'c':
            case 'C': // Toggle contours
                d_drawContours = !d_drawContours;
                return true;
                
            case 'f':
            case 'F': // Toggle field visualization
                d_drawField = !d_drawField;
                return true;
                
            case 'v':
            case 'V': // Toggle value display
                d_drawValues = !d_drawValues;
                return true;
        }
        return false;
    }
};

// Register the sketch with alice2
//ALICE2_REGISTER_SKETCH_AUTO(ScalarField01BasicSketch)

#endif // __MAIN__

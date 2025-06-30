// alice2 Scalar Field Educational Sketch 2: Boolean Operations
// Demonstrates boolean operations between rectangle and corner circles

#define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>
#include <computeGeom/scalarField.h>
#include <cmath>

using namespace alice2;

class ScalarField02BooleanSketch : public ISketch {
private:
    // Scalar fields with consistent dimensions: 100x100 grid, bounds (-50, -50) to (50, 50)
    ScalarField2D m_baseField;      // Base rectangle field
    ScalarField2D m_circleField;    // Individual circle field for operations
    ScalarField2D m_resultField;    // Combined result field
    
    // Animation and timing
    float m_time;
    
    // Boolean flags for computation controls (prefix with "b_")
    bool b_computeBoolean;
    
    // Boolean flags for visualization controls (prefix with "d_")
    bool d_drawField;
    bool d_drawValues;
    bool d_drawContours;
    bool d_previewUnion;
    bool d_previewSubtract;
    
    // Geometric parameters - consistent with specification
    Vec3 m_rectCenter;      // Rectangle center at (-15, -10)
    Vec3 m_circleCenter;    // Circle center at (15, 10)
    Vec3 m_rectSize;        // Rectangle size (40x30 units)
    
    // Corner circle parameters
    struct CornerCircle {
        Vec3 position;
        float radius;
        bool isUnion;  // true for union, false for subtract
    };
    std::vector<CornerCircle> m_cornerCircles;

public:
    ScalarField02BooleanSketch() 
        : m_baseField(Vec3(-50, -50, 0), Vec3(50, 50, 0), 100, 100)
        , m_circleField(Vec3(-50, -50, 0), Vec3(50, 50, 0), 100, 100)
        , m_resultField(Vec3(-50, -50, 0), Vec3(50, 50, 0), 100, 100)
        , m_time(0.0f)
        , b_computeBoolean(false)
        , d_drawField(true)
        , d_drawValues(false)
        , d_drawContours(true)
        , d_previewUnion(false)
        , d_previewSubtract(false)
        , m_rectCenter(0, 0, 0)
        , m_circleCenter(15, 10, 0)
        , m_rectSize(20.0f, 15.0f, 0.0f)  // Half-size for 40x30 total
    {
        // Initialize corner circles
        initializeCornerCircles();
    }
    
    ~ScalarField02BooleanSketch() = default;

    // Sketch information
    std::string getName() const override {
        return "Scalar Field 02: Boolean Operations";
    }

    std::string getDescription() const override {
        return "Educational sketch demonstrating boolean operations between rectangle and corner circles";
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
        
        std::cout << "Scalar Field 02: Boolean Operations loaded" << std::endl;
        std::cout << "Field dimensions: 100x100 grid, bounds (-50,-50) to (50,50)" << std::endl;
        std::cout << "Rectangle center: (0, 0), Circle center: (15, 10)" << std::endl;
        
        // Initialize base rectangle field
        generateBaseField();
    }

    void update(float deltaTime) override {
        m_time += deltaTime;
        
        // Animate corner circle radii using sin/cos functions
        for (size_t i = 0; i < m_cornerCircles.size(); ++i) {
            float phase = i * 1.57f; // 90 degree phase offset between circles
            m_cornerCircles[i].radius = 8.0f + std::sin(m_time * 0.8f + phase) * 4.0f;
        }
        
        // Regenerate fields if boolean computation is enabled
        if (b_computeBoolean) {
            generateBooleanField();
        }
    }

    void draw(Renderer& renderer, Camera& camera) override {
        // Choose which field to display
        ScalarField2D* displayField = &m_baseField;
        if (b_computeBoolean) {
            displayField = &m_resultField;
        }
        
        // Draw scalar field visualization
        if (d_drawField) {
            displayField->draw_points(renderer, 2); // Every 2nd point for performance
        }
        
        // Draw scalar values as text
        if (d_drawValues) {
            displayField->draw_values(renderer, 12); // Sparse grid for readability
        }
        
        // Draw contours
        if (d_drawContours) {
            drawContours(renderer, *displayField);
        }
        
        // Draw geometric centers and corner circles
        drawGeometry(renderer);
        
        // Draw preview operations if enabled
        if (d_previewUnion || d_previewSubtract) {
            drawPreviewOperations(renderer);
        }
        
        // Draw UI and controls
        drawUI(renderer);
    }

    void cleanup() override {
        std::cout << "Scalar Field 02: Boolean Operations cleanup" << std::endl;
    }

private:
    void initializeCornerCircles() {
        m_cornerCircles.clear();
        
        // Four corner circles around the rectangle
        // Top-left and bottom-right: union operations
        // Top-right and bottom-left: subtract operations
        
        Vec3 rectMin = m_rectCenter - m_rectSize;
        Vec3 rectMax = m_rectCenter + m_rectSize;
        
        // Top-left (union)
        m_cornerCircles.push_back({Vec3(rectMin.x, rectMax.y, 0), 12.0f, true});
        
        // Top-right (subtract)
        m_cornerCircles.push_back({Vec3(rectMax.x, rectMax.y, 0), 12.0f, false});
        
        // Bottom-left (subtract)
        m_cornerCircles.push_back({Vec3(rectMin.x, rectMin.y, 0), 12.0f, false});
        
        // Bottom-right (union)
        m_cornerCircles.push_back({Vec3(rectMax.x, rectMin.y, 0), 12.0f, true});
    }
    
    void generateBaseField() {
        // Generate base rectangle field (40x30 units)
        m_baseField.clear_field();
        m_baseField.apply_scalar_rect(m_rectCenter, m_rectSize, 0.0f);
    }
    
    void generateBooleanField() {
        // Start with base rectangle
        m_resultField = m_baseField;

        // Apply boolean operations with each corner circle
        for (const auto& circle : m_cornerCircles) {
            // Generate circle field
            m_circleField.clear_field();
            m_circleField.apply_scalar_circle(circle.position, circle.radius);

            // Apply boolean operation based on radius: small radius = union, large radius = subtract
            bool useUnion = circle.radius < 8.0f; // Threshold at base radius
            if (useUnion) {
                m_resultField.boolean_union(m_circleField);
            } else {
                m_resultField.boolean_subtract(m_circleField);
            }
        }
    }
    
    void drawContours(Renderer& renderer, const ScalarField2D& field) {
        renderer.setColor(Vec3(1.0f, 1.0f, 1.0f)); // White contours

        // Draw single contour line
        float threshold = 0.0f;
        field.drawIsocontours(renderer, threshold);
    }
    
    void drawGeometry(Renderer& renderer) {
        // Draw rectangle center in blue
        renderer.setColor(Vec3(0.2f, 0.2f, 1.0f));
        renderer.drawPoint(m_rectCenter, Vec3(0.2f, 0.2f, 1.0f), 8.0f);
        renderer.drawText("RECT", m_rectCenter + Vec3(0, 0, 5), 1.0f);
        
        // Draw corner circles with different colors based on radius-determined operation
        for (size_t i = 0; i < m_cornerCircles.size(); ++i) {
            const auto& circle = m_cornerCircles[i];

            // Determine operation based on radius: small radius = union, large radius = subtract
            bool useUnion = circle.radius < 8.0f;

            if (useUnion) {
                // Green for union operations (small radius)
                renderer.setColor(Vec3(0.2f, 1.0f, 0.2f));
                renderer.drawPoint(circle.position, Vec3(0.2f, 1.0f, 0.2f), 6.0f);
                renderer.drawText("U", circle.position + Vec3(0, 0, 3), 0.8f);
            } else {
                // Red for subtract operations (large radius)
                renderer.setColor(Vec3(1.0f, 0.2f, 0.2f));
                renderer.drawPoint(circle.position, Vec3(1.0f, 0.2f, 0.2f), 6.0f);
                renderer.drawText("S", circle.position + Vec3(0, 0, 3), 0.8f);
            }
        }
    }
    
    void drawPreviewOperations(Renderer& renderer) {
        // This would show preview of individual operations
        // Implementation depends on specific preview requirements
    }
    
    void drawUI(Renderer& renderer) {
        // Title and description
        renderer.setColor(Vec3(1.0f, 1.0f, 1.0f));
        renderer.drawString(getName(), 10, 30);
        renderer.drawString("Educational sketch: Boolean operations with corner circles", 10, 50);
        
        // FPS display
        renderer.setColor(Vec3(0.0f, 1.0f, 1.0f));
        renderer.drawString("FPS: " + std::to_string(Application::getInstance()->getFPS()), 10, 80);
        
        // Current mode display
        renderer.setColor(Vec3(1.0f, 1.0f, 0.0f));
        std::string mode = b_computeBoolean ? "BOOLEAN ACTIVE" : "BASE RECTANGLE";
        renderer.drawString("Current Mode: " + mode, 10, 110);
        
        // Corner circle info
        renderer.setColor(Vec3(0.8f, 0.8f, 0.8f));
        renderer.drawString("Corner Circles: 4 (2 Union, 2 Subtract)", 10, 140);
        
        // Controls
        renderer.setColor(Vec3(0.7f, 0.7f, 0.7f));
        renderer.drawString("Controls:", 10, 180);
        renderer.drawString("'B' - Toggle Boolean Computation", 10, 200);
        renderer.drawString("'U' - Preview Union Operations", 10, 220);
        renderer.drawString("'S' - Preview Subtract Operations", 10, 240);
        renderer.drawString("'F' - Toggle Field Visualization", 10, 260);
        renderer.drawString("'C' - Toggle Contours", 10, 280);
        renderer.drawString("'V' - Toggle Value Display", 10, 300);
        
        // Status indicators
        renderer.setColor(Vec3(0.5f, 1.0f, 0.5f));
        renderer.drawString("Boolean: " + std::string(b_computeBoolean ? "ON" : "OFF"), 10, 330);
        renderer.drawString("Field: " + std::string(d_drawField ? "ON" : "OFF"), 10, 350);
        renderer.drawString("Contours: " + std::string(d_drawContours ? "ON" : "OFF"), 10, 370);
    }

public:
    // Input handling
    bool onKeyPress(unsigned char key, int x, int y) override {
        switch (key) {
            case 'b':
            case 'B': // Toggle boolean computation
                b_computeBoolean = !b_computeBoolean;
                if (b_computeBoolean) {
                    generateBooleanField();
                }
                return true;
                
            case 'u':
            case 'U': // Preview union operations
                d_previewUnion = !d_previewUnion;
                d_previewSubtract = false; // Exclusive with subtract preview
                return true;
                
            case 's':
            case 'S': // Preview subtract operations
                d_previewSubtract = !d_previewSubtract;
                d_previewUnion = false; // Exclusive with union preview
                return true;
                
            case 'f':
            case 'F': // Toggle field visualization
                d_drawField = !d_drawField;
                return true;
                
            case 'c':
            case 'C': // Toggle contours
                d_drawContours = !d_drawContours;
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
//ALICE2_REGISTER_SKETCH_AUTO(ScalarField02BooleanSketch)

#endif // __MAIN__

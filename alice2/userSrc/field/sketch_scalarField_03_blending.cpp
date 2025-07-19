// alice2 Scalar Field Educational Sketch 3: SDF Blending and Tower Visualization
// Demonstrates smooth minimum blending and multi-level contour extraction with tower visualization

#define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>
#include <computeGeom/scalarField.h>
#include <cmath>

using namespace alice2;

class ScalarField03BlendingSketch : public ISketch {
private:
    // Scalar fields with consistent dimensions: 100x100 grid, bounds (-50, -50) to (50, 50)
    ScalarField2D m_baseField;      // Base rectangle field
    ScalarField2D m_circleField;    // Individual circle field for operations
    ScalarField2D m_field_lower;    // Rectangle with subtract operations (diagonal corners)
    ScalarField2D m_field_upper;    // Rectangle with union operations (other diagonal corners)
    ScalarField2D m_blended_field;  // Result of smooth blending between lower and upper

    // Animation and timing
    float m_time;

    // Boolean flags for computation controls (prefix with "b_")
    bool b_computeBlend;

    // Boolean flags for visualization controls (prefix with "d_")
    bool d_drawField;
    bool d_drawValues;
    bool d_drawContours;
    bool d_drawTower;

    // Geometric parameters - consistent with specification
    Vec3 m_rectCenter;      // Rectangle center at (0, 0)
    Vec3 m_rectSize;        // Rectangle size (40x30 units)

    // Corner circle parameters
    struct CornerCircle {
        Vec3 position;
        float radius;
        bool isUnion;  // true for union, false for subtract
    };
    std::vector<CornerCircle> m_cornerCircles;
    std::vector<CornerCircle> m_middleCircles;

    // Blending parameters
    float m_blendFactor;

    // Blend colors
    Color magenta = Color(1.0f, 0.0f, 1.0f);
    Color purple = Color(0.5f, 0.0f, 1.0f);

    // Tower visualization parameters
    std::vector<float> m_towerLevels;  // Z-levels for tower: 20 floors, 0 to 57
    std::vector<std::vector<std::pair<Vec3, Vec3>>> m_towerContours; // Contours at each level

public:
    ScalarField03BlendingSketch()
        : m_baseField(Vec3(-30, -30, 0), Vec3(30, 30, 0), 100, 100)
        , m_circleField(Vec3(-30, -30, 0), Vec3(30, 30, 0), 100, 100)
        , m_field_lower(Vec3(-30, -30, 0), Vec3(30, 30, 0), 100, 100)
        , m_field_upper(Vec3(-30, -30, 0), Vec3(30, 30, 0), 100, 100)
        , m_blended_field(Vec3(-30, -30, 0), Vec3(30, 30, 0), 100, 100)
        , m_time(0.0f)
        , b_computeBlend(false)
        , d_drawField(true)
        , d_drawValues(false)
        , d_drawContours(true)
        , d_drawTower(false)
        , m_rectCenter(0, 0, 0)
        , m_rectSize(20.0f, 15.0f, 0.0f)
        , m_blendFactor(0.7f)
    {
        // Initialize tower levels: 20 floors with 3-unit spacing (Z=0 to Z=57)
        m_towerLevels.clear();
        for (int i = 0; i < 40; ++i) {
            m_towerLevels.push_back(i * 3.0f);
        }
        m_towerContours.resize(m_towerLevels.size());

        // Initialize corner circles
        initializeCornerCircles();
    }
    
    ~ScalarField03BlendingSketch() = default;

    // Sketch information
    std::string getName() const override {
        return "Scalar Field 03: SDF Blending & Tower";
    }

    std::string getDescription() const override {
        return "Educational sketch demonstrating smooth minimum blending and tower visualization";
    }

    std::string getAuthor() const override {
        return "alice2 Educational Series";
    }

    // Sketch lifecycle
    void setup() override {
        scene().setBackgroundColor(Color(0.95f, 0.95f, 0.95f));
        scene().setShowGrid(false);
        scene().setGridSize(25.0f);
        scene().setGridDivisions(4);
        scene().setShowAxes(true);
        scene().setAxesLength(10.0f);
        
        std::cout << "Scalar Field 03: SDF Blending & Tower loaded" << std::endl;
        std::cout << "Field dimensions: 100x100 grid, bounds (-30,-30) to (30,30)" << std::endl;
        std::cout << "Architecture: Rectangle + 4 corner circles with smin blending" << std::endl;
        
        // Initialize base fields
        generateBaseFields();
    }

    void update(float deltaTime) override {
        m_time += deltaTime;

        // Regenerate blended field if computation is enabled
        if (b_computeBlend) {
                generateTowerContours();

            b_computeBlend = !b_computeBlend;
        }
    }

    void draw(Renderer& renderer, Camera& camera) override {
        if (d_drawTower) {
            // Side-by-side display: original field + tower
            drawSideBySideView(renderer);
        } else {
            // Standard single view
            drawStandardView(renderer);
        }
        
        // Draw geometric centers
        drawGeometry(renderer);
        
        // Draw UI and controls
        drawUI(renderer);
    }

    void cleanup() override {
        std::cout << "Scalar Field 03: SDF Blending & Tower cleanup" << std::endl;
    }

private:
    void initializeCornerCircles() {
        m_cornerCircles.clear();
        m_middleCircles.clear();

        // Four corner circles around the rectangle
        // Top-left and bottom-right: subtract operations (for lower field)
        // Top-right and bottom-left: union operations (for upper field)

        Vec3 rectMin = m_rectCenter - m_rectSize;
        Vec3 rectMax = m_rectCenter + m_rectSize;

        // Top-left (subtract - for lower field)
        m_cornerCircles.push_back({Vec3(rectMin.x, rectMax.y, 0), 8.0f, false});

        // Top-right (union - for upper field)
        m_cornerCircles.push_back({Vec3(rectMax.x, rectMax.y, 0), 8.0f, true});

        // Bottom-left (union - for upper field)
        m_cornerCircles.push_back({Vec3(rectMin.x, rectMin.y, 0), 8.0f, true});

        // Bottom-right (subtract - for lower field)
        m_cornerCircles.push_back({Vec3(rectMax.x, rectMin.y, 0), 8.0f, false});

        // Two middle circles (for upper field)
        m_middleCircles.push_back({Vec3(rectMin.x + m_rectSize.x, rectMin.y, 0), 8.0f, true});
        m_middleCircles.push_back({Vec3(rectMin.x + m_rectSize.x, rectMax.y, 0), 8.0f, true});
        m_middleCircles.push_back({Vec3(rectMin.x, rectMin.y + m_rectSize.y, 0), 8.0f, true});
        m_middleCircles.push_back({Vec3(rectMax.x, rectMin.y + m_rectSize.y, 0), 8.0f, true});
    }

    void generateBaseFields() {
        // Generate base rectangle field
        m_baseField.clear_field();
        m_baseField.apply_scalar_rect(m_rectCenter, m_rectSize, 0.0f);

        // Generate lower field: rectangle with subtract operations on diagonal corners
        m_field_lower = m_baseField;
        for (const auto& circle : m_cornerCircles) {
                float scale = 1.0f;
                m_circleField.clear_field();
                m_circleField.apply_scalar_circle(circle.position, circle.radius * scale);

            // if(circle.isUnion)
            //     m_field_lower.boolean_union(m_circleField);
            // else
                m_field_lower.boolean_subtract(m_circleField);
        }

        // Generate upper field: rectangle with union operations on other diagonal corners
        m_field_upper = m_baseField;
        // for (const auto& circle : m_cornerCircles) {
        //     float scale = 0.8f;
        //     m_circleField.clear_field();
        //     m_circleField.apply_scalar_circle(circle.position, circle.radius * scale);

        //     if(circle.isUnion)
        //         m_field_upper.boolean_union(m_circleField);
        //     else
        //         m_field_upper.boolean_subtract(m_circleField);
        // }
        for (const auto& circle : m_middleCircles) {
            float scale = 0.8f;

            m_circleField.clear_field();
            m_circleField.apply_scalar_circle(circle.position, circle.radius * scale);
            
            m_field_upper.boolean_smin(m_circleField);
            // if(circle.isUnion)
            //     m_field_upper.boolean_union(m_circleField);
            // else
            //     m_field_upper.boolean_subtract(m_circleField);
        }
    }
    
    void generateTowerContours() {
        m_towerContours.clear();

        // Extract contours at each tower level
        for (size_t i = 0; i < m_towerLevels.size(); ++i) {
            // Apply smooth minimum blending with upper field (rectangle with union operations)
            m_blended_field = m_field_lower;
            float wt = static_cast<float>(i) / (m_towerLevels.size() - 1);
            //wt = 0.0f ? 0.01f : wt;
            //m_blended_field.boolean_smin_weighted(m_field_upper, m_blendFactor, wt);
            m_blended_field.interpolate(m_field_upper, wt);
            
            // Get contours
            float threshold = 0.1f; // Adjust threshold for each level
            ContourData contours = m_blended_field.get_contours(threshold);
            m_towerContours[i] = contours.line_segments;
        }
    }
    
    void drawStandardView(Renderer& renderer) {
        // Draw scalar field visualization
        if (d_drawField) {
            m_field_lower.draw_points(renderer, 2);
        }
        
        // Draw scalar values as text
        if (d_drawValues) {
            m_field_lower.draw_values(renderer, 12);
        }
        
        // Draw contours
        if (d_drawContours) {
            // Draw contours for lower field
            renderer.setColor(magenta); // Magenta contours
            drawContours(renderer, m_field_lower);

            // Draw contours for upper field
            renderer.setColor(purple); // Purple contours
            drawContours(renderer, m_field_upper);
        }
    }
    
    void drawSideBySideView(Renderer& renderer) {
        // Left side: original blended field
        drawStandardView(renderer);

        // Right side: tower visualization
        drawTowerVisualization(renderer);
    }
    
    void drawTowerVisualization(Renderer& renderer) {
        // Draw tower contours at different Z levels without matrix transformations
        // Position tower further away to prevent overlap
        Vec3 towerOffset(80, 0, 0);

        for (size_t i = 0; i < m_towerLevels.size(); ++i) {
            float z = m_towerLevels[i];

            // Color gradient from magenta (bottom) to purple (top)
            float t = static_cast<float>(i) / (m_towerLevels.size() - 1);
            // Magenta (1, 0, 1) to Purple (0.5, 0, 1)
            Color color = Color::lerp(magenta, purple, t);
            renderer.setColor(color);

            // Draw contours at this level with tower offset
            for (const auto& segment : m_towerContours[i]) {
                Vec3 start = segment.first + towerOffset + Vec3(0, 0, z);
                Vec3 end = segment.second + towerOffset + Vec3(0, 0, z);
                renderer.drawLine(start, end, color, 2.0f);
            }

            // Draw level indicator every 5 levels to reduce clutter
            if (i % 5 == 0) {
                renderer.drawText("Z=" + std::to_string(static_cast<int>(z)),
                                towerOffset + Vec3(-40, -40, z), 0.8f);
            }
        }
    }
    
    void drawContours(Renderer& renderer, const ScalarField2D& field) {
        // Draw single contour line
        float threshold = 0.0f;
        field.drawIsocontours(renderer, threshold);
    }

    void drawGeometry(Renderer& renderer) {
        // Draw rectangle center in blue
        renderer.setColor(Color(0.2f, 0.2f, 1.0f));
        renderer.drawPoint(m_rectCenter, Color(0.2f, 0.2f, 1.0f), 8.0f);
        renderer.drawText("RECT", m_rectCenter + Vec3(0, 0, 5), 1.0f);

        // Draw corner circles with different colors based on operation
        for (size_t i = 0; i < m_cornerCircles.size(); ++i) {
            const auto& circle = m_cornerCircles[i];

            if (circle.isUnion) {
                // Green for union operations (upper field)
                renderer.setColor(Color(0.2f, 1.0f, 0.2f));
                renderer.drawPoint(circle.position, Color(0.2f, 1.0f, 0.2f), 6.0f);
                renderer.drawText("U", circle.position + Vec3(0, 0, 3), 0.8f);
            } else {
                // Red for subtract operations (lower field)
                renderer.setColor(Color(1.0f, 0.2f, 0.2f));
                renderer.drawPoint(circle.position, Color(1.0f, 0.2f, 0.2f), 6.0f);
                renderer.drawText("S", circle.position + Vec3(0, 0, 3), 0.8f);
            }
        }
    }
    
    void drawUI(Renderer& renderer) {
        // Title and description
        renderer.setColor(Color(1.0f, 1.0f, 1.0f));
        renderer.drawString(getName(), 10, 30);
        renderer.drawString("Educational sketch: SDF blending with tower visualization", 10, 50);
        
        // FPS display
        renderer.setColor(Color(0.0f, 1.0f, 1.0f));
        renderer.drawString("FPS: " + std::to_string(Application::getInstance()->getFPS()), 10, 80);
        
        // Current mode display
        renderer.setColor(Color(1.0f, 1.0f, 0.0f));
        std::string mode = b_computeBlend ? "SMIN BLENDED" : "LOWER FIELD";
        renderer.drawString("Current Mode: " + mode, 10, 110);

        // Architecture info
        renderer.setColor(Color(0.8f, 0.8f, 0.8f));
        renderer.drawString("Lower: Rect + Subtract Corners", 10, 140);
        renderer.drawString("Upper: Rect + Union Corners", 10, 160);
        renderer.drawString("Blend Factor: " + std::to_string(m_blendFactor).substr(0, 4), 10, 180);

        // Tower info
        if (d_drawTower) {
            renderer.drawString("Tower Levels: 20 (Z=0,3,6...57)", 10, 200);
        }
        
        // Controls
        renderer.setColor(Color(0.7f, 0.7f, 0.7f));
        renderer.drawString("Controls:", 10, 200);
        renderer.drawString("'B' - Toggle Blend Computation", 10, 220);
        renderer.drawString("'T' - Toggle Tower Visualization", 10, 240);
        renderer.drawString("'+'/'-' - Adjust Blend Factor", 10, 260);
        renderer.drawString("'F' - Toggle Field Visualization", 10, 280);
        renderer.drawString("'C' - Toggle Contours", 10, 300);
        renderer.drawString("'V' - Toggle Value Display", 10, 320);
        
        // Status indicators
        renderer.setColor(Color(0.5f, 1.0f, 0.5f));
        renderer.drawString("Blend: " + std::string(b_computeBlend ? "ON" : "OFF"), 10, 350);
        renderer.drawString("Tower: " + std::string(d_drawTower ? "ON" : "OFF"), 10, 370);
        renderer.drawString("Field: " + std::string(d_drawField ? "ON" : "OFF"), 10, 390);
    }

public:
    // Input handling
    bool onKeyPress(unsigned char key, int x, int y) override {
        switch (key) {
            case 't':
            case 'T': // Toggle tower visualization
                d_drawTower = !d_drawTower;
                if (d_drawTower) {
                    generateTowerContours();
                }
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
//ALICE2_REGISTER_SKETCH_AUTO(ScalarField03BlendingSketch)

#endif // __MAIN__

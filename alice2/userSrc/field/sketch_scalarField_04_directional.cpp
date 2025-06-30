// alice2 Scalar Field Educational Sketch 4: Directional Boolean Operations with Sun Vector
// Demonstrates dynamic boolean operations based on sun direction and exposure calculation

#define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>
#include <computeGeom/scalarField.h>
#include <cmath>
#include <random>

using namespace alice2;

class ScalarField04DirectionalSketch : public ISketch {
private:
    std::mt19937 m_rng;

    // Scalar fields with consistent dimensions: 100x100 grid, bounds (-50, -50) to (50, 50)
    ScalarField2D m_field_lower;    // Rectangle field (non-rotated)
    ScalarField2D m_field_upper;    // Rotated rectangle field (30 degrees)
    ScalarField2D m_circleField;    // Individual circle field for operations
    ScalarField2D m_resultField;    // Final result field (smin blend of lower and upper)

    // Animation and timing
    float m_time;

    // Boolean flags for computation controls (prefix with "b_")
    bool b_computeDirectional;

    // Boolean flags for visualization controls (prefix with "d_")
    bool d_drawField;
    bool d_drawValues;
    bool d_drawContours;
    bool d_drawTower;
    bool d_animateSun;

    // Geometric parameters - consistent with specification
    Vec3 m_rectCenter;      // Rectangle center at (0, 0, 0)
    Vec3 m_rectSize;        // Rectangle size (40x30 units)

    // Sun direction and exposure
    Vec3 m_sunDirection;    // 2D sun direction vector
    bool m_manualSunControl;

    ContourData m_init_contours_lower;
    ContourData m_init_contours_upper;

    // Blending parameters
    float m_blendFactor;
    float m_collisionFactor;

    // Circle cluster system
    struct Circle {
        Vec3 position;
        Vec3 velocity;
        float radius;
        bool hasCollision;

        Circle(Vec3 pos, float r) : position(pos), velocity(0, 0, 0), radius(r), hasCollision(false) {}
    };

    struct CircleCluster {
        std::vector<Circle> circles;
        Vec3 targetDirection;  // Direction this cluster should move
        Vec3 clusterVelocity;  // Overall cluster movement
        bool isSunFollowing;   // true for sun-following, false for sun-opposing

        CircleCluster(bool sunFollowing) : targetDirection(0, 0, 0), clusterVelocity(0, 0, 0), isSunFollowing(sunFollowing) {}
    };

    CircleCluster m_clusterA_lower;  // Sun-following cluster (subtract operations)
    CircleCluster m_clusterB_lower;  // Sun-opposing cluster (union operations)
    CircleCluster m_clusterA_upper;  // Sun-following cluster (subtract operations)
    CircleCluster m_clusterB_upper;  // Sun-opposing cluster (union operations)

    // Movement parameters
    float m_clusterSpeed;
    float m_collisionDamping;
    
    // Tower visualization parameters
    std::vector<float> m_towerLevels;
    std::vector<std::vector<std::pair<Vec3, Vec3>>> m_towerContours;

public:
    ScalarField04DirectionalSketch()
        : m_rng(std::random_device{}())
        ,m_field_lower(Vec3(-50, -50, 0), Vec3(50, 50, 0), 100, 100)
        , m_field_upper(Vec3(-50, -50, 0), Vec3(50, 50, 0), 100, 100)
        , m_circleField(Vec3(-50, -50, 0), Vec3(50, 50, 0), 100, 100)
        , m_resultField(Vec3(-50, -50, 0), Vec3(50, 50, 0), 100, 100)
        , m_time(0.0f)
        , b_computeDirectional(false)
        , d_drawField(true)
        , d_drawValues(false)
        , d_drawContours(true)
        , d_drawTower(false)
        , d_animateSun(true)
        , m_rectCenter(0, 0, 0)
        , m_rectSize(20.0f, 15.0f, 0.0f)
        , m_sunDirection(1.0f, 0.0f, 0.0f)
        , m_manualSunControl(false)
        , m_blendFactor(2.0f)
        , m_clusterA_lower(true)   // Sun-following cluster
        , m_clusterB_lower(false)  // Sun-opposing cluster
        , m_clusterA_upper(true)   // Sun-following cluster
        , m_clusterB_upper(false)  // Sun-opposing cluster
        , m_clusterSpeed(15.0f)
        , m_collisionDamping(0.8f)
        , m_collisionFactor(0.6f)
    {
        // Initialize tower levels: 20 floors with 3-unit spacing (Z=0 to Z=57)
        m_towerLevels.clear();
        for (int i = 0; i < 40; ++i) {
            m_towerLevels.push_back(i * 3.0f);
        }
        m_towerContours.resize(m_towerLevels.size());

        // Initialize circle clusters
        initializeCircleClusters();

        // Initialize base fields
        generateBaseFields();
    }
    
    ~ScalarField04DirectionalSketch() = default;

    // Sketch information
    std::string getName() const override {
        return "Scalar Field 04: Directional Boolean";
    }

    std::string getDescription() const override {
        return "Educational sketch demonstrating directional boolean operations with sun exposure";
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
        
        std::cout << "Scalar Field 04: Dynamic Circle Clusters loaded" << std::endl;
        std::cout << "Field dimensions: 100x100 grid, bounds (-50,-50) to (50,50)" << std::endl;
        std::cout << "Rectangle center: (0, 0), Size: 40x30 units" << std::endl;
        std::cout << "Cluster A: Sun-following (Union), Cluster B: Sun-opposing (Subtract)" << std::endl;
    }

    void update(float deltaTime) override {
        m_time += deltaTime;

        // Animate sun direction if enabled
        if (d_animateSun && !m_manualSunControl) {
            float angle = m_time * 0.3f;
            m_sunDirection.x = std::cos(angle);
            m_sunDirection.y = std::sin(angle);
            m_sunDirection = m_sunDirection.normalized();
        }

        // Animate blend factor
        m_blendFactor = 2.0f + std::sin(m_time * 0.5f) * 1.5f;

        // Update circle clusters based on sun direction
        updateCircleClusters(deltaTime);

        // Regenerate directional field if computation is enabled
        if (b_computeDirectional) {
            generateDirectionalField();
            if (d_drawTower) {
                generateTowerContours();
            }
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
        
        // Draw geometric elements
        drawGeometry(renderer);
        
        // Draw sun vector
        drawSunVector(renderer);
        
        // Draw UI and controls
        drawUI(renderer);
    }

    void cleanup() override {
        std::cout << "Scalar Field 04: Directional Boolean cleanup" << std::endl;
    }

private:
    void initializeCircleClusters() {
        // Initialize Cluster A (Sun-following, Union operations)

        float radius_A = 3.0f;
        float radius_B = 6.0f;
        m_clusterA_lower.circles.clear();

        std::uniform_real_distribution<float> m_dist(-1.0f, 1.0f);

        for(int i = 0; i < 6; ++i)
        {
            float sigmoid = 1.0/(1.0+std::exp(-i));
            m_clusterA_lower.circles.push_back(Circle(Vec3(
                m_dist(m_rng) * sigmoid,
                m_dist(m_rng) * sigmoid, 0), radius_A * sigmoid));
        }

        // Initialize Cluster B (Sun-opposing, Subtract operations)
        m_clusterB_lower.circles.clear();

        for(int i = 0; i < 7; ++i)
        {
            float sigmoid = 1.0/(1.0+std::exp(-i));
            m_clusterB_lower.circles.push_back(Circle(Vec3(
                m_dist(m_rng) * sigmoid,
                m_dist(m_rng) * sigmoid, 0), radius_B * sigmoid));
        }

        m_clusterA_upper.circles = m_clusterA_lower.circles;
        m_clusterB_upper.circles = m_clusterB_lower.circles;
    }

    void generateBaseFields() {
        // Generate lower field: non-rotated rectangle at center (0, 0, 0)
        m_field_lower.clear_field();
        m_field_lower.apply_scalar_rect(m_rectCenter, m_rectSize, 0.0f);

        // Generate upper field: rotated rectangle (30 degrees) at same center
        m_field_upper.clear_field();
        m_field_upper.apply_scalar_rect(m_rectCenter, m_rectSize, 0.523599f); // 30 degrees in radians

        m_init_contours_lower = m_field_lower.get_contours(0.0f);
        m_init_contours_upper = m_field_upper.get_contours(0.0f);
    }
    
    void updateCircleClusters(float deltaTime) {
        // Update cluster target directions based on sun direction
        m_clusterA_lower.targetDirection = m_sunDirection.normalized();  // Sun-following
        m_clusterB_lower.targetDirection = -m_sunDirection.normalized(); // Sun-opposing
        m_clusterA_upper.targetDirection = m_sunDirection.normalized();  // Sun-following
        m_clusterB_upper.targetDirection = -m_sunDirection.normalized(); // Sun-opposing
        // Update cluster A (Sun-following)

        updateCluster(m_clusterA_lower, m_init_contours_lower, deltaTime);
        updateCluster(m_clusterA_upper, m_init_contours_upper, deltaTime);

        // Update cluster B (Sun-opposing)
        updateCluster(m_clusterB_lower,m_init_contours_lower, deltaTime);
        updateCluster(m_clusterB_upper,m_init_contours_upper, deltaTime);
    }

    void updateCluster(CircleCluster& cluster, ContourData& contours, float deltaTime) {
        // Update cluster velocity towards target direction
        Vec3 targetVelocity = cluster.targetDirection * m_clusterSpeed;
        cluster.clusterVelocity = Vec3::lerp(cluster.clusterVelocity, targetVelocity, deltaTime * 2.0f);

        // Apply cluster movement to all circles
        for (auto& circle : cluster.circles) {
            circle.velocity = cluster.clusterVelocity;
            circle.position += circle.velocity * deltaTime;

            // Constrain circles to rectangle bounds
            constrainToBoundary(circle, contours, m_collisionFactor);
        }

        // Apply collision detection within cluster
        detectAndResolveCollisions(cluster, m_collisionFactor);
    }

    void constrainToBoundary(Circle& circle, const ContourData& contours, float factor = 1.0f) {
    if (contours.line_segments.empty()) return;

    // Track the smallest distance and its corresponding data
    float minDist = std::numeric_limits<float>::max();
    Vec3 closestPoint;
    Vec3 bestNormal;
    bool foundConstraint = false;

    // Find the closest point on any contour segment
    for (const auto& line : contours.line_segments) {
        const Vec3& A = line.first;
        const Vec3& B = line.second;
        Vec3 AB = B - A;

        // Handle degenerate segments
        float segmentLengthSq = AB.dot(AB);
        if (segmentLengthSq < 1e-6f) continue;

        Vec3 AP = circle.position - A;

        // Project AP onto AB, clamped to [0,1] to stay on the segment
        float t = std::clamp(AP.dot(AB) / segmentLengthSq, 0.0f, 1.0f);

        // Closest point on this segment
        Vec3 closest = A + AB * t;
        Vec3 diff = circle.position - closest;
        float dist = diff.length();

        if (dist < minDist) {
            minDist = dist;
            closestPoint = closest;

            // Calculate normal: perpendicular to segment, pointing away from circle
            if (dist > 1e-6f) {
                bestNormal = diff / dist;
            } else {
                // If circle is exactly on the segment, use perpendicular to segment
                Vec3 segmentDir = AB.normalized();
                bestNormal = Vec3(-segmentDir.y, segmentDir.x, 0.0f); // 2D perpendicular
            }
            foundConstraint = true;
        }
    }

    if (!foundConstraint) return;

    // Check if circle is penetrating the boundary
    float rScaled = circle.radius * factor;

    if (minDist < rScaled) {
        float penetration = rScaled - minDist;

        // 1) Push the circle out along the normal
        circle.position += bestNormal * penetration;

        // 2) Reflect velocity component that's going into the boundary
        float vDotN = circle.velocity.dot(bestNormal);
        if (vDotN < 0.0f) { // Only reflect if moving into the boundary
            circle.velocity = circle.velocity - bestNormal * ((1.0f + m_collisionDamping) * vDotN);
        }
    }
}

    void detectAndResolveCollisions(CircleCluster& cluster, float factor = 1.0f) {
        // Reset collision flags
        for (auto& circle : cluster.circles) {
            circle.hasCollision = false;
        }

        // Check collisions between all pairs of circles in the cluster
        for (size_t i = 0; i < cluster.circles.size(); ++i) {
            for (size_t j = i + 1; j < cluster.circles.size(); ++j) {
                Circle& circleA = cluster.circles[i];
                Circle& circleB = cluster.circles[j];

                Vec3 diff = circleB.position - circleA.position;
                float distance = diff.length();
                float minDistance = (circleA.radius + circleB.radius) * factor;

                if (distance < minDistance && distance > 0.001f) {
                    // Collision detected
                    circleA.hasCollision = true;
                    circleB.hasCollision = true;

                    // Separate circles
                    Vec3 separation = diff.normalized() * (minDistance - distance) * 0.5f;
                    circleA.position -= separation;
                    circleB.position += separation;

                    // Apply collision response to velocities
                    Vec3 relativeVelocity = circleB.velocity - circleA.velocity;
                    float velocityAlongNormal = relativeVelocity.dot(diff.normalized());

                    if (velocityAlongNormal > 0) continue; // Objects separating

                    Vec3 impulse = diff.normalized() * velocityAlongNormal * m_collisionDamping;
                    circleA.velocity += impulse;
                    circleB.velocity -= impulse;
                }
            }
        }
    }

    void generateDirectionalField() {
        // First, regenerate base fields
        generateBaseFields();

        // Apply cluster A circles (Sun-following, Union operations) to both fields
        for (const auto& circle : m_clusterA_lower.circles) {
            m_circleField.clear_field();
            m_circleField.apply_scalar_circle(circle.position, circle.radius);
            m_field_lower.boolean_subtract(m_circleField);
        }
        for (const auto& circle : m_clusterA_upper.circles) {
            m_circleField.clear_field();
            m_circleField.apply_scalar_circle(circle.position, circle.radius);
            m_field_upper.boolean_subtract(m_circleField);
        }

        // Apply cluster B circles (Sun-opposing, Subtract operations) to both fields
        for (const auto& circle : m_clusterB_lower.circles) {
            m_circleField.clear_field();
            m_circleField.apply_scalar_circle(circle.position, circle.radius);
            m_field_lower.boolean_union(m_circleField);
        }
        for (const auto& circle : m_clusterB_upper.circles) {
            m_circleField.clear_field();
            m_circleField.apply_scalar_circle(circle.position, circle.radius);
            m_field_upper.boolean_union(m_circleField);
        }

        // Final result: smin blend between processed lower and upper fields
        //m_resultField = m_field_lower;
        // m_resultField.boolean_smin(m_field_upper, m_blendFactor);
    }
    
    void generateTowerContours() {
        m_towerContours.clear();

        // Extract contours at each tower level
        for (size_t i = 0; i < m_towerLevels.size(); ++i) {
            // Apply smooth minimum blending with upper field (rectangle with union operations)
            m_resultField = m_field_lower;
            float wt = static_cast<float>(i) / (m_towerLevels.size() - 1);
            //wt = 0.0f ? 0.01f : wt;
            //m_resultField.boolean_smin_weighted(m_field_upper, 0.2f, wt);
            m_resultField.interpolate(m_field_upper, wt);
            
            // Get contours
            float threshold = 0.1f; // Adjust threshold for each level
            ContourData contours = m_resultField.get_contours(threshold);
            m_towerContours[i] = contours.line_segments;
        }
    }
    
    void drawStandardView(Renderer& renderer) {
        // Choose which field to display
        ScalarField2D* displayField = &m_field_upper;
        if (b_computeDirectional) {
            // displayField = &m_resultField;
        }

        // Draw scalar field visualization
        if (d_drawField) {
            displayField->draw_points(renderer, 2);
        }

        // Draw scalar values as text
        if (d_drawValues) {
            displayField->draw_values(renderer, 12);
        }

        // Draw contours
        if (d_drawContours) {
            drawContours(renderer, *displayField);
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
            Vec3 magenta(1.0f, 0.0f, 1.0f);
            Vec3 purple(0.5f, 0.0f, 1.0f);
            Vec3 color = Vec3::lerp(magenta, purple, t);
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
        renderer.setColor(Vec3(1.0f, 1.0f, 1.0f));

        // Draw single contour line
        float threshold = 0.0f;
        field.drawIsocontours(renderer, threshold);
    }
    
    void drawGeometry(Renderer& renderer) {
        // Draw rectangle center in blue
        renderer.setColor(Vec3(0.2f, 0.2f, 1.0f));
        renderer.drawPoint(m_rectCenter, Vec3(0.2f, 0.2f, 1.0f), 8.0f);
        renderer.drawText("RECT", m_rectCenter + Vec3(0, 0, 5), 1.0f);

        // Draw circle clusters if directional computation is active
        if (b_computeDirectional) {
            // Draw Cluster A (Sun-following, Green for Union)
            for (const auto& circle : m_clusterA_upper.circles) {
                Vec3 color = circle.hasCollision ? Vec3(1.0f, 1.0f, 0.0f) : Vec3(0.2f, 1.0f, 0.2f); // Yellow if collision, Green otherwise
                renderer.setColor(color);
                renderer.drawPoint(circle.position, color, circle.radius * 0.5f);
                renderer.drawText("S", circle.position + Vec3(0, 0, 3), 0.6f);
            }

            // Draw Cluster B (Sun-opposing, Red for Subtract)
            for (const auto& circle : m_clusterB_upper.circles) {
                Vec3 color = circle.hasCollision ? Vec3(1.0f, 1.0f, 0.0f) : Vec3(1.0f, 0.2f, 0.2f); // Yellow if collision, Red otherwise
                renderer.setColor(color);
                renderer.drawPoint(circle.position, color, circle.radius * 0.5f);
                renderer.drawText("U", circle.position + Vec3(0, 0, 3), 0.6f);
            }

            // Draw cluster movement vectors
            renderer.setColor(Vec3(0.8f, 0.8f, 0.8f));
            Vec3 clusterACenter = getClusterCenter(m_clusterA_upper);
            Vec3 clusterBCenter = getClusterCenter(m_clusterB_upper);

            // Cluster A movement vector
            renderer.drawLine(clusterACenter, clusterACenter + m_clusterA_upper.clusterVelocity * 0.1f, Vec3(0.2f, 1.0f, 0.2f), 2.0f);

            // Cluster B movement vector
            renderer.drawLine(clusterBCenter, clusterBCenter + m_clusterB_upper.clusterVelocity * 0.1f, Vec3(1.0f, 0.2f, 0.2f), 2.0f);
        }
    }

    Vec3 getClusterCenter(const CircleCluster& cluster) {
        Vec3 center(0, 0, 0);
        for (const auto& circle : cluster.circles) {
            center += circle.position;
        }
        return center / static_cast<float>(cluster.circles.size());
    }
    
    void drawSunVector(Renderer& renderer) {
        // Draw sun vector as animated arrow
        Vec3 sunStart = Vec3(-m_sunDirection.x * 30, -m_sunDirection.y * 30, 10);
        Vec3 sunEnd = sunStart * 0.8f;
        
        // Sun vector in yellow
        renderer.setColor(Vec3(1.0f, 1.0f, 0.0f));
        renderer.drawLine(sunStart, sunEnd, Vec3(1.0f, 1.0f, 0.0f), 3.0f);
        
        // Arrow head
        Vec3 arrowDir = m_sunDirection * 3.0f;
        Vec3 perpDir = Vec3(-m_sunDirection.y, m_sunDirection.x, 0) * 1.5f;
        renderer.drawLine(sunEnd, sunEnd - arrowDir + perpDir, Vec3(1.0f, 1.0f, 0.0f), 2.0f);
        renderer.drawLine(sunEnd, sunEnd - arrowDir - perpDir, Vec3(1.0f, 1.0f, 0.0f), 2.0f);
        
        // Sun direction text
        renderer.drawText("SUN", sunEnd + Vec3(2, 2, 0), 1.0f);
    }
    
    void drawUI(Renderer& renderer) {
        // Title and description
        renderer.setColor(Vec3(1.0f, 1.0f, 1.0f));
        renderer.drawString(getName(), 10, 30);
        renderer.drawString("Educational sketch: Dynamic circle clusters with sun-driven movement", 10, 50);

        // FPS display
        renderer.setColor(Vec3(0.0f, 1.0f, 1.0f));
        renderer.drawString("FPS: " + std::to_string(Application::getInstance()->getFPS()), 10, 80);

        // Current mode display
        renderer.setColor(Vec3(1.0f, 1.0f, 0.0f));
        std::string mode = b_computeDirectional ? "CLUSTER DYNAMICS ACTIVE" : "BASE RECTANGLES";
        renderer.drawString("Current Mode: " + mode, 10, 110);

        // Sun direction info
        renderer.drawString("Sun Dir: (" + std::to_string(m_sunDirection.x).substr(0, 4) + ", " +
                          std::to_string(m_sunDirection.y).substr(0, 4) + ")", 10, 200);

        // Controls
        renderer.setColor(Vec3(0.7f, 0.7f, 0.7f));
        renderer.drawString("Controls:", 10, 250);
        renderer.drawString("'S' - Toggle Sun Animation", 10, 270);
        renderer.drawString("Arrow Keys - Manual Sun Control", 10, 290);
        renderer.drawString("'D' - Toggle Cluster Dynamics", 10, 310);
        renderer.drawString("'T' - Toggle Tower Visualization", 10, 330);
        renderer.drawString("'F' - Toggle Field Visualization", 10, 350);
        renderer.drawString("'C' - Toggle Contours", 10, 370);

        // Status indicators
        renderer.setColor(Vec3(0.5f, 1.0f, 0.5f));
        renderer.drawString("Clusters: " + std::string(b_computeDirectional ? "ON" : "OFF"), 10, 400);
        renderer.drawString("Sun Anim: " + std::string(d_animateSun ? "ON" : "OFF"), 10, 420);
        renderer.drawString("Tower: " + std::string(d_drawTower ? "ON" : "OFF"), 10, 440);
    }

public:
    // Input handling
    bool onKeyPress(unsigned char key, int x, int y) override {
        switch (key) {
            case 's':
            case 'S': // Toggle sun animation
                d_animateSun = !d_animateSun;
                m_manualSunControl = !d_animateSun;
                return true;
                
            case 'd':
            case 'D': // Toggle directional computation
                b_computeDirectional = !b_computeDirectional;
                if (b_computeDirectional) {
                    generateDirectionalField();
                }
                return true;
                
            case 't':
            case 'T': // Toggle tower visualization
                d_drawTower = !d_drawTower;
                if (d_drawTower && b_computeDirectional) {
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
    
    // Special key handling for arrow keys (manual sun control)
    bool onSpecialKeyPress(int key, int x, int y) {
        if (!m_manualSunControl) return false;
        
        const float step = 0.1f;
        switch (key) {
            case 101: // Up arrow
                m_sunDirection.y += step;
                break;
            case 103: // Down arrow
                m_sunDirection.y -= step;
                break;
            case 100: // Left arrow
                m_sunDirection.x -= step;
                break;
            case 102: // Right arrow
                m_sunDirection.x += step;
                break;
            default:
                return false;
        }
        
        m_sunDirection = m_sunDirection.normalized();
        return true;
    }
};

// Register the sketch with alice2
//ALICE2_REGISTER_SKETCH_AUTO(ScalarField04DirectionalSketch)

#endif // __MAIN__

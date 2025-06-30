// alice2 Scalar Field Educational Sketch 5: Tower Set Visualization
// Demonstrates JSON data loading and 3D bounding box visualization with tower switching interface

#define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>
#include <computeGeom/scalarField.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace alice2;
using json = nlohmann::json;

// Structure to hold building data
struct BuildingData {
    int id;
    float width_m;
    float length_m;
    float height_m;
    float angle_degrees;
    float angle_radians;
    float longitude;
    float latitude;
    float area_sqm;
    int levels;
    std::string view_type;
    std::string orientation_description;
    float final_score;
    float mlp_predicted_score;
    float distance_to_green_m;
    float distance_to_water_m;
    bool valid;

    BuildingData() : id(0), width_m(0), length_m(0), height_m(0),
                    angle_degrees(0), angle_radians(0), longitude(0), latitude(0),
                    area_sqm(0), levels(0), final_score(0), mlp_predicted_score(0),
                    distance_to_green_m(0), distance_to_water_m(0), valid(false) {}
};

class ScalarField05TowerSetSketch : public ISketch {
private:
    // Building data
    std::vector<BuildingData> m_buildings;
    int m_currentBuildingIndex;
    bool m_dataLoaded;
    std::string m_loadStatus;
    
    // Scalar field for optional field visualization (100x100 grid, bounds (-50, -50) to (50, 50))
    ScalarField2D m_scalarField;
    
    // Animation and timing
    float m_time;
    
    // Boolean flags for visualization controls (prefix with "d_")
    bool d_drawBoundingBox;
    bool d_drawField;
    bool d_drawValues;
    bool d_showWireframe;
    bool d_showInfo;
    
    // Rendering properties
    Vec3 m_boxColor;
    Vec3 m_wireframeColor;
    
public:
    ScalarField05TowerSetSketch()
        : m_currentBuildingIndex(0)
        , m_dataLoaded(false)
        , m_loadStatus("Not loaded")
        , m_scalarField(Vec3(-50, -50, 0), Vec3(50, 50, 0), 100, 100)
        , m_time(0.0f)
        , d_drawBoundingBox(true)
        , d_drawField(false)
        , d_drawValues(false)
        , d_showWireframe(true)
        , d_showInfo(true)
        , m_boxColor(0.2f, 0.8f, 1.0f)
        , m_wireframeColor(1.0f, 1.0f, 1.0f)
    {
    }
    
    ~ScalarField05TowerSetSketch() = default;
    
    std::string getName() const override {
        return "Scalar Field 05: Tower Set Visualization";
    }
    
    std::string getDescription() const override {
        return "JSON data loading with 3D bounding box visualization and tower switching";
    }
    
    std::string getAuthor() const override {
        return "alice2 Educational Series";
    }
    
    void setup() override {
        // Set up scene
        scene().setBackgroundColor(Vec3(0.05f, 0.05f, 0.1f));
        scene().setShowGrid(false);  // Disable grid for cleaner visualization
        scene().setShowAxes(true);
        
        // Load building data from JSON
        loadBuildingData();
        
        // Generate initial scalar field if needed
        if (d_drawField) {
            generateScalarField();
        }
    }
    
    void update(float deltaTime) override {
        m_time += deltaTime;
        
        // Update scalar field if needed
        if (d_drawField) {
            // Optional: animate scalar field based on current building
            // generateScalarField();
        }
    }
    
    void draw(Renderer& renderer, Camera& camera) override {
        // Draw scalar field visualization if enabled
        if (d_drawField && m_dataLoaded) {
            drawScalarFieldVisualization(renderer);
        }
        
        // Draw current building's bounding box
        if (d_drawBoundingBox && m_dataLoaded && !m_buildings.empty()) {
            drawCurrentBuildingBoundingBox(renderer);
        }
        
        // Draw UI
        drawUI(renderer);
    }
    
    void cleanup() override {
        // Clean up resources
        m_buildings.clear();
    }

private:
    void loadBuildingData() {
        try {
            std::ifstream file(
                "C://Users//taizhong_chen//source//repos//GitZHCODE//zspace_alice2//alice2//data//mlp_building (7).json");
            if (!file.is_open()) {
                m_loadStatus = "Failed to open JSON file";
                std::cerr << "Failed to open alice2/data/mlp_building (7).json" << std::endl;
                return;
            }
            
            json j;
            file >> j;
            
            if (!j.contains("buildings") || !j["buildings"].is_array()) {
                m_loadStatus = "Invalid JSON format - no buildings array";
                std::cerr << "Invalid JSON format - no buildings array" << std::endl;
                return;
            }
            
            m_buildings.clear();
            
            for (const auto& building : j["buildings"]) {
                BuildingData data;

                // Extract required fields with error checking
                if (building.contains("Building_ID") && building["Building_ID"].is_number()) {
                    data.id = building["Building_ID"];
                } else {
                    continue; // Skip invalid buildings
                }

                // Extract dimensions (direct fields, not nested)
                if (building.contains("Width_m") && building["Width_m"].is_number()) {
                    data.width_m = building["Width_m"];
                }
                if (building.contains("Length_m") && building["Length_m"].is_number()) {
                    data.length_m = building["Length_m"];
                }
                if (building.contains("Height_m") && building["Height_m"].is_number()) {
                    data.height_m = building["Height_m"];
                }

                // Extract orientation
                if (building.contains("Orientation_Angle_deg") && building["Orientation_Angle_deg"].is_number()) {
                    data.angle_degrees = building["Orientation_Angle_deg"];
                    data.angle_radians = data.angle_degrees * M_PI / 180.0f; // Convert to radians
                }

                // Extract coordinates
                if (building.contains("Longitude") && building["Longitude"].is_number()) {
                    data.longitude = building["Longitude"];
                }
                if (building.contains("Latitude") && building["Latitude"].is_number()) {
                    data.latitude = building["Latitude"];
                }

                // Extract area and levels
                if (building.contains("Area_sqm") && building["Area_sqm"].is_number()) {
                    data.area_sqm = building["Area_sqm"];
                }
                if (building.contains("Levels") && building["Levels"].is_number()) {
                    data.levels = building["Levels"];
                }

                // Extract optional fields (direct fields, not nested)
                if (building.contains("View_Type") && building["View_Type"].is_string()) {
                    data.view_type = building["View_Type"];
                }
                if (building.contains("Orientation_Description") && building["Orientation_Description"].is_string()) {
                    data.orientation_description = building["Orientation_Description"];
                }
                if (building.contains("Final_Score") && building["Final_Score"].is_number()) {
                    data.final_score = building["Final_Score"];
                }
                if (building.contains("MLP_Predicted_Score") && building["MLP_Predicted_Score"].is_number()) {
                    data.mlp_predicted_score = building["MLP_Predicted_Score"];
                }
                if (building.contains("Distance_to_Green_m") && building["Distance_to_Green_m"].is_number()) {
                    data.distance_to_green_m = building["Distance_to_Green_m"];
                }
                if (building.contains("Distance_to_Water_m") && building["Distance_to_Water_m"].is_number()) {
                    data.distance_to_water_m = building["Distance_to_Water_m"];
                }
                
                data.valid = true;
                m_buildings.push_back(data);
            }
            
            if (!m_buildings.empty()) {
                m_dataLoaded = true;
                m_loadStatus = "Loaded " + std::to_string(m_buildings.size()) + " buildings";
                std::cout << "Successfully loaded " << m_buildings.size() << " buildings from JSON" << std::endl;
            } else {
                m_loadStatus = "No valid buildings found";
            }
            
        } catch (const std::exception& e) {
            m_loadStatus = "JSON parsing error: " + std::string(e.what());
            std::cerr << "Error loading building data: " << e.what() << std::endl;
        }
    }
    
    void generateScalarField() {
        // Generate a simple scalar field based on current building (optional feature)
        if (!m_dataLoaded || m_buildings.empty()) return;
        
        const BuildingData& building = m_buildings[m_currentBuildingIndex];
        
        // Clear and regenerate field
        m_scalarField.clear_field();
        
        // Add a rectangle representing the building footprint
        Vec3 center(0, 0, 0);  // Position at origin for individual display
        Vec3 halfSize(building.width_m * 0.5f, building.length_m * 0.5f, 0);

        m_scalarField.apply_scalar_rect(center, halfSize, building.angle_radians);
        m_scalarField.normalise();
    }
    
    void drawScalarFieldVisualization(Renderer& renderer) {
        // Draw scalar field points
        if (d_drawField) {
            m_scalarField.draw_points(renderer, 2);
        }
        
        // Draw scalar values as text
        if (d_drawValues) {
            m_scalarField.draw_values(renderer, 12);
        }
    }
    
    void drawCurrentBuildingBoundingBox(Renderer& renderer, float t = 1.0f) {
        if (m_buildings.empty()) return;

        const BuildingData& building = m_buildings[m_currentBuildingIndex];

        // Set up transformation matrix for the bounding box
        renderer.pushMatrix();

        // Position at origin with height offset to center the box properly
        // The cube is drawn from -0.5 to +0.5, so we need to translate by half height
        Mat4 translation = Mat4::translation(Vec3(0, 0, building.height_m * 0.5f));

        // Apply rotation around Z-axis (convert degrees to radians if needed)
        Mat4 rotation = Mat4::rotation(Vec3(0, 0, 1), building.angle_radians);

        // Apply scale to match building dimensions
        Mat4 scale = Mat4::scale(Vec3(building.width_m * t, building.length_m * t, building.height_m * t));

        // Apply transformations in correct order: translate, then rotate, then scale
        Mat4 transform = scale * rotation * translation;
        renderer.multMatrix(transform);

        // Draw wireframe bounding box
        if (d_showWireframe) {
            renderer.setWireframe(true);
            renderer.setColor(m_wireframeColor);
            renderer.drawCube(1.0f);
            renderer.setWireframe(false);
        }

        // Draw solid bounding box with transparency
        renderer.setColor(m_boxColor, 0.3f);
        renderer.drawCube(1.0f);

        renderer.popMatrix();
    }

    void drawUI(Renderer& renderer) {
        // Title and description
        renderer.setColor(Vec3(1.0f, 1.0f, 1.0f));
        renderer.drawString(getName(), 10, 30);
        renderer.drawString("Educational sketch: JSON data loading with 3D bounding box visualization", 10, 50);

        // FPS display
        renderer.setColor(Vec3(0.0f, 1.0f, 1.0f));
        renderer.drawString("FPS: " + std::to_string(Application::getInstance()->getFPS()), 10, 80);

        // Data loading status
        renderer.setColor(Vec3(1.0f, 1.0f, 0.0f));
        renderer.drawString("Data Status: " + m_loadStatus, 10, 110);

        // Current building information
        if (m_dataLoaded && !m_buildings.empty()) {
            const BuildingData& building = m_buildings[m_currentBuildingIndex];

            renderer.setColor(Vec3(0.8f, 1.0f, 0.8f));
            renderer.drawString("Current Tower: " + std::to_string(m_currentBuildingIndex + 1) + " / " + std::to_string(m_buildings.size()), 10, 140);
            renderer.drawString("Tower ID: " + std::to_string(building.id), 10, 160);

            // Dimensions
            renderer.setColor(Vec3(0.9f, 0.9f, 0.9f));
            char dimStr[256];
            snprintf(dimStr, sizeof(dimStr), "Dimensions: %.1f × %.1f × %.1f meters",
                    building.width_m, building.length_m, building.height_m);
            renderer.drawString(std::string(dimStr), 10, 180);

            // Area and levels
            char areaStr[256];
            snprintf(areaStr, sizeof(areaStr), "Area: %.1f sqm, Levels: %d",
                    building.area_sqm, building.levels);
            renderer.drawString(std::string(areaStr), 10, 200);

            // Orientation
            char orientStr[256];
            snprintf(orientStr, sizeof(orientStr), "Orientation: %.1f° (%s)",
                    building.angle_degrees, building.orientation_description.c_str());
            renderer.drawString(std::string(orientStr), 10, 220);

            // Additional info
            if (!building.view_type.empty()) {
                renderer.drawString("View Type: " + building.view_type, 10, 240);
            }

            // Scores
            char scoreStr[256];
            snprintf(scoreStr, sizeof(scoreStr), "Final Score: %.1f, MLP Score: %.1f",
                    building.final_score, building.mlp_predicted_score);
            renderer.drawString(std::string(scoreStr), 10, 260);

            // Distances
            char distStr[256];
            snprintf(distStr, sizeof(distStr), "Green: %.1fm, Water: %.1fm",
                    building.distance_to_green_m, building.distance_to_water_m);
            renderer.drawString(std::string(distStr), 10, 280);
        }

        // Visualization controls status
        renderer.setColor(Vec3(0.7f, 0.7f, 1.0f));
        std::string boxStatus = d_drawBoundingBox ? "ON" : "OFF";
        std::string wireStatus = d_showWireframe ? "ON" : "OFF";
        std::string fieldStatus = d_drawField ? "ON" : "OFF";

        renderer.drawString("Bounding Box: " + boxStatus, 10, 310);
        renderer.drawString("Wireframe: " + wireStatus, 10, 330);
        renderer.drawString("Scalar Field: " + fieldStatus, 10, 350);

        // Controls help
        renderer.setColor(Vec3(0.75f, 0.75f, 0.75f));
        renderer.drawString("Controls:", 10, 380);
        renderer.drawString("'1'     - Previous tower", 10, 400);
        renderer.drawString("'2'     - Next tower", 10, 420);
        renderer.drawString("'B'     - Toggle bounding box", 10, 440);
        renderer.drawString("'W'     - Toggle wireframe", 10, 460);
        renderer.drawString("'F'     - Toggle scalar field", 10, 480);
        renderer.drawString("'V'     - Toggle field values", 10, 500);
        renderer.drawString("'I'     - Toggle info display", 10, 520);
        renderer.drawString("'ESC'   - Exit", 10, 540);
    }

public:
    // Input handling
    bool onKeyPress(unsigned char key, int x, int y) override {
        switch (key) {
            case '1': // Previous tower
                if (m_dataLoaded && !m_buildings.empty()) {
                    m_currentBuildingIndex = (m_currentBuildingIndex - 1 + static_cast<int>(m_buildings.size())) % static_cast<int>(m_buildings.size());
                    if (d_drawField) {
                        generateScalarField();
                    }
                    std::cout << "Previous tower: " << (m_currentBuildingIndex + 1) << " (ID: " << m_buildings[m_currentBuildingIndex].id << ")" << std::endl;
                }
                return true;

            case '2': // Next tower
                if (m_dataLoaded && !m_buildings.empty()) {
                    m_currentBuildingIndex = (m_currentBuildingIndex + 1) % static_cast<int>(m_buildings.size());
                    if (d_drawField) {
                        generateScalarField();
                    }
                    std::cout << "Next tower: " << (m_currentBuildingIndex + 1) << " (ID: " << m_buildings[m_currentBuildingIndex].id << ")" << std::endl;
                }
                return true;

            case 'b':
            case 'B': // Toggle bounding box
                d_drawBoundingBox = !d_drawBoundingBox;
                return true;

            case 'w':
            case 'W': // Toggle wireframe
                d_showWireframe = !d_showWireframe;
                return true;

            case 'f':
            case 'F': // Toggle scalar field
                d_drawField = !d_drawField;
                if (d_drawField) {
                    generateScalarField();
                }
                return true;

            case 'v':
            case 'V': // Toggle field values
                d_drawValues = !d_drawValues;
                return true;

            case 'i':
            case 'I': // Toggle info display
                d_showInfo = !d_showInfo;
                return true;
        }
        return false;
    }
};

// Register the sketch with alice2
//ALICE2_REGISTER_SKETCH_AUTO(ScalarField05TowerSetSketch)

#endif // __MAIN__

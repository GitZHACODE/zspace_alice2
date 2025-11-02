// alice2 Eigen & JSON Integration Demo
// Simple demo: Export Eigen matrix to JSON, then read it back

// #define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>

// Include Eigen library for mathematical operations
#include <Eigen/Dense>

// Include nlohmann/json for JSON parsing
#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>

using namespace alice2;
using json = nlohmann::json;

class EigenJsonDemo : public ISketch {
private:
    // Simple Eigen matrix for demo
    Eigen::Matrix3f testMatrix;
    Eigen::Matrix3f loadedMatrix;

    // Status flags
    bool matrixSaved;
    bool matrixLoaded;
    std::string statusMessage;

public:
    EigenJsonDemo() : matrixSaved(false), matrixLoaded(false), statusMessage("Ready") {}
    ~EigenJsonDemo() = default;

    std::string getName() const override {
        return "Eigen & JSON Integration Demo";
    }

    std::string getDescription() const override {
        return "Simple demo: Export Eigen matrix to JSON, then read it back";
    }

    std::string getAuthor() const override {
        return "alice2 Integration Team";
    }

    void setup() override {
        // Initialize a simple 3x3 matrix with some values
        testMatrix << 1.0f, 2.0f, 3.0f,
                      4.0f, 5.0f, 6.0f,
                      7.0f, 8.0f, 9.0f;

        // Set up scene
        scene().setBackgroundColor(Color(0.1f, 0.1f, 0.15f));
        scene().setShowGrid(true);
        scene().setShowAxes(true);
    }

    void update(float deltaTime) override {
        // Nothing to update in this simple demo
    }

    void draw(Renderer& renderer, Camera& camera) override {
        // Draw a simple visualization showing matrix values
        renderer.drawPoint(Vec3(0, 0, 0), Color(1.0f, 0.0f, 0.0f), 10.0f);

        // 2D UI text
        renderer.setColor(Color(1.0f, 1.0f, 1.0f));
        renderer.drawString(getName(), 10, 30);

        // Display status
        renderer.setColor(Color(0.8f, 0.8f, 1.0f));
        renderer.drawString("Status: " + statusMessage, 10, 50);

        if (matrixSaved) {
            renderer.setColor(Color(0.0f, 1.0f, 0.0f));
            renderer.drawString("Matrix saved to JSON: SUCCESS", 10, 70);
        }

        if (matrixLoaded) {
            renderer.setColor(Color(0.0f, 1.0f, 0.0f));
            renderer.drawString("Matrix loaded from JSON: SUCCESS", 10, 90);
        }

        // Display original matrix values
        renderer.setColor(Color(1.0f, 1.0f, 0.0f));
        renderer.drawString("Original Matrix:", 10, 120);
        for (int i = 0; i < 3; ++i) {
            char row[100];
            sprintf_s(row, "[%.1f, %.1f, %.1f]",
                     testMatrix(i, 0), testMatrix(i, 1), testMatrix(i, 2));
            renderer.drawString(row, 10, 140 + i * 20);
        }

        // Display loaded matrix values
        if (matrixLoaded) {
            renderer.setColor(Color(0.0f, 1.0f, 1.0f));
            renderer.drawString("Loaded Matrix:", 10, 220);
            for (int i = 0; i < 3; ++i) {
                char row[100];
                sprintf_s(row, "[%.1f, %.1f, %.1f]",
                         loadedMatrix(i, 0), loadedMatrix(i, 1), loadedMatrix(i, 2));
                renderer.drawString(row, 10, 240 + i * 20);
            }
        }

        // FPS and controls
        renderer.setColor(Color(0.0f, 1.0f, 1.0f));
        renderer.drawString("FPS: " + std::to_string(Application::getInstance()->getFPS()), 10, 320);

        renderer.setColor(Color(0.75f, 0.75f, 0.75f));
        renderer.drawString("'ESC' - Exit", 10, 350);
        renderer.drawString("'J'   - Export json", 10, 370);
        renderer.drawString("'L'   - Load json", 10, 390);

        // 3D text showing library integration
        renderer.setColor(Color(1.0f, 0.5f, 0.0f));
        renderer.drawText("Eigen + JSON", Vec3(0, 0, 3.0f), 1.0f);
    }

    void cleanup() override {
        // Nothing to clean up in this simple demo
    }

        // Input handling (optional)
    bool onKeyPress(unsigned char key, int x, int y) override {
        // Handle keyboard input
        switch (key) {
            case 'j':
            case 'J':
                saveMatrixToJson();
                return true;
            case 'l':
            case 'L':
                loadMatrixFromJson();
                return true;
        }
        return false; // Not handled
    }

private:
    void saveMatrixToJson() {
        try {
            json j;

            // Convert Eigen matrix to JSON array
            j["matrix"] = json::array();
            for (int i = 0; i < 3; ++i) {
                json row = json::array();
                for (int j_col = 0; j_col < 3; ++j_col) {
                    row.push_back(testMatrix(i, j_col));
                }
                j["matrix"].push_back(row);
            }

            // Save to file
            std::ofstream file("config/eigen_matrix.json");
            if (file.is_open()) {
                file << j.dump(4); // Pretty print with 4 spaces
                matrixSaved = true;
                statusMessage = "Matrix saved successfully";
                std::cout << "Matrix saved to config/eigen_matrix.json" << std::endl;
            } else {
                statusMessage = "Failed to save matrix";
                std::cout << "Could not open file for writing" << std::endl;
            }
        } catch (const std::exception& e) {
            statusMessage = "Error saving matrix: " + std::string(e.what());
            std::cout << "Error saving matrix: " << e.what() << std::endl;
        }
    }

    void loadMatrixFromJson() {
        try {
            std::ifstream file("config/eigen_matrix.json");
            if (file.is_open()) {
                json j;
                file >> j;

                // Convert JSON array back to Eigen matrix
                if (j.contains("matrix") && j["matrix"].is_array()) {
                    for (int i = 0; i < 3 && i < j["matrix"].size(); ++i) {
                        if (j["matrix"][i].is_array()) {
                            for (int j_col = 0; j_col < 3 && j_col < j["matrix"][i].size(); ++j_col) {
                                loadedMatrix(i, j_col) = j["matrix"][i][j_col];
                            }
                        }
                    }
                    matrixLoaded = true;
                    statusMessage = "Matrix loaded successfully";
                    std::cout << "Matrix loaded from config/eigen_matrix.json" << std::endl;
                } else {
                    statusMessage = "Invalid JSON format";
                    std::cout << "Invalid JSON format" << std::endl;
                }
            } else {
                statusMessage = "Could not open matrix file";
                std::cout << "Could not open config/eigen_matrix.json" << std::endl;
            }
        } catch (const std::exception& e) {
            statusMessage = "Error loading matrix: " + std::string(e.what());
            std::cout << "Error loading matrix: " << e.what() << std::endl;
        }
    }

};

// Register the sketch
//ALICE2_REGISTER_SKETCH_AUTO(EigenJsonDemo)

#endif // __MAIN__

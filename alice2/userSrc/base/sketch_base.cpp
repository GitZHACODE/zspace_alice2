// alice2 Base Sketch Template
// This is a template for creating user sketches in alice2

#define __MAIN__
#ifdef __MAIN__


#include <alice2.h>
#include <sketches/SketchRegistry.h>

using namespace alice2;

class BaseSketch : public ISketch {
public:
    BaseSketch() = default;
    ~BaseSketch() = default;

    // Sketch information
    std::string getName() const override {
        return "Base Sketch";
    }

    std::string getDescription() const override {
        return "A basic template sketch for alice2";
    }

    std::string getAuthor() const override {
        return "alice2 User";
    }

    // Sketch lifecycle
    void setup() override {
        // Initialize your sketch here
        // This is called once when the sketch is loaded
        
        // Example: Set background color
        scene().setBackgroundColor(Vec3(0.15f, 0.15f, 0.15f));
        std::cout << "Background color set to light gray" << std::endl;

        // Note: alice2 uses Z-up coordinate system by default (zspace compatibility)

        // Example: Enable grid
        scene().setShowGrid(true);
        scene().setGridSize(10.0f);
        scene().setGridDivisions(10);

        // Example: Enable axes
        scene().setShowAxes(true);
        scene().setAxesLength(2.0f);

    }

    void update(float deltaTime) override {
        // Update your sketch logic here
        // This is called every frame
        // deltaTime is the time elapsed since the last frame in seconds
        
        // Example: Rotate objects, update animations, etc.
    }

    void draw(Renderer& renderer, Camera& camera) override {
        // Draw your custom content here
        // This is called every frame after update()

        // Example: Draw a simple cube using enhanced renderer method
        // renderer.pushMatrix();
        // renderer.drawCube(1.0f, Vec3(1.0f, 0.5f, 0.2f)); // Orange cube
        // renderer.popMatrix();

        // Example: Draw some points using enhanced renderer methods
        for (int i = 0; i < 10; i++) {
            Vec3 pos(0, 0, i);
            Vec3 color(0.2f, 1.0f, 0.5f); // Green color
            renderer.drawPoint(pos, color, 5.0f); // Position, color, size
        }

        // Example: Draw some lines using enhanced renderer methods
        for (int i = 0; i < 5; i++) {
            Vec3 start(i, 0, 0);
            Vec3 end(i, 2, 0);
            Vec3 color(1.0f, 0.2f, 0.8f); // Pink color
            renderer.drawLine(start, end, color, 2.0f); // Start, end, color, width
        }

        // 2D text rendering (screen overlay)
        renderer.setColor(Vec3(1.0f, 1.0f, 1.0f));
        renderer.drawString(getName(), 10, 30);
        renderer.drawString(getDescription(), 10, 50);

        renderer.setColor(Vec3(0.0f, 1.0f, 1.0f));
        renderer.drawString("FPS: " + std::to_string((Application::getInstance()->getFPS())), 10, 70);

        renderer.setColor(Vec3(0.75f, 0.75f, 0.75f));
        renderer.drawString("'ESC' - Exit ", 10, 200);
        renderer.drawString("'F'   - Extend view ", 10, 220);
        renderer.drawString("'N'   - Switch to the next sketch ", 10, 240);
        renderer.drawString("'P'   - Switch to the previous sketch ", 10, 260);

        // Test 3D text rendering (billboard text in world space with screen-space sizing)
        renderer.setColor(Vec3(1.0f, 0.0f, 0.5f)); // Yellow text
        renderer.drawText("Hello from alice2 !", Vec3(0, 0, 2.0f), 1.2f);
    }

    void cleanup() override {
        // Clean up resources here
        // This is called when the sketch is unloaded
    }

    // Input handling (optional)
    bool onKeyPress(unsigned char key, int x, int y) override {
        // Handle keyboard input
        switch (key) {
            case 27: // ESC key
                // Example: Exit application
                return false; // Not handled - allow default exit
        }
        return false; // Not handled
    }

    bool onMousePress(int button, int state, int x, int y) override {
        // Handle mouse button input
        // button: 0=left, 1=middle, 2=right
        // state: 0=down, 1=up
        return false; // Not handled - allow default camera controls
    }

    bool onMouseMove(int x, int y) override {
        // Handle mouse movement
        return false; // Not handled - allow default camera controls
    }
};

// Register the sketch with alice2 (both old and new systems)
ALICE2_REGISTER_SKETCH_AUTO(BaseSketch)

#endif // __MAIN__


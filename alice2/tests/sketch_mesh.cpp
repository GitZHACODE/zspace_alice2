// alice2 Mesh Rendering Sketch
// Demonstrates basic mesh rendering with MeshObject class

// #define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>
#include <objects/MeshObject.h>
#include <memory>

using namespace alice2;

class MeshSketch : public ISketch {
private:
    std::shared_ptr<MeshObject> m_cube;
    std::shared_ptr<MeshObject> m_plane;
    float m_time;
    bool m_normalShadedMode;
    bool m_showVertices;
    bool m_showEdges;

public:
    MeshSketch() : m_time(0.0f), m_normalShadedMode(false), m_showVertices(false), m_showEdges(false) {}
    ~MeshSketch() = default;

    // Sketch information
    std::string getName() const override {
        return "Mesh Sketch";
    }

    std::string getDescription() const override {
        return "Basic mesh rendering demonstration";
    }

    std::string getAuthor() const override {
        return "alice2";
    }

    // Sketch lifecycle
    void setup() override {
        // Set background color
        scene().setBackgroundColor(Color(0.15f, 0.15f, 0.15f));

        // Enable grid and axes
        scene().setShowGrid(true);
        scene().setGridSize(10.0f);
        scene().setGridDivisions(10);
        scene().setShowAxes(true);
        scene().setAxesLength(2.0f);

        // Create mesh objects
        m_cube = std::make_shared<MeshObject>("TestCube");
        m_cube->createCube(2.0f);
        m_cube->setRenderMode(MeshRenderMode::NormalShaded);
        m_cube->setNormalShadingColors(Color(1.0f, 1.0f, 1.0f), Color(0.8f, 0.2f, 0.8f)); // White to magenta
        m_cube->getTransform().setTranslation(Vec3(-3.0f, 0.0f, 1.0f));
        scene().addObject(m_cube);

        m_plane = std::make_shared<MeshObject>("TestPlane");
        m_plane->createPlane(3.0f, 3.0f, 2, 2);
        m_plane->setRenderMode(MeshRenderMode::NormalShaded);
        m_plane->setNormalShadingColors(Color(1.0f, 1.0f, 1.0f), Color(0.8f, 0.2f, 0.8f)); // White to magenta
        m_plane->getTransform().setTranslation(Vec3(3.0f, 0.0f, 0.0f));
        scene().addObject(m_plane);
    }

    void update(float deltaTime) override {
        m_time += deltaTime;

        // Rotate the cube
        // if (m_cube) {
        //     m_cube->getTransform().setRotation(Quaternion::fromAxisAngle(Vec3(0, 0, 1), m_time * 0.5f));
        // }

        // // Rotate the plane around Y axis
        // if (m_plane) {
        //     m_plane->getTransform().setRotation(Quaternion::fromAxisAngle(Vec3(0, 1, 0), m_time * 0.3f));
        // }
    }

    void draw(Renderer& renderer, Camera& camera) override {
        // Draw 2D text overlay
        renderer.setColor(Color(1.0f, 1.0f, 1.0f));
        renderer.drawString(getName(), 10, 30);
        renderer.drawString(getDescription(), 10, 50);

        renderer.setColor(Color(0.0f, 1.0f, 1.0f));
        renderer.drawString("FPS: " + std::to_string(Application::getInstance()->getFPS()), 10, 70);

        renderer.setColor(Color(0.75f, 0.75f, 0.75f));
        renderer.drawString("'M' - Toggle render mode (Lit/NormalShaded)", 10, 200);
        renderer.drawString("'V' - Toggle vertex overlay", 10, 220);
        renderer.drawString("'E' - Toggle edge overlay", 10, 240);
        renderer.drawString("'ESC' - Exit", 10, 260);
        renderer.drawString("'N' - Next sketch", 10, 280);

        // Display current state
        std::string mode = m_normalShadedMode ? "NormalShaded" : "Lit";
        renderer.setColor(Color(1.0f, 1.0f, 0.0f));
        renderer.drawString("Render Mode: " + mode, 10, 100);

        renderer.setColor(Color(0.8f, 1.0f, 0.8f));
        renderer.drawString("Vertices: " + std::string(m_showVertices ? "ON" : "OFF"), 10, 120);
        renderer.drawString("Edges: " + std::string(m_showEdges ? "ON" : "OFF"), 10, 140);
    }

    // Input handling
    bool onKeyPress(unsigned char key, int x, int y) override {
        switch (key) {
            case 'm':
                m_normalShadedMode = !m_normalShadedMode;
                if (m_cube) {
                    m_cube->setRenderMode(m_normalShadedMode ? MeshRenderMode::Lit : MeshRenderMode::NormalShaded);
                }
                if (m_plane) {
                    m_plane->setRenderMode(m_normalShadedMode ? MeshRenderMode::Lit : MeshRenderMode::NormalShaded);
                }
                return true;

            case 'v':
                m_showVertices = !m_showVertices;
                if (m_cube) {
                    m_cube->setShowVertices(m_showVertices);
                }
                if (m_plane) {
                    m_plane->setShowVertices(m_showVertices);
                }
                return true;

            case 'e':
                m_showEdges = !m_showEdges;
                if (m_cube) {
                    m_cube->setShowEdges(m_showEdges);
                }
                if (m_plane) {
                    m_plane->setShowEdges(m_showEdges);
                }
                return true;
        }
        return false;
    }

    bool onMousePress(int button, int state, int x, int y) override {
        return false;
    }

    bool onMouseMove(int x, int y) override {
        return false;
    }

    void cleanup() override {
        scene().removeObject(m_cube);
        scene().removeObject(m_plane);
        std::cout << "Mesh Sketch cleanup" << std::endl;
    }
};

// Register the sketch with alice2
// ALICE2_REGISTER_SKETCH_AUTO(MeshSketch)

#endif // __MAIN__

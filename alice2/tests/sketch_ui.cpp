 // alice2 Empty Sketch Template
// Minimal template for creating a new user sketch in alice2

//#define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>

using namespace alice2;

class UISketch : public ISketch {
public:
    UISketch() = default;
    ~UISketch() = default;

    // Sketch information
    std::string getName() const override { return "UI Sketch"; }

    std::string getDescription() const override { return "Minimal UI elements example"; }

    std::string getAuthor() const override {
        return "alice2 User";
    }

    // Sketch lifecycle
    void setup() override {
        // Initial background
        scene().setBackgroundColor(Color(m_backgroundColor, m_backgroundColor, m_backgroundColor));

        // Minimal UI: construct and add a background slider and a toggle
        m_ui = std::make_unique<SimpleUI>(input());
        m_ui->addSlider("Background", Vec2(10, 60), 180.0f, 0.0f, 1.0f, m_backgroundColor);
        m_ui->addToggle("Show Label", UIRect(10, 80, 120, 20), m_showLabel);
        m_ui->addToggle("Dark Theme", UIRect(140, 80, 120, 20), m_darkTheme);
    }

    void update(float /*deltaTime*/) override {
        // Update background from slider so Application clear() uses it
        scene().setBackgroundColor(Color(m_backgroundColor, m_backgroundColor, m_backgroundColor));
        if (m_ui) {
            m_ui->setTheme(m_darkTheme ? SimpleUI::UITheme::Dark : SimpleUI::UITheme::Light);
        }
    }

    void draw(Renderer& renderer, Camera& /*camera*/) override {
        if (m_showLabel) {
            renderer.setColor(Color(1.0f, 1.0f, 1.0f));
            renderer.drawString("Drag slider: background", 10, 20);
        }
        if (m_ui) m_ui->draw(renderer);
    }

    // Input handling (optional)
    bool onKeyPress(unsigned char key, int x, int y) override {
        // Handle keyboard input
        return false;
    }

    bool onMousePress(int button, int state, int x, int y) override {
        if (m_ui && m_ui->onMousePress(button, state, x, y)) {
            return true; // UI consumed -> block default camera behavior
        }
        return false; // Not handled by UI
    }

    bool onMouseMove(int x, int y) override {
        if (m_ui && m_ui->onMouseMove(x, y)) {
            return true; // UI dragging
        }
        return false; // Not handled by UI
    }

private:
    std::unique_ptr<SimpleUI> m_ui;
    float m_backgroundColor = 0.1f;
    bool  m_showLabel = true;
    bool  m_darkTheme = false;
};

// Register the sketch with alice2 (both old and new systems)
// ALICE2_REGISTER_SKETCH_AUTO(UISketch)

#endif // __MAIN__

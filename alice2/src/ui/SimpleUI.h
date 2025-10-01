#pragma once

#include "../utils/Math.h"
#include "../core/Renderer.h"
#include "../input/InputManager.h"
#include <string>
#include <vector>
#include <functional>

namespace alice2 {

    struct UIRect {
        float x{0}, y{0}, w{0}, h{0};
        bool contains(float px, float py) const {
            return px >= x && px <= x + w && py >= y && py <= y + h;
        }
    };

    // Minimal, immediate-mode-ish UI for simple overlays
    class SimpleUI {
    public:
        explicit SimpleUI(InputManager& input)
            : m_input(input) {}

        // Theme
        enum class UITheme { Light, Dark };
        void setTheme(UITheme theme);
        UITheme getTheme() const { return m_theme; }

        // Toggle button bound to a boolean
        void addToggle(const std::string& label, const UIRect& rect, bool& bound);

        // Radio button group bound to an index (0..N-1)
        void addButtonGroup(const std::vector<std::string>& labels, const Vec2& pos,
                            float buttonW, float buttonH, float spacing, int& selectedIdx);

        // Horizontal slider bound to a float in [minV, maxV]
        void addSlider(const std::string& label, const Vec2& pos,
                       float width, float minV, float maxV, float& bound);

        // Per-event input forwarding from sketch (recommended)
        bool onMousePress(int button, int state, int x, int y); // return true if consumed
        bool onMouseMove(int x, int y); // return true if consumed

        // Draw UI each frame
        void draw(Renderer& r);

        // Clear all controls (optional)
        void clear();

    private:
        struct Palette {
            Color text;
            Color textSecondary;
            Color border;
            Color fill;
            Color selectedBorder;
            Color selectedFill;
            Color sliderTrack;
            Color sliderKnob;
        };

        struct Toggle {
            std::string label;
            UIRect rect;
            bool* bound{nullptr};
            bool pressed{false};
        };

        struct ButtonGroup {
            std::vector<std::string> labels;
            Vec2 pos;
            float w{80.0f}, h{24.0f}, spacing{6.0f};
            int* selected{nullptr};
        };

        struct Slider {
            std::string label;
            Vec2 pos;
            float width{120.0f};
            float minV{0.0f}, maxV{1.0f};
            float* bound{nullptr};
            bool dragging{false};
        };

        InputManager& m_input;
        std::vector<Toggle> m_toggles;
        std::vector<ButtonGroup> m_groups;
        std::vector<Slider> m_sliders;

        // Interaction state
        int m_activeSlider{-1};

        // Theme state
        UITheme m_theme{UITheme::Light};
        Palette m_palette{};

        void updatePalette();

        // Helpers
        static void drawRect(Renderer& r, const UIRect& rc, const Color& color, float lw = 1.0f);
        static void drawRectFilled(Renderer& r, const UIRect& rc, const Color& color);
        static void drawLabel(Renderer& r, const std::string& text, float x, float y, const Color& color);
        static float clampf(float v, float a, float b);
    };

} // namespace alice2

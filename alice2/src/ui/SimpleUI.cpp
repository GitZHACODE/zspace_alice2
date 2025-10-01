#include "SimpleUI.h"
#include "../utils/OpenGL.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace alice2 {

    // Theme handling
    void SimpleUI::setTheme(UITheme theme) {
        if (m_theme != theme) {
            m_theme = theme;
            updatePalette();
        }
    }

    void SimpleUI::updatePalette() {
        if (m_theme == UITheme::Light) {
            m_palette.text           = Color(1.0f, 1.0f, 1.0f, 1.0f);
            m_palette.textSecondary  = Color(0.85f, 0.85f, 0.85f, 1.0f);
            m_palette.border         = Color(0.60f, 0.60f, 0.60f, 1.0f);
            m_palette.fill           = Color(1.0f, 1.0f, 1.0f, 0.06f);
            m_palette.selectedBorder = Color(0.85f, 0.85f, 0.85f, 1.0f);
            m_palette.selectedFill   = Color(1.0f, 1.0f, 1.0f, 0.20f);
            m_palette.sliderTrack    = Color(0.60f, 0.60f, 0.60f, 1.0f);
            m_palette.sliderKnob     = Color(0.90f, 0.90f, 0.90f, 1.0f);
        } else { // Dark
            m_palette.text           = Color(0.0f, 0.0f, 0.0f, 1.0f);
            m_palette.textSecondary  = Color(0.20f, 0.20f, 0.20f, 1.0f);
            m_palette.border         = Color(0.25f, 0.25f, 0.25f, 1.0f);
            m_palette.fill           = Color(0.0f, 0.0f, 0.0f, 0.10f);
            m_palette.selectedBorder = Color(0.40f, 0.40f, 0.40f, 1.0f);
            m_palette.selectedFill   = Color(0.0f, 0.0f, 0.0f, 0.18f);
            m_palette.sliderTrack    = Color(0.25f, 0.25f, 0.25f, 1.0f);
            m_palette.sliderKnob     = Color(0.40f, 0.40f, 0.40f, 1.0f);
        }
    }

    // Public API
    void SimpleUI::addToggle(const std::string& label, const UIRect& rect, bool& bound) {
        m_toggles.push_back(Toggle{label, rect, &bound, false});
    }

    void SimpleUI::addButtonGroup(const std::vector<std::string>& labels, const Vec2& pos,
                                  float buttonW, float buttonH, float spacing, int& selectedIdx) {
        ButtonGroup g;
        g.labels = labels;
        g.pos = pos;
        g.w = buttonW;
        g.h = buttonH;
        g.spacing = spacing;
        g.selected = &selectedIdx;
        m_groups.push_back(std::move(g));
    }

    void SimpleUI::addSlider(const std::string& label, const Vec2& pos,
                             float width, float minV, float maxV, float& bound) {
        Slider s;
        s.label = label;
        s.pos = pos;
        s.width = width;
        s.minV = minV;
        s.maxV = maxV;
        s.bound = &bound;
        m_sliders.push_back(std::move(s));
    }

    bool SimpleUI::onMousePress(int button, int state, int x, int y) {
        // Left button only
        if (button != 0) return false;

        bool consumed = false;

        if (state == 0) { // press
            // Sliders: press near knob or on track -> start dragging
            for (int i = 0; i < static_cast<int>(m_sliders.size()); ++i) {
                auto& s = m_sliders[i];
                if (!s.bound) continue;
                float t = (*s.bound - s.minV) / (s.maxV - s.minV + 1e-12f);
                t = clampf(t, 0.0f, 1.0f);
                float knobX = s.pos.x + t * s.width;
                float knobY = s.pos.y;
                float dx = knobX - x;
                float dy = knobY - y;
                float dist2 = dx*dx + dy*dy;
                const float r = 8.0f; // pixels
                // hit if near knob or within track bounding box
                if (dist2 <= r*r || (x >= s.pos.x && x <= s.pos.x + s.width && std::abs(y - s.pos.y) <= 6.0f)) {
                    s.dragging = true;
                    m_activeSlider = i;
                    // update immediately
                    float u = clampf((x - s.pos.x) / s.width, 0.0f, 1.0f);
                    *s.bound = s.minV + u * (s.maxV - s.minV);
                    consumed = true;
                }
            }

            // Toggles
            if (!consumed) {
                for (auto& t : m_toggles) {
                    if (t.bound && t.rect.contains((float)x, (float)y)) {
                        *t.bound = !(*t.bound);
                        t.pressed = true;
                        consumed = true;
                    }
                }
            }

            // Button groups (radio)
            if (!consumed) {
                for (auto& g : m_groups) {
                    if (!g.selected) continue;
                    for (int i = 0; i < (int)g.labels.size(); ++i) {
                        float bx = g.pos.x + i * (g.w + g.spacing);
                        UIRect rc{bx, g.pos.y, g.w, g.h};
                        if (rc.contains((float)x, (float)y)) {
                            *g.selected = i;
                            consumed = true;
                            break;
                        }
                    }
                    if (consumed) break;
                }
            }

            if (consumed) {
                // Neutralize left button so CameraController doesn't orbit while interacting
                m_input.setMouseButton(MouseButton::Left, KeyState::Released);
            }
        } else { // release
            // Stop any slider dragging
            if (m_activeSlider >= 0 && m_activeSlider < (int)m_sliders.size()) {
                m_sliders[m_activeSlider].dragging = false;
            }
            m_activeSlider = -1;
            for (auto& t : m_toggles) t.pressed = false;
        }

        return consumed;
    }

    bool SimpleUI::onMouseMove(int x, int y) {
        if (m_activeSlider >= 0 && m_activeSlider < (int)m_sliders.size()) {
            auto& s = m_sliders[m_activeSlider];
            if (s.bound) {
                float u = clampf((x - s.pos.x) / s.width, 0.0f, 1.0f);
                *s.bound = s.minV + u * (s.maxV - s.minV);
            }
            // Keep camera neutralized while dragging
            m_input.setMouseButton(MouseButton::Left, KeyState::Released);
            return true;
        }
        return false;
    }

    void SimpleUI::draw(Renderer& r) {
        // Ensure palette initialized
        updatePalette();

        // Toggles
        for (const auto& t : m_toggles) {
            const bool on = t.bound && *t.bound;
            const Color border = on ? m_palette.selectedBorder : m_palette.border;
            const Color fill   = on ? m_palette.selectedFill   : m_palette.fill;
            const Color text   = m_palette.text;

            drawRectFilled(r, t.rect, fill);
            drawRect(r, t.rect, border, t.pressed ? 2.0f : 1.0f);
            drawLabel(r, t.label, t.rect.x + 8.0f, t.rect.y + t.rect.h - 6.0f, text);
        }

        // Button groups
        for (const auto& g : m_groups) {
            int sel = (g.selected ? *g.selected : -1);
            for (int i = 0; i < (int)g.labels.size(); ++i) {
                float bx = g.pos.x + i * (g.w + g.spacing);
                UIRect rc{bx, g.pos.y, g.w, g.h};
                bool on = (i == sel);
                const Color border = on ? m_palette.selectedBorder : m_palette.border;
                const Color fill   = on ? m_palette.selectedFill   : m_palette.fill;
                const Color text   = m_palette.text;
                drawRectFilled(r, rc, fill);
                drawRect(r, rc, border, 1.0f);
                drawLabel(r, g.labels[i], rc.x + 8.0f, rc.y + rc.h - 6.0f, text);
            }
        }

        // Sliders
        for (const auto& s : m_sliders) {
            // Track
            Vec2 a{s.pos.x, s.pos.y};
            Vec2 b{s.pos.x + s.width, s.pos.y};
            r.setColor(m_palette.sliderTrack);
            r.setLineWidth(2.0f);
            r.draw2dLine(a, b);

            // Knob
            float t = 0.0f;
            if (s.bound) t = clampf((*s.bound - s.minV) / (s.maxV - s.minV + 1e-12f), 0.0f, 1.0f);
            Vec2 k{ s.pos.x + t * s.width, s.pos.y };
            r.draw2dPoint(k, m_palette.sliderKnob, 8.0f);

            // Label and value (right side)
            r.setColor(m_palette.textSecondary);
            drawLabel(r, s.label, s.pos.x, s.pos.y - 8.0f, m_palette.textSecondary);

            if (s.bound) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%.3f", *s.bound);
                drawLabel(r, buf, s.pos.x + s.width + 10.0f, s.pos.y + 4.0f, m_palette.text);
            }
        }
    }

    void SimpleUI::clear() {
        m_toggles.clear();
        m_groups.clear();
        m_sliders.clear();
        m_activeSlider = -1;
    }

    // Helpers
    void SimpleUI::drawRect(Renderer& r, const UIRect& rc, const Color& color, float lw) {
        r.setColor(color);
        r.setLineWidth(lw);
        Vec2 p0{rc.x, rc.y};
        Vec2 p1{rc.x + rc.w, rc.y};
        Vec2 p2{rc.x + rc.w, rc.y + rc.h};
        Vec2 p3{rc.x, rc.y + rc.h};
        r.draw2dLine(p0, p1);
        r.draw2dLine(p1, p2);
        r.draw2dLine(p2, p3);
        r.draw2dLine(p3, p0);
    }

    void SimpleUI::drawRectFilled(Renderer& r, const UIRect& rc, const Color& color) {
        // Use immediate mode quad in screen space
        // Save matrices
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        int vx, vy, vw, vh;
        vx = vy = vw = vh = 0;
        // Try to fetch viewport from renderer via private members is not possible here,
        // but using glGetIntegerv maintains consistency
        GLint vp[4];
        glGetIntegerv(GL_VIEWPORT, vp);
        vw = vp[2];
        vh = vp[3];
        glOrtho(0, vw, vh, 0, -1, 1);

        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();

        glColor4f(color.r, color.g, color.b, color.a);
        glBegin(GL_QUADS);
        glVertex2f(rc.x, rc.y);
        glVertex2f(rc.x + rc.w, rc.y);
        glVertex2f(rc.x + rc.w, rc.y + rc.h);
        glVertex2f(rc.x, rc.y + rc.h);
        glEnd();

        // Restore matrices
        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
    }

    void SimpleUI::drawLabel(Renderer& r, const std::string& text, float x, float y, const Color& color) {
        Color old = r.getCurrentColor();
        r.setColor(color);
        r.drawString(text, x, y);
        r.setColor(old);
    }

    float SimpleUI::clampf(float v, float a, float b) {
        return (v < a) ? a : (v > b ? b : v);
    }

} // namespace alice2

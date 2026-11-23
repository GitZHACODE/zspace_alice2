#include "SimpleUI.h"
#include "../utils/OpenGL.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace alice2 {

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
            m_palette.fill           = Color(1.0f, 1.0f, 1.0f, 0.08f);
            m_palette.buttonBorder   = Color(0.25f, 0.72f, 0.95f, 1.0f);
            m_palette.buttonFill     = Color(0.10f, 0.45f, 0.75f, 0.35f);
            m_palette.selectedBorder = Color(0.15f, 0.90f, 0.55f, 1.0f);
            m_palette.selectedFill   = Color(0.10f, 0.75f, 0.45f, 0.28f);
            m_palette.sliderTrack    = Color(0.60f, 0.60f, 0.60f, 1.0f);
            m_palette.sliderKnob     = Color(0.90f, 0.90f, 0.90f, 1.0f);
        } else { // Dark text on light background
            m_palette.text           = Color(0.08f, 0.10f, 0.14f, 1.0f);
            m_palette.textSecondary  = Color(0.25f, 0.28f, 0.34f, 1.0f);
            m_palette.border         = Color(0.45f, 0.48f, 0.54f, 1.0f);
            m_palette.fill           = Color(0.94f, 0.95f, 0.97f, 0.88f);
            m_palette.buttonBorder   = Color(0.10f, 0.42f, 0.78f, 1.0f);
            m_palette.buttonFill     = Color(0.82f, 0.90f, 0.99f, 0.94f);
            m_palette.selectedBorder = Color(0.05f, 0.62f, 0.36f, 1.0f);
            m_palette.selectedFill   = Color(0.76f, 0.95f, 0.85f, 0.94f);
            m_palette.sliderTrack    = Color(0.45f, 0.48f, 0.54f, 1.0f);
            m_palette.sliderKnob     = Color(0.12f, 0.44f, 0.80f, 1.0f);
        }
    }

    void SimpleUI::addButton(const std::string& label, const UIRect& rect, std::function<void()> onClick) {
        m_buttons.push_back(Button{label, rect, std::move(onClick), false});
    }

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
                             float width, float minV, float maxV, float& bound, float step) {
        Slider s;
        s.label = label;
        s.pos = pos;
        s.width = width;
        s.minV = minV;
        s.maxV = maxV;
        s.step = step;
        s.bound = &bound;
        m_sliders.push_back(std::move(s));
    }

    bool SimpleUI::onMousePress(int button, int state, int x, int y) {
        if (button != 0) return false;

        bool consumed = false;

        if (state == 0) { // press
            for (int i = 0; i < static_cast<int>(m_sliders.size()); ++i) {
                auto& s = m_sliders[i];
                if (!s.bound) continue;

                // Optional [-] and [+] fine-step buttons when s.step > 0
                if (s.step > 0.0f) {
                    const UIRect minusRc{ s.pos.x + s.width + 50.0f, s.pos.y - 8.0f, 16.0f, 15.0f };
                    const UIRect plusRc{  s.pos.x + s.width + 69.0f, s.pos.y - 8.0f, 16.0f, 15.0f };
                    if (minusRc.contains(static_cast<float>(x), static_cast<float>(y))) {
                        const float stepped = std::round((*s.bound - s.step) / s.step) * s.step;
                        *s.bound = clampf(stepped, s.minV, s.maxV);
                        consumed = true;
                        break;
                    }
                    if (plusRc.contains(static_cast<float>(x), static_cast<float>(y))) {
                        const float stepped = std::round((*s.bound + s.step) / s.step) * s.step;
                        *s.bound = clampf(stepped, s.minV, s.maxV);
                        consumed = true;
                        break;
                    }
                }

                float t = (*s.bound - s.minV) / (s.maxV - s.minV + 1e-12f);
                t = clampf(t, 0.0f, 1.0f);
                float knobX = s.pos.x + t * s.width;
                float knobY = s.pos.y;
                float dx = knobX - x;
                float dy = knobY - y;
                float dist2 = dx * dx + dy * dy;
                const float r = 8.0f;
                if (dist2 <= r * r) {
                    // Grabbing the knob directly starts precision relative drag without jumping on click
                    s.dragging = true;
                    m_activeSlider = i;
                    m_dragStartX = x;
                    m_dragStartVal = *s.bound;
                    consumed = true;
                } else if (x >= s.pos.x && x <= s.pos.x + s.width && std::abs(y - s.pos.y) <= 6.0f) {
                    s.dragging = true;
                    m_activeSlider = i;
                    float u = clampf((x - s.pos.x) / s.width, 0.0f, 1.0f);
                    float raw = s.minV + u * (s.maxV - s.minV);
                    if (s.step > 0.0f) {
                        raw = std::round(raw / s.step) * s.step;
                    }
                    *s.bound = clampf(raw, s.minV, s.maxV);
                    m_dragStartX = x;
                    m_dragStartVal = *s.bound;
                    consumed = true;
                }
            }

            if (!consumed) {
                for (auto& b : m_buttons) {
                    if (b.rect.contains((float)x, (float)y)) {
                        b.pressed = true;
                        if (b.onClick) b.onClick();
                        consumed = true;
                        break;
                    }
                }
            }

            if (!consumed) {
                for (auto& t : m_toggles) {
                    if (t.bound && t.rect.contains((float)x, (float)y)) {
                        *t.bound = !(*t.bound);
                        t.pressed = true;
                        consumed = true;
                        break;
                    }
                }
            }

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
                m_input.setMouseButton(MouseButton::Left, KeyState::Released);
            }
        } else { // release
            if (m_activeSlider >= 0 && m_activeSlider < (int)m_sliders.size()) {
                m_sliders[m_activeSlider].dragging = false;
            }
            m_activeSlider = -1;
            for (auto& b : m_buttons) b.pressed = false;
            for (auto& t : m_toggles) t.pressed = false;
        }

        return consumed;
    }

    bool SimpleUI::onMouseMove(int x, int y) {
        if (m_activeSlider >= 0 && m_activeSlider < (int)m_sliders.size()) {
            auto& s = m_sliders[m_activeSlider];
            if (s.bound) {
                if (s.step > 0.0f && s.step < 0.5f) {
                    // Precision drag: at most 1 step per ~3 pixels so 0.01 increments never skip values
                    const float naturalPerPx = (s.maxV - s.minV) / std::max(1.0f, s.width);
                    const float perPx = std::min(naturalPerPx, s.step * 0.35f);
                    float raw = m_dragStartVal + static_cast<float>(x - m_dragStartX) * perPx;
                    raw = std::round(raw / s.step) * s.step;
                    *s.bound = clampf(raw, s.minV, s.maxV);
                } else {
                    float u = clampf((x - s.pos.x) / s.width, 0.0f, 1.0f);
                    float raw = s.minV + u * (s.maxV - s.minV);
                    if (s.step > 0.0f) {
                        raw = std::round(raw / s.step) * s.step;
                    }
                    *s.bound = clampf(raw, s.minV, s.maxV);
                }
            }
            m_input.setMouseButton(MouseButton::Left, KeyState::Released);
            return true;
        }
        return false;
    }

    void SimpleUI::draw(Renderer& r) {
        updatePalette();

        // Action Buttons
        for (const auto& b : m_buttons) {
            const Color fill = b.pressed ? m_palette.selectedFill : m_palette.buttonFill;
            const Color border = b.pressed ? m_palette.selectedBorder : m_palette.buttonBorder;
            drawRectFilled(r, b.rect, fill);
            drawRect(r, b.rect, border, b.pressed ? 2.5f : 1.5f);
            drawLabel(r, b.label, b.rect.x + 8.0f, b.rect.y + b.rect.h - 7.0f, m_palette.text);
        }

        // Toggles
        for (const auto& t : m_toggles) {
            const bool on = t.bound && *t.bound;
            const Color border = on ? m_palette.selectedBorder : m_palette.border;
            const Color fill   = on ? m_palette.selectedFill   : m_palette.fill;
            drawRectFilled(r, t.rect, fill);
            drawRect(r, t.rect, border, (on || t.pressed) ? 2.0f : 1.0f);
            drawLabel(r, t.label, t.rect.x + 8.0f, t.rect.y + t.rect.h - 7.0f, m_palette.text);
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
                drawRectFilled(r, rc, fill);
                drawRect(r, rc, border, on ? 2.0f : 1.0f);
                drawLabel(r, g.labels[i], rc.x + 8.0f, rc.y + rc.h - 7.0f, m_palette.text);
            }
        }

        // Sliders
        for (const auto& s : m_sliders) {
            Vec2 a{s.pos.x, s.pos.y};
            Vec2 b{s.pos.x + s.width, s.pos.y};
            r.setColor(m_palette.sliderTrack);
            r.setLineWidth(2.0f);
            r.draw2dLine(a, b);

            float t = 0.0f;
            if (s.bound) t = clampf((*s.bound - s.minV) / (s.maxV - s.minV + 1e-12f), 0.0f, 1.0f);
            Vec2 k{ s.pos.x + t * s.width, s.pos.y };
            r.draw2dPoint(k, m_palette.sliderKnob, 8.0f);

            drawLabel(r, s.label, s.pos.x, s.pos.y - 8.0f, m_palette.textSecondary);

            if (s.bound) {
                char buf[64];
                if (s.step >= 0.99f) {
                    std::snprintf(buf, sizeof(buf), "%.0f", *s.bound);
                } else if (s.step >= 0.009f) {
                    std::snprintf(buf, sizeof(buf), "%.2f", *s.bound);
                } else if (s.maxV <= 0.05f) {
                    std::snprintf(buf, sizeof(buf), "%.5f", *s.bound);
                } else if (s.maxV >= 50.0f) {
                    std::snprintf(buf, sizeof(buf), "%.0f", *s.bound);
                } else {
                    std::snprintf(buf, sizeof(buf), "%.3f", *s.bound);
                }
                drawLabel(r, buf, s.pos.x + s.width + 8.0f, s.pos.y + 4.0f, m_palette.text);

                if (s.step > 0.0f) {
                    const UIRect minusRc{ s.pos.x + s.width + 50.0f, s.pos.y - 8.0f, 16.0f, 15.0f };
                    const UIRect plusRc{  s.pos.x + s.width + 69.0f, s.pos.y - 8.0f, 16.0f, 15.0f };
                    drawRectFilled(r, minusRc, m_palette.buttonFill);
                    drawRect(r, minusRc, m_palette.buttonBorder, 1.0f);
                    drawLabel(r, "-", minusRc.x + 5.0f, minusRc.y + 12.0f, m_palette.text);

                    drawRectFilled(r, plusRc, m_palette.buttonFill);
                    drawRect(r, plusRc, m_palette.buttonBorder, 1.0f);
                    drawLabel(r, "+", plusRc.x + 4.0f, plusRc.y + 12.0f, m_palette.text);
                }
            }
        }
    }

    void SimpleUI::clear() {
        m_buttons.clear();
        m_toggles.clear();
        m_groups.clear();
        m_sliders.clear();
        m_activeSlider = -1;
    }

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
        (void)r;
        glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_LIGHTING);

        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        GLint vp[4];
        glGetIntegerv(GL_VIEWPORT, vp);
        glOrtho(0, vp[2], vp[3], 0, -1, 1);

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

        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
        glPopAttrib();
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

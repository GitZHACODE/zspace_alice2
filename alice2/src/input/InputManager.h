#pragma once

#ifndef ALICE2_INPUT_MANAGER_H
#define ALICE2_INPUT_MANAGER_H

#include "../utils/Math.h"
#include <unordered_map>
#include <functional>

namespace alice2 {

    enum class MouseButton {
        Left = 0,
        Middle = 1,
        Right = 2
    };

    enum class KeyState {
        Released = 0,
        Pressed = 1
    };

    struct MouseState {
        Vec3 position;          // Current mouse position (x, y, 0)
        Vec3 lastPosition;      // Previous mouse position
        Vec3 delta;             // Mouse movement delta
        bool buttons[3];        // Button states (Left, Middle, Right)
        bool lastButtons[3];    // Previous button states
        float wheelDelta;       // Mouse wheel delta
    };

    class InputManager {
    public:
        InputManager();
        ~InputManager() = default;

        // Update (call once per frame)
        void update();

        // Keyboard input
        void setKeyState(unsigned char key, KeyState state);
        bool isKeyPressed(unsigned char key) const;
        bool isKeyDown(unsigned char key) const;
        bool isKeyReleased(unsigned char key) const;

        // Mouse input
        void setMousePosition(int x, int y);
        void setMouseButton(MouseButton button, KeyState state);
        void setMouseWheel(float delta);

        const MouseState& getMouseState() const { return m_mouseState; }
        Vec3 getMousePosition() const { return m_mouseState.position; }
        Vec3 getMouseDelta() const { return m_mouseState.delta; }
        bool isMouseButtonPressed(MouseButton button) const;
        bool isMouseButtonDown(MouseButton button) const;
        bool isMouseButtonReleased(MouseButton button) const;
        float getMouseWheelDelta() const { return m_mouseState.wheelDelta; }

        // Modifiers
        void setModifiers(int modifiers) { m_modifiers = modifiers; }
        int getModifiers() const { return m_modifiers; }
        bool isShiftPressed() const;
        bool isCtrlPressed() const;
        bool isAltPressed() const;

        // Event callbacks
        using KeyCallback = std::function<void(unsigned char, int, int)>;
        using MouseButtonCallback = std::function<void(MouseButton, KeyState, int, int)>;
        using MouseMoveCallback = std::function<void(int, int)>;
        using MouseWheelCallback = std::function<void(float)>;

        void setKeyCallback(KeyCallback callback) { m_keyCallback = callback; }
        void setMouseButtonCallback(MouseButtonCallback callback) { m_mouseButtonCallback = callback; }
        void setMouseMoveCallback(MouseMoveCallback callback) { m_mouseMoveCallback = callback; }
        void setMouseWheelCallback(MouseWheelCallback callback) { m_mouseWheelCallback = callback; }

        // Input processing
        void processKeyboard(unsigned char key, int x, int y);
        void processMouseButton(int button, int state, int x, int y);
        void processMouseMotion(int x, int y);
        void processMouseWheel(float delta);

    private:
        // Keyboard state
        std::unordered_map<unsigned char, KeyState> m_keyStates;
        std::unordered_map<unsigned char, KeyState> m_lastKeyStates;

        // Mouse state
        MouseState m_mouseState;
        int m_modifiers;

        // Callbacks
        KeyCallback m_keyCallback;
        MouseButtonCallback m_mouseButtonCallback;
        MouseMoveCallback m_mouseMoveCallback;
        MouseWheelCallback m_mouseWheelCallback;

        void updateMouseDelta();
    };

} // namespace alice2

#endif // ALICE2_INPUT_MANAGER_H

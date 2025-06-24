#include "InputManager.h"
#include <GLFW/glfw3.h>
#include <iostream>

// Debug logging flag - set to true to enable detailed input logging
#define DEBUG_INPUT_LOGGING false
#define DEBUG_MOUSE_BUTTON_LOGGING false

namespace alice2 {

    InputManager::InputManager()
        : m_modifiers(0)
    {
        // Initialize mouse state
        m_mouseState.position = Vec3(0, 0, 0);
        m_mouseState.lastPosition = Vec3(0, 0, 0);
        m_mouseState.delta = Vec3(0, 0, 0);
        m_mouseState.wheelDelta = 0.0f;
        
        for (int i = 0; i < 3; i++) {
            m_mouseState.buttons[i] = false;
            m_mouseState.lastButtons[i] = false;
        }
    }

    void InputManager::update() {
        if (DEBUG_INPUT_LOGGING) {
            std::cout << "[INPUT] InputManager::update() - Before update:" << std::endl;
            std::cout << "  Mouse pos: (" << m_mouseState.position.x << ", " << m_mouseState.position.y << ")" << std::endl;
            std::cout << "  Mouse delta: (" << m_mouseState.delta.x << ", " << m_mouseState.delta.y << ")" << std::endl;
            std::cout << "  Buttons: L=" << m_mouseState.buttons[0] << " M=" << m_mouseState.buttons[1] << " R=" << m_mouseState.buttons[2] << std::endl;
            std::cout << "  Wheel delta: " << m_mouseState.wheelDelta << std::endl;
        }

        // Update last states
        m_lastKeyStates = m_keyStates;

        for (int i = 0; i < 3; i++) {
            m_mouseState.lastButtons[i] = m_mouseState.buttons[i];
        }

        // Update mouse delta
        updateMouseDelta();

        if (DEBUG_INPUT_LOGGING) {
            std::cout << "[INPUT] InputManager::update() - After delta update:" << std::endl;
            std::cout << "  Mouse delta: (" << m_mouseState.delta.x << ", " << m_mouseState.delta.y << ")" << std::endl;
        }

        // Reset wheel delta
        m_mouseState.wheelDelta = 0.0f;

        if (DEBUG_INPUT_LOGGING) {
            std::cout << "[INPUT] InputManager::update() - Complete" << std::endl;
        }
    }

    void InputManager::setKeyState(unsigned char key, KeyState state) {
        m_keyStates[key] = state;
    }

    bool InputManager::isKeyPressed(unsigned char key) const {
        auto it = m_keyStates.find(key);
        auto lastIt = m_lastKeyStates.find(key);
        
        bool currentPressed = (it != m_keyStates.end() && it->second == KeyState::Pressed);
        bool lastPressed = (lastIt != m_lastKeyStates.end() && lastIt->second == KeyState::Pressed);
        
        return currentPressed && !lastPressed;
    }

    bool InputManager::isKeyDown(unsigned char key) const {
        auto it = m_keyStates.find(key);
        return (it != m_keyStates.end() && it->second == KeyState::Pressed);
    }

    bool InputManager::isKeyReleased(unsigned char key) const {
        auto it = m_keyStates.find(key);
        auto lastIt = m_lastKeyStates.find(key);
        
        bool currentPressed = (it != m_keyStates.end() && it->second == KeyState::Pressed);
        bool lastPressed = (lastIt != m_lastKeyStates.end() && lastIt->second == KeyState::Pressed);
        
        return !currentPressed && lastPressed;
    }

    void InputManager::setMousePosition(int x, int y) {
        m_mouseState.lastPosition = m_mouseState.position;
        m_mouseState.position = Vec3(static_cast<float>(x), static_cast<float>(y), 0);
    }

    void InputManager::setMouseButton(MouseButton button, KeyState state) {
        int index = static_cast<int>(button);
        if (index >= 0 && index < 3) {
            m_mouseState.buttons[index] = (state == KeyState::Pressed);
        }
    }

    void InputManager::setMouseWheel(float delta) {
        m_mouseState.wheelDelta = delta;
    }

    bool InputManager::isMouseButtonPressed(MouseButton button) const {
        int index = static_cast<int>(button);
        if (index >= 0 && index < 3) {
            return m_mouseState.buttons[index] && !m_mouseState.lastButtons[index];
        }
        return false;
    }

    bool InputManager::isMouseButtonDown(MouseButton button) const {
        int index = static_cast<int>(button);
        if (index >= 0 && index < 3) {
            return m_mouseState.buttons[index];
        }
        return false;
    }

    bool InputManager::isMouseButtonReleased(MouseButton button) const {
        int index = static_cast<int>(button);
        if (index >= 0 && index < 3) {
            return !m_mouseState.buttons[index] && m_mouseState.lastButtons[index];
        }
        return false;
    }

    bool InputManager::isShiftPressed() const {
        return (m_modifiers & GLFW_MOD_SHIFT) != 0;
    }

    bool InputManager::isCtrlPressed() const {
        return (m_modifiers & GLFW_MOD_CONTROL) != 0;
    }

    bool InputManager::isAltPressed() const {
        return (m_modifiers & GLFW_MOD_ALT) != 0;
    }

    void InputManager::processKeyboard(unsigned char key, int x, int y) {
        // Note: modifiers will be set by the application when calling this function
        setKeyState(key, KeyState::Pressed);

        if (m_keyCallback) {
            m_keyCallback(key, x, y);
        }
    }

    void InputManager::processMouseButton(int button, int state, int x, int y) {
        if (DEBUG_MOUSE_BUTTON_LOGGING) {
            std::cout << "[INPUT] processMouseButton: button=" << button << " state=" << state << " pos=(" << x << "," << y << ")" << std::endl;
        }

        // Note: modifiers will be set by the application when calling this function
        setMousePosition(x, y);

        MouseButton mb;
        switch (button) {
            case GLFW_MOUSE_BUTTON_LEFT: mb = MouseButton::Left; break;
            case GLFW_MOUSE_BUTTON_MIDDLE: mb = MouseButton::Middle; break;
            case GLFW_MOUSE_BUTTON_RIGHT: mb = MouseButton::Right; break;
            default: return;
        }

        KeyState ks = (state == 0) ? KeyState::Pressed : KeyState::Released;  // 0 = pressed, 1 = released (GLUT compatibility)
        setMouseButton(mb, ks);

        if (DEBUG_MOUSE_BUTTON_LOGGING) {
            const char* buttonName = (mb == MouseButton::Left) ? "LEFT" : (mb == MouseButton::Middle) ? "MIDDLE" : "RIGHT";
            const char* stateName = (ks == KeyState::Pressed) ? "PRESSED" : "RELEASED";
            std::cout << "[INPUT] Mouse button " << buttonName << " " << stateName << " at (" << x << "," << y << ")" << std::endl;
        }

        if (m_mouseButtonCallback) {
            m_mouseButtonCallback(mb, ks, x, y);
        }
    }

    void InputManager::processMouseMotion(int x, int y) {
        if (DEBUG_INPUT_LOGGING) {
            std::cout << "[INPUT] processMouseMotion: pos=(" << x << "," << y << ")" << std::endl;
        }

        setMousePosition(x, y);

        if (DEBUG_INPUT_LOGGING) {
            std::cout << "[INPUT] Mouse motion delta: (" << m_mouseState.delta.x << "," << m_mouseState.delta.y << ")" << std::endl;
        }

        if (m_mouseMoveCallback) {
            m_mouseMoveCallback(x, y);
        }
    }

    void InputManager::processMouseWheel(float delta) {
        if (DEBUG_INPUT_LOGGING) {
            std::cout << "[INPUT] processMouseWheel: delta=" << delta << std::endl;
        }

        setMouseWheel(delta);

        if (m_mouseWheelCallback) {
            m_mouseWheelCallback(delta);
        }
    }

    void InputManager::updateMouseDelta() {
        m_mouseState.delta = m_mouseState.position - m_mouseState.lastPosition;
        m_mouseState.lastPosition = m_mouseState.position;
    }

} // namespace alice2

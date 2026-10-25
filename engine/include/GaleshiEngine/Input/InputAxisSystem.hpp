#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

union SDL_Event;

namespace Galeshi {

enum class InputBindingType {
    Key,
    MouseButton,
};

struct InputAxisBinding {
    InputBindingType type = InputBindingType::Key;
    std::string code;
    float direction = 1.0f;
};

enum class InputMouseSource {
    None,
    X,
    Y,
    Wheel,
};

struct InputAxis {
    std::string name;
    std::vector<InputAxisBinding> bindings;
    InputMouseSource mouseSource = InputMouseSource::None;

    float sensitivity = 1.0f;
    float deadZone = 0.1f;
    bool invert = false;
    bool clampValue = true;

    // Runtime values, refreshed once per frame.
    float value = 0.0f;
    float rawValue = 0.0f;
};

// Per-frame keyboard and mouse axes. Defaults provide a small Unity-like set:
// Horizontal, Vertical, Jump, Mouse X, Mouse Y and Mouse ScrollWheel.
class InputAxisSystem {
public:
    InputAxisSystem();

    void ResetDefaults();
    void Update(float dt);
    void ProcessEvent(const SDL_Event& event);

    float GetAxis(const std::string& name) const;
    float GetAxisRaw(const std::string& name) const;

    bool IsKeyDown(const std::string& key) const;
    bool IsMouseButtonDown(const std::string& button) const;

    glm::vec2 MousePosition() const { return {m_mouseX, m_mouseY}; }
    glm::vec2 MouseDelta() const { return {m_mouseDX, m_mouseDY}; }
    float MouseWheel() const { return m_mouseWheel; }

    InputAxis* FindAxis(const std::string& name);
    const InputAxis* FindAxis(const std::string& name) const;

    std::vector<InputAxis>& Axes() { return m_axes; }
    const std::vector<InputAxis>& Axes() const { return m_axes; }

    static std::string BindingLabel(const InputAxisBinding& binding);
    static std::string MouseSourceLabel(InputMouseSource source);

private:
    static int KeyNameToScancode(const std::string& name);
    static int MouseButtonNameToButton(const std::string& name);
    static std::string NormalizeName(const std::string& name);

    std::vector<InputAxis> m_axes;

    const bool* m_keyboardState = nullptr;
    uint32_t m_mouseButtons = 0;

    float m_mouseX = 0.0f;
    float m_mouseY = 0.0f;
    float m_previousMouseX = 0.0f;
    float m_previousMouseY = 0.0f;
    float m_mouseDX = 0.0f;
    float m_mouseDY = 0.0f;
    float m_mouseWheel = 0.0f;
    float m_pendingWheel = 0.0f;
    bool m_hasMousePosition = false;
};

} // namespace Galeshi

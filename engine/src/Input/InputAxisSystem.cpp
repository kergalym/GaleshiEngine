#include "GaleshiEngine/Input/InputAxisSystem.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace Galeshi {

namespace {

std::string Trim(const std::string& value) {
    size_t begin = 0;
    while (begin < value.size() &&
           std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }

    size_t end = value.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }

    return value.substr(begin, end - begin);
}

} // namespace

InputAxisSystem::InputAxisSystem() {
    ResetDefaults();
}

void InputAxisSystem::ResetDefaults() {
    m_axes.clear();

    InputAxis horizontal;
    horizontal.name = "Horizontal";
    horizontal.bindings = {
        {InputBindingType::Key, "D", 1.0f},
        {InputBindingType::Key, "A", -1.0f},
        {InputBindingType::Key, "Right", 1.0f},
        {InputBindingType::Key, "Left", -1.0f},
    };
    m_axes.push_back(std::move(horizontal));

    InputAxis vertical;
    vertical.name = "Vertical";
    vertical.bindings = {
        {InputBindingType::Key, "W", 1.0f},
        {InputBindingType::Key, "S", -1.0f},
        {InputBindingType::Key, "Up", 1.0f},
        {InputBindingType::Key, "Down", -1.0f},
    };
    m_axes.push_back(std::move(vertical));

    InputAxis jump;
    jump.name = "Jump";
    jump.bindings = {
        {InputBindingType::Key, "Space", 1.0f},
    };
    m_axes.push_back(std::move(jump));

    InputAxis mouseX;
    mouseX.name = "Mouse X";
    mouseX.mouseSource = InputMouseSource::X;
    mouseX.sensitivity = 0.1f;
    mouseX.deadZone = 0.0f;
    mouseX.clampValue = false;
    m_axes.push_back(std::move(mouseX));

    InputAxis mouseY;
    mouseY.name = "Mouse Y";
    mouseY.mouseSource = InputMouseSource::Y;
    mouseY.sensitivity = 0.1f;
    mouseY.deadZone = 0.0f;
    mouseY.invert = true;
    mouseY.clampValue = false;
    m_axes.push_back(std::move(mouseY));

    InputAxis mouseWheel;
    mouseWheel.name = "Mouse ScrollWheel";
    mouseWheel.mouseSource = InputMouseSource::Wheel;
    mouseWheel.sensitivity = 1.0f;
    mouseWheel.deadZone = 0.0f;
    mouseWheel.clampValue = false;
    m_axes.push_back(std::move(mouseWheel));
}

void InputAxisSystem::Update(float dt) {
    (void)dt;

    m_keyboardState = SDL_GetKeyboardState(nullptr);

    float mouseX = 0.0f;
    float mouseY = 0.0f;
    m_mouseButtons = static_cast<uint32_t>(SDL_GetMouseState(&mouseX, &mouseY));

    if (m_hasMousePosition) {
        m_mouseDX = mouseX - m_previousMouseX;
        m_mouseDY = mouseY - m_previousMouseY;
    } else {
        m_mouseDX = 0.0f;
        m_mouseDY = 0.0f;
        m_hasMousePosition = true;
    }

    m_previousMouseX = mouseX;
    m_previousMouseY = mouseY;
    m_mouseX = mouseX;
    m_mouseY = mouseY;

    m_mouseWheel = m_pendingWheel;
    m_pendingWheel = 0.0f;

    for (InputAxis& axis : m_axes) {
        float raw = 0.0f;

        for (const InputAxisBinding& binding : axis.bindings) {
            const bool down = binding.type == InputBindingType::Key
                                  ? IsKeyDown(binding.code)
                                  : IsMouseButtonDown(binding.code);
            if (down) {
                raw += binding.direction;
            }
        }

        switch (axis.mouseSource) {
        case InputMouseSource::X:
            raw += m_mouseDX;
            break;
        case InputMouseSource::Y:
            raw += m_mouseDY;
            break;
        case InputMouseSource::Wheel:
            raw += m_mouseWheel;
            break;
        case InputMouseSource::None:
            break;
        }

        if (axis.clampValue) {
            raw = std::clamp(raw, -1.0f, 1.0f);
        }

        raw *= axis.sensitivity;
        if (axis.invert) {
            raw = -raw;
        }
        axis.rawValue = raw;

        float value = raw;
        if (axis.deadZone > 0.0f) {
            const float deadZone = std::clamp(axis.deadZone, 0.0f, 0.999f);
            if (std::fabs(value) <= deadZone) {
                value = 0.0f;
            } else if (axis.clampValue) {
                value = std::copysign(
                    (std::fabs(value) - deadZone) / (1.0f - deadZone), value);
            }
        }

        axis.value = value;
    }
}

void InputAxisSystem::ProcessEvent(const SDL_Event& event) {
    if (event.type != SDL_EVENT_MOUSE_WHEEL) {
        return;
    }

    float wheel = event.wheel.y;
    if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
        wheel = -wheel;
    }
    m_pendingWheel += wheel;
}

float InputAxisSystem::GetAxis(const std::string& name) const {
    const InputAxis* axis = FindAxis(name);
    return axis ? axis->value : 0.0f;
}

float InputAxisSystem::GetAxisRaw(const std::string& name) const {
    const InputAxis* axis = FindAxis(name);
    return axis ? axis->rawValue : 0.0f;
}

bool InputAxisSystem::IsKeyDown(const std::string& key) const {
    const int scancode = KeyNameToScancode(key);
    if (scancode == SDL_SCANCODE_UNKNOWN) {
        return false;
    }

    if (!m_keyboardState) {
        const bool* state = SDL_GetKeyboardState(nullptr);
        return state && state[scancode];
    }

    return m_keyboardState[scancode];
}

bool InputAxisSystem::IsMouseButtonDown(const std::string& button) const {
    const int sdlButton = MouseButtonNameToButton(button);
    if (sdlButton == 0) {
        return false;
    }

    return (m_mouseButtons & SDL_BUTTON_MASK(sdlButton)) != 0;
}

InputAxis* InputAxisSystem::FindAxis(const std::string& name) {
    const std::string key = NormalizeName(name);
    for (InputAxis& axis : m_axes) {
        if (NormalizeName(axis.name) == key) {
            return &axis;
        }
    }
    return nullptr;
}

const InputAxis* InputAxisSystem::FindAxis(const std::string& name) const {
    const std::string key = NormalizeName(name);
    for (const InputAxis& axis : m_axes) {
        if (NormalizeName(axis.name) == key) {
            return &axis;
        }
    }
    return nullptr;
}

std::string InputAxisSystem::BindingLabel(const InputAxisBinding& binding) {
    if (binding.code.empty()) {
        return "None";
    }

    if (binding.type == InputBindingType::MouseButton) {
        return "Mouse " + binding.code;
    }

    return binding.code;
}

std::string InputAxisSystem::MouseSourceLabel(InputMouseSource source) {
    switch (source) {
    case InputMouseSource::X:
        return "Mouse X";
    case InputMouseSource::Y:
        return "Mouse Y";
    case InputMouseSource::Wheel:
        return "Mouse ScrollWheel";
    case InputMouseSource::None:
        break;
    }

    return "None";
}

int InputAxisSystem::KeyNameToScancode(const std::string& name) {
    const std::string trimmed = Trim(name);
    if (trimmed.empty()) {
        return SDL_SCANCODE_UNKNOWN;
    }

    const std::string normalized = NormalizeName(trimmed);
    if (normalized == "shift") {
        return SDL_SCANCODE_LSHIFT;
    }
    if (normalized == "ctrl" || normalized == "control") {
        return SDL_SCANCODE_LCTRL;
    }
    if (normalized == "alt") {
        return SDL_SCANCODE_LALT;
    }
    if (normalized == "esc") {
        return SDL_SCANCODE_ESCAPE;
    }
    if (normalized == "return") {
        return SDL_SCANCODE_RETURN;
    }

    SDL_Scancode scancode = SDL_GetScancodeFromName(trimmed.c_str());
    if (scancode == SDL_SCANCODE_UNKNOWN) {
        std::string upper = trimmed;
        std::transform(upper.begin(), upper.end(), upper.begin(),
                       [](unsigned char c) {
                           return static_cast<char>(std::toupper(c));
                       });
        scancode = SDL_GetScancodeFromName(upper.c_str());
    }

    return static_cast<int>(scancode);
}

int InputAxisSystem::MouseButtonNameToButton(const std::string& name) {
    const std::string normalized = NormalizeName(name);

    if (normalized == "left" || normalized == "lmb" ||
        normalized == "mouse left") {
        return SDL_BUTTON_LEFT;
    }
    if (normalized == "right" || normalized == "rmb" ||
        normalized == "mouse right") {
        return SDL_BUTTON_RIGHT;
    }
    if (normalized == "middle" || normalized == "mmb" ||
        normalized == "mouse middle") {
        return SDL_BUTTON_MIDDLE;
    }
    if (normalized == "x1" || normalized == "mouse x1") {
        return SDL_BUTTON_X1;
    }
    if (normalized == "x2" || normalized == "mouse x2") {
        return SDL_BUTTON_X2;
    }

    return 0;
}

std::string InputAxisSystem::NormalizeName(const std::string& name) {
    std::string normalized = Trim(name);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return normalized;
}

} // namespace Galeshi

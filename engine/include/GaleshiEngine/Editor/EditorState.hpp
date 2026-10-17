#pragma once

#include "GaleshiEngine/Scene/Entity.hpp"
#include <glm/glm.hpp>

namespace Galeshi {

enum class GizmoMode { Translate, Rotate, Scale };

class EditorState {
public:
    // --- Selection ---
    Entity&       Selected()       { return m_selected; }
    const Entity& Selected() const { return m_selected; }
    void Select(Entity e) { m_selected = e; }
    void ClearSelection() { m_selected = Entity{}; }

    // --- Gizmo ---
    GizmoMode Mode() const { return m_mode; }
    void SetMode(GizmoMode m) { m_mode = m; }

    // --- Editor camera (orbit) ---
    float     camYaw    = 0.0f;
    float     camPitch  = 0.35f;
    float     camRadius = 8.0f;
    glm::vec3 camTarget{0.0f};

private:
    Entity    m_selected{};
    GizmoMode m_mode = GizmoMode::Translate;
};

} // namespace Galeshi

#pragma once

#include "GaleshiEngine/Scene/Entity.hpp"
#include <glm/glm.hpp>
#include <unordered_set>
#include <vector>

namespace Galeshi {

enum class GizmoMode { Translate, Rotate, Scale };

class EditorState {
public:
    // --- Selection (multi) ---
    bool IsSelected(Entity e) const {
        return m_selected.count(e.Handle()) > 0;
    }

    void Select(Entity e) {
        m_selected.clear();
        if (e) m_selected.insert(e.Handle());
    }

    void AddToSelection(Entity e) {
        if (e) m_selected.insert(e.Handle());
    }

    void RemoveFromSelection(Entity e) {
        m_selected.erase(e.Handle());
    }

    void ClearSelection() { m_selected.clear(); }

    // Первая выделенная (для Details) — может быть пустой.
    Entity Selected(Scene* scene) const;

    // Все выделенные
    std::vector<Entity> SelectedAll(Scene* scene) const;

    bool HasSelection() const { return !m_selected.empty(); }

    // --- Gizmo ---
    GizmoMode Mode() const { return m_mode; }
    void SetMode(GizmoMode m) { m_mode = m; }

    // --- Gizmo-lock (не двигать камеру, когда ImGuizmo активен) ---
    bool GizmoLocked() const { return m_gizmoLocked; }
    void SetGizmoLocked(bool v) { m_gizmoLocked = v; }

    // --- Editor camera (orbit) ---
    float     camYaw    = 0.0f;
    float     camPitch  = 0.35f;
    float     camRadius = 8.0f;
    glm::vec3 camTarget{0.0f};

private:
    std::unordered_set<entt::entity> m_selected;
    GizmoMode m_mode = GizmoMode::Translate;
    bool      m_gizmoLocked = false;
};

} // namespace Galeshi

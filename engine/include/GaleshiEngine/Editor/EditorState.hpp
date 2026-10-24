#pragma once

#include "GaleshiEngine/Scene/Entity.hpp"
#include <glm/glm.hpp>
#include <unordered_set>
#include <vector>

namespace Galeshi {

enum class GizmoMode { Translate, Rotate, Scale };
class EditorState {
public:
    enum class ToolMode { Select, Raise, Lower, Smooth, Flatten, Noise };
    

    // --- Selection ---
    bool IsSelected(Entity e) const { return m_selected.count(e.Handle()) > 0; }
    void Select(Entity e) {
        m_selected.clear();
        if (e) m_selected.insert(e.Handle());
    }
    void AddToSelection(Entity e)    { if (e) m_selected.insert(e.Handle()); }
    void RemoveFromSelection(Entity e) { m_selected.erase(e.Handle()); }
    void ClearSelection() { m_selected.clear(); }

    Entity Selected(Scene* scene) const {
        if (m_selected.empty() || !scene) return {};
        return Entity{ *m_selected.begin(), scene };
    }

    std::vector<Entity> SelectedAll(Scene* scene) const {
        std::vector<Entity> out;
        out.reserve(m_selected.size());
        for (auto h : m_selected) out.emplace_back(h, scene);
        return out;
    }

    bool HasSelection() const { return !m_selected.empty(); }

    // --- Gizmo ---
    GizmoMode Mode() const { return m_mode; }
    void SetMode(GizmoMode m) { m_mode = m; }

    bool GizmoLocked() const { return m_gizmoLocked; }
    void SetGizmoLocked(bool v) { m_gizmoLocked = v; }

    // --- Tool (для Landscape Sculpting) ---
    ToolMode toolMode = ToolMode::Select;
    float brushRadius = 4.0f;
    float brushStrength = 3.0f;
    float brushFalloff = 0.65f;
    float flattenHeight = 0.0f;
    float noiseFrequency = 0.15f;

    // --- Play Mode ---
    bool isPlaying = false;

    // --- Editor Camera (orbit) ---
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

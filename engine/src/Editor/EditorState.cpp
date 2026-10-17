#include "GaleshiEngine/Editor/EditorState.hpp"
#include "GaleshiEngine/Scene/Scene.hpp"

namespace Galeshi {

Entity EditorState::Selected(Scene* scene) const {
    if (m_selected.empty() || !scene) return {};
    return Entity{ *m_selected.begin(), scene };
}

std::vector<Entity> EditorState::SelectedAll(Scene* scene) const {
    std::vector<Entity> out;
    out.reserve(m_selected.size());
    for (auto h : m_selected) out.emplace_back(h, scene);
    return out;
}

} // namespace Galeshi

#include "GaleshiEngine/Scene/Scene.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace Galeshi {

glm::mat4 TransformComponent::Matrix() const {
    glm::mat4 T = glm::translate(glm::mat4(1.0f), position);
    glm::mat4 R = glm::mat4_cast(rotation);
    glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);
    return T * R * S;
}

Entity Scene::CreateEntity(const std::string& name) {
    Entity e{ m_registry.create(), this };
    e.AddComponent<NameComponent>(NameComponent{ name });
    e.AddComponent<TransformComponent>();
    return e;
}

void Scene::DestroyEntity(Entity e) {
    m_registry.destroy(e.Handle());
}

std::vector<Entity> Scene::AllEntities() {
    std::vector<Entity> out;
    auto view = m_registry.view<NameComponent>();
    out.reserve(view.size());
    for (auto h : view) out.emplace_back(h, this);
    return out;
}

Entity Scene::FindEntityByName(const std::string& name) {
    auto view = m_registry.view<NameComponent>();
    for (auto h : view) {
        if (view.get<NameComponent>(h).name == name)
            return Entity{ h, this };
    }
    return {};
}

} // namespace Galeshi

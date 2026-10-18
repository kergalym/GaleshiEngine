#pragma once

#include <entt/entt.hpp>
#include <string>
#include <utility>
#include <type_traits>

namespace Galeshi {

class Scene;

class Entity {
public:
    Entity() = default;
    Entity(entt::entity handle, Scene* scene) : m_handle(handle), m_scene(scene) {}

        template<typename T, typename... Args>
    T& AddComponent(Args&&... args) {
        auto& reg = m_scene->Registry();
        if constexpr (std::is_empty_v<T>) {
            reg.template emplace<T>(m_handle);
            static T s_dummy{};
            return s_dummy;
        } else {
            return reg.template emplace<T>(m_handle, std::forward<Args>(args)...);
        }
    }

    template<typename T>
    T& GetComponent() {
        return m_scene->Registry().template get<T>(m_handle);
    }

    template<typename T>
    const T& GetComponent() const {
        return m_scene->Registry().template get<T>(m_handle);
    }

    template<typename T>
    bool HasComponent() const {
        return m_scene->Registry().template all_of<T>(m_handle);
    }

    template<typename T>
    void RemoveComponent() {
        m_scene->Registry().template remove<T>(m_handle);
    }

    operator bool() const { return m_handle != entt::null; }
    operator entt::entity() const { return m_handle; }

    bool operator==(const Entity& o) const { return m_handle == o.m_handle; }
    bool operator!=(const Entity& o) const { return m_handle != o.m_handle; }

    entt::entity Handle() const { return m_handle; }

private:
    entt::entity m_handle{ entt::null };
    Scene*       m_scene  = nullptr;
};

} // namespace Galeshi

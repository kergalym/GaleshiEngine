#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>
#include <string>

namespace Galeshi {

class Mesh;
class Material;

struct NameComponent {
    std::string name = "Entity";
};

struct TransformComponent {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};

    glm::mat4 Matrix() const;
};

struct MeshComponent {
    std::shared_ptr<Mesh> mesh;
};

struct MaterialComponent {
    std::shared_ptr<Material> material;
};

struct CameraComponent {
    float fovY      = 60.0f;    // градусы
    float nearZ     = 0.1f;
    float farZ      = 1000.0f;
    bool  primary   = true;
};

struct DirectionalLightComponent {
    glm::vec3 direction{-0.4f, -0.8f, -0.5f};
    glm::vec3 color{1.0f, 0.97f, 0.9f};
    float     intensity = 1.2f;
};

struct PointLightComponent {
    glm::vec3 color{1.0f};
    float     intensity = 1.0f;
    float     radius    = 10.0f;
};

} // namespace Galeshi

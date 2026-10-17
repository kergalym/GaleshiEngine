#pragma once

#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <string>
#include <vector>

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

    glm::mat4 LocalMatrix() const;
};

struct ParentComponent {
    entt::entity parent = entt::null;
};

struct MeshComponent {
    std::shared_ptr<Mesh> mesh;
    std::string           sourceAssetPath;
    std::string           primitiveType;
    int                   meshIndex = 0;

    MeshComponent() = default;
    MeshComponent(std::shared_ptr<Mesh> m, std::string path = {}, std::string prim = {}, int mi = 0)
        : mesh(std::move(m)), sourceAssetPath(std::move(path)), primitiveType(std::move(prim)), meshIndex(mi) {}
};

struct SkeletalComponent {
    bool isAnimated = false;
    std::string sourcePath;
    float currentTime = 0.0f;
    float animationSpeed = 1.0f;
    int activeAnimation = 0;
    std::vector<glm::mat4> boneMatrices;
};

struct MaterialComponent {
    std::shared_ptr<Material> material;
    MaterialComponent() = default;
    explicit MaterialComponent(std::shared_ptr<Material> m) : material(std::move(m)) {}
};

struct CameraComponent {
    float fovY    = 60.0f;
    float nearZ   = 0.1f;
    float farZ   = 1000.0f;
    bool  primary = true;
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

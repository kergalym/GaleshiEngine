#pragma once

#include <cstdint>

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
    bool isGlass = false;
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

struct RigidBodyComponent {
    enum class MotionType { Static, Dynamic, Kinematic };
    MotionType motionType = MotionType::Dynamic;
    float      mass       = 1.0f;
    float      friction   = 0.5f;
    float      restitution = 0.0f;
    bool       useGravity = true;

    // Runtime handle to Jolt body; 0xFFFFFFFF = not created.
    uint32_t   bodyId     = 0xFFFFFFFFu;
};

struct BoxColliderComponent {
    glm::vec3 halfExtents{0.5f};
};

struct LandscapeComponent {
    int sizeX = 100;
    int sizeZ = 100;
    float scale = 1.0f;
    std::vector<float> heights;
};

struct NavAgentComponent {
    std::vector<glm::vec3> path;
    size_t currentWaypoint = 0;
    float speed = 3.0f;
    float stoppingDistance = 0.5f;
};

struct SoftBodyComponent {
    int gridX = 10;
    int gridY = 10;
    float spacing = 0.2f;
    glm::vec3 origin = {0, 3, 0};
    float stiffness = 0.8f;
    std::vector<glm::vec3> vertices;
    std::vector<uint32_t> indices;
    uint32_t softBodyId = 0xFFFFFFFF;
};

struct AudioSourceComponent {
    std::string soundPath;
    bool        loop     = false;
    bool        autoplay = false;
    bool        playing  = false;
};

// Tag: entity was created by a Lua script and should be cleaned on hot-reload.
struct LuaEntityComponent {};


struct SpotLightComponent {
    glm::vec3 direction = {0.0f, -1.0f, 0.0f}; // Направление взгляда прожектора
    float innerConeAngle = 15.0f;  // Внутренний угол (градусы), где свет полный
    float outerConeAngle = 30.0f;  // Внешний угол (градусы), где свет затухает до 0
    float radius = 10.0f;          // Радиус действия
    glm::vec3 color = {1.0f, 1.0f, 1.0f};      // Цвет
    float intensity = 1.0f;        // Интенсивность
};
} // namespace Galeshi

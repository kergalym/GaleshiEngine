#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <functional>
#include <vector>
#include <entt/entt.hpp>

namespace JPH { class JobSystem; }

namespace Galeshi {

class Scene;
class AssetManager;
struct ColliderComponent;
struct RigidBodyComponent;
struct SoftBodyComponent;
struct LandscapeComponent;

class EnginePhysicsSystem {
public:
    EnginePhysicsSystem();
    ~EnginePhysicsSystem();

    bool Init();
    void Shutdown();
    void Reset();
    void Update(Scene& scene, float deltaTime);
    void SetBodyTransform(uint32_t bodyId, const glm::vec3& position, const glm::quat& rotation);
    void AddImpulse(uint32_t bodyId, const glm::vec3& impulse);
    void SetLinearVelocity(uint32_t bodyId, const glm::vec3& velocity);
    glm::vec3 GetLinearVelocity(uint32_t bodyId) const;

    uint32_t CreateBox(const glm::vec3& halfExtents, const glm::vec3& position, entt::entity entity = entt::null, bool isStatic = false);
    uint32_t CreateSphere(float radius, const glm::vec3& position, entt::entity entity = entt::null, bool isStatic = false);
    uint32_t CreateCapsule(float halfHeight, float radius, const glm::vec3& position, entt::entity entity = entt::null, bool isStatic = false);

    // Creates a static Jolt heightfield collider from a landscape height map.
    // The heightfield is sampled in landscape-local space and placed at the
    // supplied world matrix. Non-square maps are padded with no-collision
    // samples because Jolt heightfields use a square sample grid.
    uint32_t CreateHeightfield(const LandscapeComponent& landscape,
                               const glm::mat4& world,
                               entt::entity entity = entt::null,
                               float friction = 0.8f);

    // Creates a body from an authored collider + rigid body. The collider
    // shape, offset and material are applied, and the rigid body controls
    // motion type, mass and gravity.
    uint32_t CreateCollider(const ColliderComponent& collider,
                            const RigidBodyComponent& body,
                            const glm::vec3& position,
                            const glm::quat& rotation,
                            entt::entity entity = entt::null);
    void DestroyBody(uint32_t bodyId);

    // Sweep a sphere from origin to target and report the first body accepted
    // by filter. Bodies without a mapped entity are ignored, so the internal
    // default ground plane never participates.
    bool SphereCast(const glm::vec3& origin, const glm::vec3& target,
                    float radius,
                    const std::function<bool(entt::entity)>& filter,
                    glm::vec3& hitPoint) const;

    void SyncTransformsToECS(Scene& scene);
    uint32_t CreateCloth(const SoftBodyComponent& cloth,
                         entt::entity entity);
    uint32_t CreateClothFromMesh(const std::vector<glm::vec3>& vertices,
                                 const std::vector<uint32_t>& indices,
                                 const SoftBodyComponent& cloth,
                                 const glm::vec3& position,
                                 const glm::quat& rotation,
                                 entt::entity entity);
    void UpdateSoftBodies(Scene& scene);

    // Creates a Jolt ragdoll from the entity's skeletal bones. The entity
    // must own a SkeletalComponent with a model that has a bone hierarchy.
    bool CreateRagdoll(Scene& scene, AssetManager& assets,
                       entt::entity entity);
    void DestroyRagdoll(Scene& scene, entt::entity entity);
    bool IsRagdollActive(entt::entity entity) const;
    uint32_t RagdollBodyCount(entt::entity entity) const;
    void ApplyRagdollImpulse(entt::entity entity,
                             const glm::vec3& impulse);

    // Callback receives (entityA, entityB) for each ContactAdded event.
    void SetOnContactCallback(std::function<void(entt::entity, entt::entity)> cb);

    // Trigger callbacks receive (triggerEntity, otherEntity). They fire once
    // when a sensor collider starts overlapping and once when the last
    // contact between the pair is removed. Assigning a new callback replaces
    // the previous override.
    void SetOnTriggerBeginOverlap(
        std::function<void(entt::entity, entt::entity)> cb);
    void SetOnTriggerEndOverlap(
        std::function<void(entt::entity, entt::entity)> cb);

JPH::JobSystem* GetJobSystem() const;

private:
    struct Impl;
    void SyncRagdollsToECS(Scene& scene);
    std::unique_ptr<Impl> m_impl;
};

} // namespace Galeshi

// Jolt include order matters: Jolt/Jolt.h MUST be first.
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/GroupFilterTable.h>
#include <Jolt/Physics/SoftBody/SoftBodySharedSettings.h>
#include <Jolt/Physics/SoftBody/SoftBodyCreationSettings.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>

#include "GaleshiEngine/Physics/PhysicsSystem.hpp"
#include <spdlog/spdlog.h>
#include "GaleshiEngine/Scene/Scene.hpp"
#include "GaleshiEngine/Assets/AssetManager.hpp"
#include "GaleshiEngine/Assets/Mesh.hpp"
#include <assimp/scene.h>
#include <Jolt/Physics/SoftBody/SoftBodyMotionProperties.h>
#include <Jolt/Physics/Body/BodyLockInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyLockMulti.h>
#include "GaleshiEngine/Scene/Components.hpp"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <mutex>
#include <numeric>
#include <unordered_map>
#include <unordered_set>
#include <entt/entt.hpp>
#include <functional>
#include <thread>
#include <vector>

namespace {

constexpr JPH::ObjectLayer LAYER_NON_MOVING = 0;
constexpr JPH::ObjectLayer LAYER_MOVING     = 1;

constexpr JPH::BroadPhaseLayer BP_NON_MOVING(0);
constexpr JPH::BroadPhaseLayer BP_MOVING(1);

class BPFilter final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer l) const override {
        return l == LAYER_MOVING ? BP_MOVING : BP_NON_MOVING;
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer) const override { return "BP"; }
#endif
};

class ObjVsBP final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer, JPH::BroadPhaseLayer) const override { return true; }
};

class ObjPair final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        if (a == LAYER_NON_MOVING && b == LAYER_NON_MOVING) return false;
        return true;
    }
};

glm::mat4 RagdollEntityWorldMatrix(Galeshi::Scene& scene,
                                  entt::entity handle) {
    auto& reg = scene.Registry();
    glm::mat4 world(1.0f);
    std::vector<entt::entity> chain;
    entt::entity current = handle;
    int depth = 0;
    while (reg.valid(current) && depth++ < 128) {
        chain.push_back(current);
        if (reg.all_of<Galeshi::ParentComponent>(current)) {
            current = reg.get<Galeshi::ParentComponent>(current).parent;
        } else {
            break;
        }
    }
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        if (reg.all_of<Galeshi::TransformComponent>(*it)) {
            world = world *
                reg.get<Galeshi::TransformComponent>(*it).LocalMatrix();
        }
    }
    return world;
}

std::string RagdollBoneBaseName(std::string name) {
    const size_t separator = name.find_last_of(":|");
    if (separator != std::string::npos) name.erase(0, separator + 1);
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return name;
}

bool IsRagdollBoneName(const std::string& name) {
    static constexpr const char* kSides[] = {
        "_right", "_left", ".right", ".left", "_r", "_l", ".r", ".l"
    };
    static constexpr const char* kParts[] = {
        "hips", "pelvis", "chest", "neck", "head", "clavicle",
        "shoulder", "arm", "hand", "thigh", "leg", "calf", "foot"
    };
    std::string lower = RagdollBoneBaseName(name);
    for (const char* side : kSides) {
        const size_t length = std::strlen(side);
        if (lower.size() > length &&
            lower.compare(lower.size() - length, length, side) == 0) {
            lower.erase(lower.size() - length);
            break;
        }
    }
    for (const char* part : kParts) {
        const size_t length = std::strlen(part);
        if (lower.size() >= length &&
            lower.compare(lower.size() - length, length, part) == 0) {
            return true;
        }
    }
    return lower.find("spine") != std::string::npos;
}

} // namespace

namespace Galeshi {

class ContactListenerImpl final : public JPH::ContactListener {
public:
    std::function<void(entt::entity, entt::entity)> onContact;
    std::function<void(entt::entity, entt::entity)> onTriggerBegin;
    std::function<void(entt::entity, entt::entity)> onTriggerEnd;
    std::unordered_map<uint32_t, entt::entity>* bodyToEntity = nullptr;

    struct TriggerContact {
        entt::entity trigger = entt::null;
        entt::entity other = entt::null;
        uint32_t contacts = 0;
    };
    enum class EventType { Contact, TriggerBegin, TriggerEnd };
    struct Event {
        EventType type = EventType::Contact;
        entt::entity trigger = entt::null;
        entt::entity other = entt::null;
    };
    std::mutex eventMutex;
    std::unordered_map<uint64_t, TriggerContact> triggerContacts;
    std::vector<Event> events;

    static uint64_t PairKey(uint32_t a, uint32_t b) {
        if (a > b) std::swap(a, b);
        return (static_cast<uint64_t>(a) << 32) | b;
    }

    std::vector<Event> TakeEvents() {
        std::lock_guard<std::mutex> lock(eventMutex);
        std::vector<Event> result;
        result.swap(events);
        return result;
    }

    JPH::ValidateResult OnContactValidate(const JPH::Body&, const JPH::Body&,
                                          JPH::RVec3Arg,
                                          const JPH::CollideShapeResult&) override {
        return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
    }

    void OnContactAdded(const JPH::Body& b1, const JPH::Body& b2,
                        const JPH::ContactManifold&, JPH::ContactSettings&) override {
        if (!bodyToEntity) return;
        const uint32_t id1 = b1.GetID().GetIndexAndSequenceNumber();
        const uint32_t id2 = b2.GetID().GetIndexAndSequenceNumber();
        const auto it1 = bodyToEntity->find(id1);
        const auto it2 = bodyToEntity->find(id2);
        if (it1 == bodyToEntity->end() || it2 == bodyToEntity->end()) return;

        std::lock_guard<std::mutex> lock(eventMutex);
        events.push_back({EventType::Contact, it1->second, it2->second});

        const bool sensor1 = b1.IsSensor();
        const bool sensor2 = b2.IsSensor();
        if (!sensor1 && !sensor2) return;

        TriggerContact& contact = triggerContacts[PairKey(id1, id2)];
        if (contact.contacts++ == 0) {
            contact.trigger = sensor1 ? it1->second : it2->second;
            contact.other = sensor1 ? it2->second : it1->second;
            events.push_back({EventType::TriggerBegin, contact.trigger,
                              contact.other});
        }
    }

    void OnContactPersisted(const JPH::Body&, const JPH::Body&,
                            const JPH::ContactManifold&, JPH::ContactSettings&) override {}

    void OnContactRemoved(const JPH::SubShapeIDPair& pair) override {
        const uint32_t id1 =
            pair.GetBody1ID().GetIndexAndSequenceNumber();
        const uint32_t id2 =
            pair.GetBody2ID().GetIndexAndSequenceNumber();
        std::lock_guard<std::mutex> lock(eventMutex);
        const auto it = triggerContacts.find(PairKey(id1, id2));
        if (it == triggerContacts.end()) return;
        if (it->second.contacts > 0) --it->second.contacts;
        if (it->second.contacts > 0) return;
        events.push_back({EventType::TriggerEnd, it->second.trigger,
                          it->second.other});
        triggerContacts.erase(it);
    }
};

struct EnginePhysicsSystem::Impl {
    struct RagdollPart {
        uint32_t bodyId = 0xFFFFFFFFu;
        int boneIndex = -1;
        glm::mat4 inverseBind{1.0f};
    };

    struct RagdollFollow {
        int boneIndex = -1;
        size_t parentPart = 0;
        glm::mat4 localBind{1.0f};
        glm::mat4 inverseBind{1.0f};
    };

    struct RagdollRuntime {
        std::vector<RagdollPart> parts;
        std::vector<RagdollFollow> followers;
        std::vector<JPH::Constraint*> constraints;
    };

    std::unique_ptr<JPH::TempAllocatorImpl>   tempAllocator;
    std::unique_ptr<JPH::JobSystemThreadPool> jobSystem;
    std::unique_ptr<JPH::PhysicsSystem>       physicsSystem;

    BPFilter  bpFilter;
    ObjVsBP   objVsBp;
    ObjPair   objPair;

    JPH::BodyInterface* bodyInterface = nullptr;
    ContactListenerImpl contactListener;
    std::unordered_map<uint32_t, entt::entity> bodyToEntity;
    std::unordered_map<entt::entity, RagdollRuntime> ragdolls;
    float accumulator = 0.0f;
    bool initialized = false;
};

EnginePhysicsSystem::EnginePhysicsSystem() : m_impl(std::make_unique<Impl>()) {}
EnginePhysicsSystem::~EnginePhysicsSystem() { Shutdown(); }

bool EnginePhysicsSystem::Init() {
    if (m_impl->initialized) return true;

    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();



    constexpr JPH::uint kMaxBodies            = 65536;
    constexpr JPH::uint kMaxBodyPairs         = 65536;
    constexpr JPH::uint kMaxContactConstraints= 16384;

    m_impl->physicsSystem = std::make_unique<JPH::PhysicsSystem>();
    m_impl->physicsSystem->Init(
        kMaxBodies, 0, kMaxBodyPairs, kMaxContactConstraints,
        m_impl->bpFilter, m_impl->objVsBp, m_impl->objPair);

    m_impl->tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(32 * 1024 * 1024);

    const int hw = static_cast<int>(std::thread::hardware_concurrency());
    const int threads = std::max(1, hw - 1);
    m_impl->jobSystem = std::make_unique<JPH::JobSystemThreadPool>(2048, 8, threads);

    m_impl->bodyInterface = &m_impl->physicsSystem->GetBodyInterface();
    m_impl->contactListener.bodyToEntity = &m_impl->bodyToEntity;
    m_impl->physicsSystem->SetContactListener(&m_impl->contactListener);

    // Static ground plane: top surface at y=0.
    JPH::BodyCreationSettings ground(
        new JPH::BoxShape(JPH::Vec3(50.0f, 0.5f, 50.0f)),
        JPH::RVec3(0.0f, -0.5f, 0.0f),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Static,
        LAYER_NON_MOVING);
    m_impl->bodyInterface->CreateAndAddBody(ground, JPH::EActivation::DontActivate);

    m_impl->initialized = true;
    return true;
}
JPH::JobSystem* EnginePhysicsSystem::GetJobSystem() const {
    return m_impl->jobSystem.get();
}


void EnginePhysicsSystem::Shutdown() {
    if (!m_impl || !m_impl->initialized) return;
    m_impl->bodyInterface = nullptr;
    m_impl->physicsSystem.reset();
    m_impl->jobSystem.reset();
    m_impl->tempAllocator.reset();
    m_impl->bodyToEntity.clear();
    m_impl->ragdolls.clear();
    m_impl->accumulator = 0.0f;
    {
        std::lock_guard<std::mutex> lock(
            m_impl->contactListener.eventMutex);
        m_impl->contactListener.triggerContacts.clear();
        m_impl->contactListener.events.clear();
    }
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
    m_impl->initialized = false;
}

void EnginePhysicsSystem::Reset() {
    Shutdown();
    Init();
}

void EnginePhysicsSystem::Update(Scene& scene, float dt) {
    if (!m_impl->initialized) return;

    // Fixed simulation step. The first frame after a scene load can carry a
    // multi-second delta while assets are decoded; stepping that directly
    // lets a falling body tunnel through the ground in one update. Clamp and
    // substep instead, and drop the backlog if the frame is still too slow.
    constexpr float kFixedStep = 1.0f / 60.0f;
    constexpr int kMaxSteps = 4;
    m_impl->accumulator += std::clamp(dt, 0.0f, 0.25f);
    int steps = 0;
    while (m_impl->accumulator >= kFixedStep && steps < kMaxSteps) {
        m_impl->physicsSystem->Update(kFixedStep, 1,
            m_impl->tempAllocator.get(), m_impl->jobSystem.get());
        m_impl->accumulator -= kFixedStep;
        ++steps;
    }
    if (steps == kMaxSteps) m_impl->accumulator = 0.0f;
    SyncRagdollsToECS(scene);

    // Jolt invokes contact callbacks from worker threads. Deliver queued
    // events on the caller's thread so script callbacks can touch the scene.
    for (const auto& event : m_impl->contactListener.TakeEvents()) {
        switch (event.type) {
        case ContactListenerImpl::EventType::Contact:
            if (m_impl->contactListener.onContact) {
                m_impl->contactListener.onContact(event.trigger, event.other);
            }
            break;
        case ContactListenerImpl::EventType::TriggerBegin:
            if (m_impl->contactListener.onTriggerBegin) {
                m_impl->contactListener.onTriggerBegin(event.trigger,
                                                       event.other);
            }
            break;
        case ContactListenerImpl::EventType::TriggerEnd:
            if (m_impl->contactListener.onTriggerEnd) {
                m_impl->contactListener.onTriggerEnd(event.trigger,
                                                     event.other);
            }
            break;
        }
    }
}

uint32_t EnginePhysicsSystem::CreateBox(const glm::vec3& halfExtents, const glm::vec3& position, entt::entity entity, bool isStatic) {
    if (!m_impl->initialized) return 0xFFFFFFFFu;

    JPH::EMotionType motion = isStatic ? JPH::EMotionType::Static : JPH::EMotionType::Dynamic;
    JPH::ObjectLayer layer = isStatic ? LAYER_NON_MOVING : LAYER_MOVING;

    JPH::BodyCreationSettings s(
        new JPH::BoxShape(JPH::Vec3(halfExtents.x, halfExtents.y, halfExtents.z)),
        JPH::RVec3(position.x, position.y, position.z),
        JPH::Quat::sIdentity(),
        motion,
        layer);
    s.mAllowSleeping = true;
    s.mFriction = 0.5f;
    s.mRestitution = 0.0f;

    JPH::BodyID id = m_impl->bodyInterface->CreateAndAddBody(s, JPH::EActivation::Activate);
    if (id.IsInvalid()) return 0xFFFFFFFFu;
    uint32_t rawId = id.GetIndexAndSequenceNumber();
    if (entity != entt::null) m_impl->bodyToEntity[rawId] = entity;
    return rawId;
}


uint32_t EnginePhysicsSystem::CreateSphere(float radius, const glm::vec3& position, entt::entity entity, bool isStatic) {
    if (!m_impl->initialized) return 0xFFFFFFFFu;
    JPH::EMotionType motion = isStatic ? JPH::EMotionType::Static : JPH::EMotionType::Dynamic;
    JPH::ObjectLayer layer = isStatic ? LAYER_NON_MOVING : LAYER_MOVING;
    JPH::BodyCreationSettings s(
        new JPH::SphereShape(radius),
        JPH::RVec3(position.x, position.y, position.z),
        JPH::Quat::sIdentity(),
        motion, layer);
    JPH::BodyID id = m_impl->bodyInterface->CreateAndAddBody(s, JPH::EActivation::Activate);
    if (id.IsInvalid()) return 0xFFFFFFFFu;
    uint32_t rawId = id.GetIndexAndSequenceNumber();
    if (entity != entt::null) m_impl->bodyToEntity[rawId] = entity;
    return rawId;
}

uint32_t EnginePhysicsSystem::CreateCapsule(float halfHeight, float radius, const glm::vec3& position, entt::entity entity, bool isStatic) {
    if (!m_impl->initialized) return 0xFFFFFFFFu;
    JPH::EMotionType motion = isStatic ? JPH::EMotionType::Static : JPH::EMotionType::Dynamic;
    JPH::ObjectLayer layer = isStatic ? LAYER_NON_MOVING : LAYER_MOVING;
    JPH::BodyCreationSettings s(
        new JPH::CapsuleShape(halfHeight, radius),
        JPH::RVec3(position.x, position.y, position.z),
        JPH::Quat::sIdentity(),
        motion, layer);
    // Capsules standing on their rounded end are unstable; keep legacy
    // dynamic capsules upright unless a collider/rigid-body pair is used.
    if (motion == JPH::EMotionType::Dynamic) {
        s.mAllowedDOFs =
            JPH::EAllowedDOFs::TranslationX |
            JPH::EAllowedDOFs::TranslationY |
            JPH::EAllowedDOFs::TranslationZ;
    }
    JPH::BodyID id = m_impl->bodyInterface->CreateAndAddBody(s, JPH::EActivation::Activate);
    if (id.IsInvalid()) return 0xFFFFFFFFu;
    uint32_t rawId = id.GetIndexAndSequenceNumber();
    if (entity != entt::null) m_impl->bodyToEntity[rawId] = entity;
    return rawId;
}

uint32_t EnginePhysicsSystem::CreateCollider(const ColliderComponent& collider,
                                             const RigidBodyComponent& body,
                                             const glm::vec3& position,
                                             const glm::quat& rotation,
                                             entt::entity entity) {
    if (!m_impl->initialized) return 0xFFFFFFFFu;

    JPH::EMotionType motion = JPH::EMotionType::Dynamic;
    JPH::ObjectLayer layer = LAYER_MOVING;
    switch (body.motionType) {
        case RigidBodyComponent::MotionType::Static:
            motion = JPH::EMotionType::Static;
            layer = LAYER_NON_MOVING;
            break;
        case RigidBodyComponent::MotionType::Kinematic:
            motion = JPH::EMotionType::Kinematic;
            break;
        case RigidBodyComponent::MotionType::Dynamic:
        default:
            break;
    }

    JPH::Shape* shape = nullptr;
    switch (collider.shapeType) {
        case ColliderShapeType::Sphere:
            shape = new JPH::SphereShape(std::max(collider.radius, 0.001f));
            break;
        case ColliderShapeType::Capsule:
            shape = new JPH::CapsuleShape(std::max(collider.halfHeight, 0.001f),
                                          std::max(collider.radius, 0.001f));
            break;
        case ColliderShapeType::Box:
        default:
            shape = new JPH::BoxShape(JPH::Vec3(
                std::max(collider.halfExtents.x, 0.001f),
                std::max(collider.halfExtents.y, 0.001f),
                std::max(collider.halfExtents.z, 0.001f)));
            break;
    }

    // Jolt quaternions are XYZW, glm uses WXYZ.
    const JPH::Quat joltRot(rotation.x, rotation.y, rotation.z, rotation.w);

    // Keep the body origin at the entity origin: bake the collider offset into
    // a decorated shape so transform synchronization stays consistent.
    if (collider.offset != glm::vec3(0.0f)) {
        shape = new JPH::RotatedTranslatedShape(
            JPH::Vec3(collider.offset.x, collider.offset.y, collider.offset.z),
            JPH::Quat::sIdentity(), shape);
    }

    JPH::BodyCreationSettings settings(
        shape,
        JPH::RVec3(position.x, position.y, position.z),
        joltRot, motion, layer);
    settings.mFriction = std::max(collider.friction, 0.0f);
    settings.mRestitution = std::clamp(collider.restitution, 0.0f, 1.0f);
    settings.mIsSensor = collider.isTrigger;
    settings.mAllowSleeping = true;
    settings.mGravityFactor = body.useGravity ? 1.0f : 0.0f;

    if (motion == JPH::EMotionType::Dynamic && body.lockRotation) {
        settings.mAllowedDOFs =
            JPH::EAllowedDOFs::TranslationX |
            JPH::EAllowedDOFs::TranslationY |
            JPH::EAllowedDOFs::TranslationZ;
    }

    if (motion != JPH::EMotionType::Static && body.mass > 0.0f) {
        JPH::MassProperties massProperties = shape->GetMassProperties();
        massProperties.ScaleToMass(body.mass);
        settings.mOverrideMassProperties =
            JPH::EOverrideMassProperties::MassAndInertiaProvided;
        settings.mMassPropertiesOverride = massProperties;
    }

    JPH::BodyID id = m_impl->bodyInterface->CreateAndAddBody(
        settings, JPH::EActivation::Activate);
    if (id.IsInvalid()) return 0xFFFFFFFFu;
    const uint32_t rawId = id.GetIndexAndSequenceNumber();
    if (entity != entt::null) m_impl->bodyToEntity[rawId] = entity;
    return rawId;
}

void EnginePhysicsSystem::DestroyBody(uint32_t bodyId) {
    if (!m_impl->initialized || bodyId == 0xFFFFFFFFu) return;
    JPH::BodyID id(bodyId);
    if (m_impl->bodyInterface->IsAdded(id)) {
        m_impl->bodyInterface->RemoveBody(id);
    }
    m_impl->bodyInterface->DestroyBody(id);
    m_impl->bodyToEntity.erase(bodyId);
}

bool EnginePhysicsSystem::SphereCast(
    const glm::vec3& origin, const glm::vec3& target, float radius,
    const std::function<bool(entt::entity)>& filter,
    glm::vec3& hitPoint) const {
    if (!m_impl->initialized || !filter) return false;

    const glm::vec3 delta = target - origin;
    if (glm::dot(delta, delta) < 1e-8f) return false;

    class EntityFilter final : public JPH::BodyFilter {
    public:
        EntityFilter(
            const std::unordered_map<uint32_t, entt::entity>& entities,
            const std::function<bool(entt::entity)>& accept)
            : mEntities(entities), mAccept(accept) {}

        bool ShouldCollide(const JPH::BodyID& body) const override {
            const auto it = mEntities.find(
                body.GetIndexAndSequenceNumber());
            return it != mEntities.end() && mAccept(it->second);
        }

    private:
        const std::unordered_map<uint32_t, entt::entity>& mEntities;
        const std::function<bool(entt::entity)>& mAccept;
    } entityFilter(m_impl->bodyToEntity, filter);

    JPH::SphereShape sphere(std::max(radius, 0.01f));
    JPH::RShapeCast cast(
        &sphere, JPH::Vec3::sReplicate(1.0f),
        JPH::RMat44::sTranslation(
            JPH::RVec3(origin.x, origin.y, origin.z)),
        JPH::Vec3(delta.x, delta.y, delta.z));

    JPH::ShapeCastSettings settings;
    settings.mBackFaceModeTriangles =
        JPH::EBackFaceMode::CollideWithBackFaces;
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
    m_impl->physicsSystem->GetNarrowPhaseQuery().CastShape(
        cast, settings, JPH::RVec3::sZero(), collector, {}, {},
        entityFilter);
    if (!collector.HadHit()) return false;

    const float fraction =
        glm::clamp(collector.mHit.mFraction, 0.0f, 1.0f);
    hitPoint = origin + delta * fraction;
    return true;
}


uint32_t EnginePhysicsSystem::CreateCloth(
    const SoftBodyComponent& cloth, entt::entity entity) {
    if (!m_impl->initialized) return 0xFFFFFFFFu;
    const int gridX = std::max(cloth.gridX, 2);
    const int gridY = std::max(cloth.gridY, 2);
    const float spacing = cloth.spacing;
    const glm::vec3 origin = cloth.origin;

    // Jolt 5.2 soft-body API: shared settings, faces, then constraints.
    JPH::Ref<JPH::SoftBodySharedSettings> settings = new JPH::SoftBodySharedSettings;

    for (int y = 0; y < gridY; ++y) {
        for (int x = 0; x < gridX; ++x) {
            JPH::SoftBodySharedSettings::Vertex v;
            // Keep grid vertices relative to the body origin. The render mesh
            // is authored as origin + (x, y) * spacing, so UpdateSoftBodies
            // can add the body position without double-offsetting the cloth.
            JPH::Vec3 pos(
                x * spacing,
                0.0f,
                y * spacing
            );
            pos.StoreFloat3(&v.mPosition);
            // Pin corners with invMass = 0; the rest stay dynamic.
            bool isCorner = (x == 0 || x == gridX - 1) && (y == 0 || y == gridY - 1);
            v.mInvMass = isCorner ? 0.0f : 1.0f;
            settings->mVertices.push_back(v);
        }
    }

    auto vidx = [gridX](int x, int y) { return x + y * gridX; };
    for (int y = 0; y < gridY - 1; ++y) {
        for (int x = 0; x < gridX - 1; ++x) {
            JPH::SoftBodySharedSettings::Face f;
            f.mVertex[0] = vidx(x, y);
            f.mVertex[1] = vidx(x, y + 1);
            f.mVertex[2] = vidx(x + 1, y + 1);
            settings->AddFace(f);
            f.mVertex[1] = vidx(x + 1, y + 1);
            f.mVertex[2] = vidx(x + 1, y);
            settings->AddFace(f);
        }
    }

    const float stiffness = std::clamp(cloth.stiffness, 0.01f, 1.0f);
    JPH::SoftBodySharedSettings::VertexAttributes attr(
        std::max(cloth.stretchCompliance / stiffness, 1.0e-6f),
        std::max(cloth.shearCompliance / stiffness, 1.0e-6f),
        std::max(cloth.bendCompliance / stiffness, 1.0e-6f));
    settings->CreateConstraints(&attr, 1, JPH::SoftBodySharedSettings::EBendType::Distance);
    settings->Optimize();

    JPH::SoftBodyCreationSettings sbSettings(
        settings,
        JPH::RVec3(origin.x, origin.y, origin.z),
        JPH::Quat::sIdentity(),
        LAYER_MOVING);
    sbSettings.mFriction = cloth.friction;
    sbSettings.mRestitution = cloth.restitution;
    sbSettings.mGravityFactor = cloth.gravityFactor;
    sbSettings.mLinearDamping = cloth.linearDamping;
    sbSettings.mPressure = cloth.pressure;
    sbSettings.mUpdatePosition = true;

    JPH::BodyID id = m_impl->bodyInterface->CreateAndAddSoftBody(sbSettings, JPH::EActivation::Activate);
    if (id.IsInvalid()) {
        spdlog::error("[Physics] CreateCloth failed");
        return 0xFFFFFFFFu;
    }
    uint32_t rawId = id.GetIndexAndSequenceNumber();
    if (entity != entt::null) m_impl->bodyToEntity[rawId] = entity;
    spdlog::info("[Physics] Cloth created: {}x{} grid, bodyId=0x{:08x}", gridX, gridY, rawId);
    return rawId;
}


uint32_t EnginePhysicsSystem::CreateClothFromMesh(
    const std::vector<glm::vec3>& vertices,
    const std::vector<uint32_t>& indices,
    const SoftBodyComponent& cloth,
    const glm::vec3& position,
    const glm::quat& rotation,
    entt::entity entity) {
    if (!m_impl->initialized || vertices.empty() || indices.size() < 3) {
        return 0xFFFFFFFFu;
    }

    JPH::Ref<JPH::SoftBodySharedSettings> settings =
        new JPH::SoftBodySharedSettings;

    bool hasDynamicVertex = false;
    for (size_t i = 0; i < vertices.size(); ++i) {
        const float weight = i < cloth.paintWeights.size()
            ? std::clamp(cloth.paintWeights[i], 0.0f, 1.0f)
            : 0.0f;
        JPH::SoftBodySharedSettings::Vertex vertex;
        JPH::Vec3 position(vertices[i].x, vertices[i].y, vertices[i].z);
        position.StoreFloat3(&vertex.mPosition);
        vertex.mInvMass = weight > 1.0e-4f
            ? weight / std::max(cloth.mass, 1.0e-3f)
            : 0.0f;
        hasDynamicVertex = hasDynamicVertex || vertex.mInvMass > 0.0f;
        settings->mVertices.push_back(vertex);
    }
    if (!hasDynamicVertex) {
        spdlog::warn("[Physics] Cloth has no painted vertices");
        return 0xFFFFFFFFu;
    }

    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const uint32_t i0 = indices[i];
        const uint32_t i1 = indices[i + 1];
        const uint32_t i2 = indices[i + 2];
        if (i0 >= vertices.size() || i1 >= vertices.size() ||
            i2 >= vertices.size() || i0 == i1 || i1 == i2 || i0 == i2) {
            continue;
        }
        const float w0 = i0 < cloth.paintWeights.size()
            ? cloth.paintWeights[i0] : 0.0f;
        const float w1 = i1 < cloth.paintWeights.size()
            ? cloth.paintWeights[i1] : 0.0f;
        const float w2 = i2 < cloth.paintWeights.size()
            ? cloth.paintWeights[i2] : 0.0f;
        if (w0 <= 1.0e-4f && w1 <= 1.0e-4f && w2 <= 1.0e-4f) continue;

        JPH::SoftBodySharedSettings::Face face;
        face.mVertex[0] = i0;
        face.mVertex[1] = i1;
        face.mVertex[2] = i2;
        settings->AddFace(face);
    }
    if (settings->mFaces.empty()) {
        spdlog::warn("[Physics] Cloth has no painted faces");
        return 0xFFFFFFFFu;
    }

    const float stiffness = std::clamp(cloth.stiffness, 0.01f, 1.0f);
    JPH::SoftBodySharedSettings::VertexAttributes attributes(
        std::max(cloth.stretchCompliance / stiffness, 1.0e-6f),
        std::max(cloth.shearCompliance / stiffness, 1.0e-6f),
        std::max(cloth.bendCompliance / stiffness, 1.0e-6f));
    settings->CreateConstraints(&attributes, 1,
                                JPH::SoftBodySharedSettings::EBendType::Distance);
    settings->Optimize();

    JPH::SoftBodyCreationSettings bodySettings(
        settings,
        JPH::RVec3(position.x, position.y, position.z),
        JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w),
        LAYER_MOVING);
    bodySettings.mFriction = cloth.friction;
    bodySettings.mRestitution = cloth.restitution;
    bodySettings.mGravityFactor = cloth.gravityFactor;
    bodySettings.mLinearDamping = cloth.linearDamping;
    bodySettings.mPressure = cloth.pressure;
    bodySettings.mUpdatePosition = true;
    bodySettings.mMakeRotationIdentity = false;
    bodySettings.mAllowSleeping = false;

    JPH::BodyID id = m_impl->bodyInterface->CreateAndAddSoftBody(
        bodySettings, JPH::EActivation::Activate);
    if (id.IsInvalid()) {
        spdlog::error("[Physics] CreateClothFromMesh failed");
        return 0xFFFFFFFFu;
    }
    const uint32_t rawId = id.GetIndexAndSequenceNumber();
    if (entity != entt::null) m_impl->bodyToEntity[rawId] = entity;
    spdlog::info("[Physics] Cloth from mesh: {} vertices, {} faces, "
                 "bodyId=0x{:08x}", vertices.size(),
                 settings->mFaces.size(), rawId);
    return rawId;
}

void EnginePhysicsSystem::UpdateSoftBodies(Scene& scene) {
    auto view = scene.Registry().view<SoftBodyComponent, MeshComponent>();
    for (auto e : view) {
        auto& sb = view.get<SoftBodyComponent>(e);
        auto& mc = view.get<MeshComponent>(e);
        if (sb.softBodyId == 0xFFFFFFFF || !mc.mesh) continue;

        JPH::BodyID id(sb.softBodyId);
        const JPH::BodyLockInterface& lockInterface = m_impl->physicsSystem->GetBodyLockInterface();
        JPH::BodyLockRead lock(lockInterface, id);
        if (!lock.Succeeded()) continue;
        const JPH::Body& body = lock.GetBody();
        const auto* mp = static_cast<const JPH::SoftBodyMotionProperties*>(body.GetMotionProperties());
        if (!mp) continue;

        const auto& joltVertices = mp->GetVertices();
        if (sb.meshCloth &&
            sb.simVertexMap.size() != mc.mesh->Vertices().size()) {
            continue;
        }
        if (!sb.meshCloth &&
            joltVertices.size() != mc.mesh->Vertices().size()) {
            continue;
        }
        std::vector<glm::vec3> positions(mc.mesh->Vertices().size());
        if (sb.meshCloth) {
            for (size_t i = 0; i < positions.size(); ++i) {
                const uint32_t simIndex = sb.simVertexMap[i];
                if (simIndex >= joltVertices.size()) continue;
                const JPH::Vec3& pos = joltVertices[simIndex].mPosition;
                positions[i] = glm::vec3(pos.GetX(), pos.GetY(), pos.GetZ());
            }
        } else {
            JPH::RVec3 bodyPos = body.GetPosition();
            JPH::Quat bodyRot = body.GetRotation();
            for (size_t i = 0; i < joltVertices.size(); ++i) {
                JPH::RVec3 pos = bodyPos + bodyRot * joltVertices[i].mPosition;
                positions[i] = glm::vec3(pos.GetX(), pos.GetY(), pos.GetZ());
            }
        }
        mc.mesh->UpdateVertices(positions);
    }
}

bool EnginePhysicsSystem::CreateRagdoll(Scene& scene, AssetManager& assets,
                                        entt::entity entity) {
    if (!m_impl->initialized || entity == entt::null) return false;
    auto& reg = scene.Registry();
    if (!reg.valid(entity) ||
        !reg.all_of<SkeletalComponent, TransformComponent>(entity)) {
        return false;
    }
    auto& skeleton = reg.get<SkeletalComponent>(entity);
    if (skeleton.sourcePath.empty()) return false;
    auto model = assets.LoadModel(skeleton.sourcePath);
    if (!model.rawScene || !model.rawScene->mRootNode ||
        model.boneMap.empty()) {
        spdlog::warn("[Physics] Ragdoll requires a skeletal model hierarchy: {}",
                     skeleton.sourcePath);
        return false;
    }

    DestroyRagdoll(scene, entity);

    if (skeleton.boneMatrices.size() != 256) {
        skeleton.boneMatrices.assign(256, glm::mat4(1.0f));
    }

    std::unordered_set<std::string> boneNames;
    boneNames.reserve(model.boneMap.size());
    for (const auto& [name, index] : model.boneMap) {
        if (index >= 0 && index < 255) boneNames.insert(name);
    }
    if (boneNames.empty()) return false;

    std::unordered_map<std::string, std::string> parentBones;
    std::function<void(const aiNode*, const std::string&)> walk =
        [&](const aiNode* node, const std::string& nearestParent) {
            if (!node) return;
            std::string nextParent = nearestParent;
            const std::string name = node->mName.C_Str();
            if (boneNames.count(name)) {
                parentBones[name] = nearestParent;
                nextParent = name;
            }
            for (unsigned i = 0; i < node->mNumChildren; ++i) {
                walk(node->mChildren[i], nextParent);
            }
        };
    walk(model.rawScene->mRootNode, {});

    const glm::mat4 entityWorld = RagdollEntityWorldMatrix(scene, entity);
    struct BoneBuild {
        std::string name;
        int index = -1;
        glm::mat4 inverseBind{1.0f};
        glm::vec3 worldPosition{0.0f};
        glm::quat worldRotation{1.0f, 0.0f, 0.0f, 0.0f};
    };
    std::vector<BoneBuild> bones;
    bones.reserve(boneNames.size());
    std::unordered_map<std::string, size_t> boneLookup;

    for (const auto& [name, index] : model.boneMap) {
        if (index < 0 || index >= 255) continue;
        const auto offset = model.boneOffsetMatrices.find(name);
        if (offset == model.boneOffsetMatrices.end()) continue;
        const glm::mat4 bindWorld = glm::inverse(offset->second);
        const glm::mat4 modelWorld =
            skeleton.boneMatrices[static_cast<size_t>(index)] * bindWorld;
        const glm::mat4 world = entityWorld * modelWorld;
        BoneBuild bone;
        bone.name = name;
        bone.index = index;
        bone.inverseBind = offset->second;
        bone.worldPosition = glm::vec3(world[3]);
        bone.worldRotation = glm::normalize(glm::quat_cast(glm::mat3(world)));
        boneLookup.emplace(name, bones.size());
        bones.push_back(std::move(bone));
    }
    if (bones.empty()) return false;

    // shortcut: a name-based core-bone set and point joints keep arbitrary
    // rigs working; upgrade to authored RagdollSettings with swing/twist limits
    // when non-humanoid rigs or self-collision are required.
    std::vector<size_t> selected;
    selected.reserve(bones.size());
    for (size_t i = 0; i < bones.size(); ++i) {
        if (IsRagdollBoneName(bones[i].name)) selected.push_back(i);
    }
    if (selected.size() < 4) {
        selected.resize(bones.size());
        std::iota(selected.begin(), selected.end(), size_t{0});
    }

    std::unordered_map<std::string, size_t> selectedLookup;
    std::unordered_set<std::string> selectedNames;
    for (size_t i = 0; i < selected.size(); ++i) {
        selectedLookup.emplace(bones[selected[i]].name, i);
        selectedNames.insert(bones[selected[i]].name);
    }

    std::unordered_map<std::string, std::string> selectedParents;
    for (size_t i = 0; i < selected.size(); ++i) {
        std::string parent = parentBones[bones[selected[i]].name];
        while (!parent.empty() && selectedNames.count(parent) == 0) {
            const auto next = parentBones.find(parent);
            parent = next == parentBones.end() ? std::string{} : next->second;
        }
        if (!parent.empty()) {
            selectedParents.emplace(bones[selected[i]].name, parent);
        }
    }

    m_impl->ragdolls[entity] = Impl::RagdollRuntime{};
    auto& runtime = m_impl->ragdolls[entity];
    runtime.parts.reserve(selected.size());

    auto cleanup = [&]() {
        for (JPH::Constraint* constraint : runtime.constraints) {
            m_impl->physicsSystem->RemoveConstraint(constraint);
        }
        runtime.constraints.clear();
        for (const auto& part : runtime.parts) {
            DestroyBody(part.bodyId);
        }
        runtime.parts.clear();
        m_impl->ragdolls.erase(entity);
    };

    std::vector<float> nearestDistance(selected.size(), 0.0f);
    std::vector<bool> hasChild(selected.size(), false);
    for (const auto& [childName, parentName] : selectedParents) {
        const auto child = selectedLookup.find(childName);
        const auto parent = selectedLookup.find(parentName);
        if (child == selectedLookup.end() || parent == selectedLookup.end()) {
            continue;
        }
        const float distance = glm::distance(
            bones[selected[parent->second]].worldPosition,
            bones[selected[child->second]].worldPosition);
        if (!hasChild[parent->second] ||
            distance < nearestDistance[parent->second]) {
            nearestDistance[parent->second] = distance;
            hasChild[parent->second] = true;
        }
    }

    JPH::Ref<JPH::GroupFilterTable> selfCollision =
        new JPH::GroupFilterTable(static_cast<JPH::uint>(selected.size()));
    for (size_t i = 0; i < selected.size(); ++i) {
        for (size_t j = i + 1; j < selected.size(); ++j) {
            selfCollision->DisableCollision(
                static_cast<JPH::CollisionGroup::SubGroupID>(i),
                static_cast<JPH::CollisionGroup::SubGroupID>(j));
        }
    }
    const JPH::CollisionGroup::GroupID collisionGroup =
        static_cast<JPH::CollisionGroup::GroupID>(entity) + 1u;

    for (size_t i = 0; i < selected.size(); ++i) {
        const BoneBuild& bone = bones[selected[i]];
        const float radius = glm::clamp(
            hasChild[i] ? nearestDistance[i] * 0.16f : 0.045f, 0.02f, 0.14f);
        JPH::SphereShape* shape = new JPH::SphereShape(radius);
        JPH::BodyCreationSettings settings(
            shape, JPH::RVec3(bone.worldPosition.x, bone.worldPosition.y,
                              bone.worldPosition.z),
            JPH::Quat(bone.worldRotation.x, bone.worldRotation.y,
                      bone.worldRotation.z, bone.worldRotation.w),
            JPH::EMotionType::Dynamic, LAYER_MOVING);
        settings.mCollisionGroup = JPH::CollisionGroup(
            selfCollision,
            collisionGroup,
            static_cast<JPH::CollisionGroup::SubGroupID>(runtime.parts.size()));
        settings.mAllowSleeping = true;
        settings.mFriction = 0.6f;
        settings.mRestitution = 0.0f;
        settings.mLinearDamping = 0.25f;
        settings.mAngularDamping = 0.45f;
        settings.mGravityFactor = 1.0f;
        JPH::MassProperties massProperties = shape->GetMassProperties();
        massProperties.ScaleToMass(0.4f);
        settings.mOverrideMassProperties =
            JPH::EOverrideMassProperties::MassAndInertiaProvided;
        settings.mMassPropertiesOverride = massProperties;
        const JPH::BodyID bodyId = m_impl->bodyInterface->CreateAndAddBody(
            settings, JPH::EActivation::Activate);
        if (bodyId.IsInvalid()) {
            cleanup();
            return false;
        }
        const uint32_t rawId = bodyId.GetIndexAndSequenceNumber();
        m_impl->bodyToEntity[rawId] = entity;
        runtime.parts.push_back({rawId, bone.index, bone.inverseBind});
    }

    const JPH::BodyLockInterface& lockInterface =
        m_impl->physicsSystem->GetBodyLockInterface();
    for (const auto& [childName, parentName] : selectedParents) {
        const auto child = selectedLookup.find(childName);
        const auto parent = selectedLookup.find(parentName);
        if (child == selectedLookup.end() || parent == selectedLookup.end()) {
            continue;
        }
        const auto& childPart = runtime.parts[child->second];
        const auto& parentPart = runtime.parts[parent->second];
        JPH::PointConstraintSettings settings;
        settings.mSpace = JPH::EConstraintSpace::WorldSpace;
        const glm::vec3 point =
            bones[selected[child->second]].worldPosition;
        settings.mPoint1 = settings.mPoint2 =
            JPH::RVec3(point.x, point.y, point.z);

        const JPH::BodyID bodies[] = {
            JPH::BodyID(parentPart.bodyId), JPH::BodyID(childPart.bodyId)};
        JPH::BodyLockMultiWrite lock(lockInterface, bodies, 2);
        JPH::Body* parentBody = lock.GetBody(0);
        JPH::Body* childBody = lock.GetBody(1);
        if (!parentBody || !childBody) continue;
        JPH::TwoBodyConstraint* constraint =
            settings.Create(*parentBody, *childBody);
        if (constraint) {
            m_impl->physicsSystem->AddConstraint(constraint);
            runtime.constraints.push_back(constraint);
        }
    }

    // Bones outside the simulated core (fingers, face, hair, toes) follow the
    // nearest simulated ancestor rigidly instead of staying in the old pose.
    for (const BoneBuild& bone : bones) {
        if (selectedNames.count(bone.name)) continue;
        std::string ancestor = parentBones[bone.name];
        while (!ancestor.empty() && selectedNames.count(ancestor) == 0) {
            const auto next = parentBones.find(ancestor);
            ancestor = next == parentBones.end() ? std::string{} : next->second;
        }
        if (ancestor.empty()) continue;
        const auto parent = selectedLookup.find(ancestor);
        const auto child = boneLookup.find(bone.name);
        if (parent == selectedLookup.end() || child == boneLookup.end()) {
            continue;
        }
        if (parent->second >= runtime.parts.size()) continue;
        const glm::mat4 parentBind = bones[boneLookup[ancestor]].inverseBind;
        runtime.followers.push_back({
            bone.index,
            parent->second,
            parentBind * glm::inverse(bones[child->second].inverseBind),
            bone.inverseBind});
    }

    reg.emplace_or_replace<RagdollComponent>(entity);
    skeleton.isAnimated = true;
    spdlog::info("[Physics] Ragdoll created for {} bones on entity {}",
                 runtime.parts.size(), static_cast<uint32_t>(entity));
    return !runtime.parts.empty();
}

void EnginePhysicsSystem::DestroyRagdoll(Scene& scene, entt::entity entity) {
    auto it = m_impl->ragdolls.find(entity);
    if (it == m_impl->ragdolls.end()) {
        if (scene.Registry().valid(entity)) {
            scene.Registry().remove<RagdollComponent>(entity);
        }
        return;
    }
    // ConstraintManager holds a Ref<Constraint>; removing releases the last
    // reference and deletes the constraint.
    for (JPH::Constraint* constraint : it->second.constraints) {
        m_impl->physicsSystem->RemoveConstraint(constraint);
    }
    for (const auto& part : it->second.parts) {
        DestroyBody(part.bodyId);
    }
    m_impl->ragdolls.erase(it);
    if (scene.Registry().valid(entity)) {
        scene.Registry().remove<RagdollComponent>(entity);
    }
}

bool EnginePhysicsSystem::IsRagdollActive(entt::entity entity) const {
    return m_impl->ragdolls.find(entity) != m_impl->ragdolls.end();
}

uint32_t EnginePhysicsSystem::RagdollBodyCount(entt::entity entity) const {
    const auto it = m_impl->ragdolls.find(entity);
    return it == m_impl->ragdolls.end()
        ? 0u
        : static_cast<uint32_t>(it->second.parts.size());
}

void EnginePhysicsSystem::ApplyRagdollImpulse(entt::entity entity,
                                              const glm::vec3& impulse) {
    const auto it = m_impl->ragdolls.find(entity);
    if (it == m_impl->ragdolls.end()) return;
    for (const auto& part : it->second.parts) {
        AddImpulse(part.bodyId, impulse);
    }
}

void EnginePhysicsSystem::SyncRagdollsToECS(Scene& scene) {
    if (!m_impl->initialized) return;
    auto& reg = scene.Registry();
    for (auto& [entity, runtime] : m_impl->ragdolls) {
        if (!reg.valid(entity) ||
            !reg.all_of<SkeletalComponent>(entity)) {
            continue;
        }
        auto& skeleton = reg.get<SkeletalComponent>(entity);
        if (skeleton.boneMatrices.size() != 256) {
            skeleton.boneMatrices.assign(256, glm::mat4(1.0f));
        }
        const glm::mat4 inverseEntity =
            glm::inverse(RagdollEntityWorldMatrix(scene, entity));
        auto getModel = [&](uint32_t bodyId, glm::mat4& model) {
            const JPH::BodyID id(bodyId);
            if (!m_impl->bodyInterface->IsAdded(id)) return false;
            const JPH::RVec3 position = m_impl->bodyInterface->GetPosition(id);
            const JPH::Quat rotation = m_impl->bodyInterface->GetRotation(id);
            const glm::quat q(rotation.GetW(), rotation.GetX(),
                              rotation.GetY(), rotation.GetZ());
            const glm::mat4 world =
                glm::translate(glm::mat4(1.0f),
                               glm::vec3(position.GetX(), position.GetY(),
                                         position.GetZ())) *
                glm::mat4_cast(q);
            model = inverseEntity * world;
            return true;
        };
        for (const auto& part : runtime.parts) {
            glm::mat4 model;
            if (!getModel(part.bodyId, model)) continue;
            if (part.boneIndex >= 0 && part.boneIndex < 255) {
                skeleton.boneMatrices[static_cast<size_t>(part.boneIndex)] =
                    model * part.inverseBind;
            }
        }
        for (const auto& follow : runtime.followers) {
            if (follow.parentPart >= runtime.parts.size()) continue;
            glm::mat4 parentModel;
            if (!getModel(runtime.parts[follow.parentPart].bodyId,
                          parentModel)) {
                continue;
            }
            if (follow.boneIndex >= 0 && follow.boneIndex < 255) {
                skeleton.boneMatrices[static_cast<size_t>(follow.boneIndex)] =
                    parentModel * follow.localBind * follow.inverseBind;
            }
        }
        skeleton.isAnimated = true;
    }
}

void EnginePhysicsSystem::SetBodyTransform(uint32_t bodyId, const glm::vec3& position, const glm::quat& rotation) {
    if (!m_impl->initialized || bodyId == 0xFFFFFFFF) return;
    JPH::BodyID id(bodyId);
    if (!m_impl->bodyInterface->IsAdded(id)) return;
    
    // Jolt quaternions are XYZW; glm uses WXYZ.
    JPH::Quat joltRot(rotation.x, rotation.y, rotation.z, rotation.w);
    JPH::RVec3 joltPos(position.x, position.y, position.z);
    
    // Teleport the body and wake it so it cannot sleep in mid-air.
    m_impl->bodyInterface->SetPositionAndRotation(id, joltPos, joltRot, JPH::EActivation::Activate);
    m_impl->bodyInterface->SetLinearAndAngularVelocity(id, JPH::Vec3::sZero(), JPH::Vec3::sZero());
}
void EnginePhysicsSystem::AddImpulse(uint32_t bodyId, const glm::vec3& impulse) {
    if (!m_impl->initialized) return;
    JPH::BodyID id(bodyId);
    if (m_impl->bodyInterface->IsAdded(id)) {
        m_impl->bodyInterface->ActivateBody(id);
        m_impl->bodyInterface->AddImpulse(id, JPH::Vec3(impulse.x, impulse.y, impulse.z));
        m_impl->bodyInterface->ActivateBody(id);
    }
}

void EnginePhysicsSystem::SetLinearVelocity(uint32_t bodyId, const glm::vec3& velocity) {
    if (!m_impl->initialized) return;
    JPH::BodyID id(bodyId);
    if (m_impl->bodyInterface->IsAdded(id)) {
        m_impl->bodyInterface->ActivateBody(id);
        m_impl->bodyInterface->SetLinearVelocity(id, JPH::Vec3(velocity.x, velocity.y, velocity.z));
        m_impl->bodyInterface->ActivateBody(id);
    }
}

glm::vec3 EnginePhysicsSystem::GetLinearVelocity(uint32_t bodyId) const {
    if (!m_impl->initialized || bodyId == 0xFFFFFFFF) return glm::vec3(0.0f);
    JPH::BodyID id(bodyId);
    if (!m_impl->bodyInterface->IsAdded(id)) return glm::vec3(0.0f);
    const JPH::Vec3 velocity = m_impl->bodyInterface->GetLinearVelocity(id);
    return glm::vec3(velocity.GetX(), velocity.GetY(), velocity.GetZ());
}


void EnginePhysicsSystem::SyncTransformsToECS(Scene& scene) {
    if (!m_impl->initialized) return;
    auto& reg = scene.Registry();
    auto view = reg.view<RigidBodyComponent, TransformComponent>();

    auto computeParentWorld = [&](entt::entity entity) {
        glm::mat4 parentWorld(1.0f);
        entt::entity parent = entt::null;
        if (reg.all_of<ParentComponent>(entity)) {
            parent = reg.get<ParentComponent>(entity).parent;
        }
        if (!reg.valid(parent)) return parentWorld;

        std::vector<entt::entity> chain;
        entt::entity current = parent;
        int depth = 0;
        while (current != entt::null && depth++ < 128) {
            chain.push_back(current);
            if (reg.all_of<ParentComponent>(current))
                current = reg.get<ParentComponent>(current).parent;
            else
                break;
        }
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            if (reg.all_of<TransformComponent>(*it))
                parentWorld = parentWorld *
                    reg.get<TransformComponent>(*it).LocalMatrix();
        }
        return parentWorld;
    };

    // Static colliders that are parented to a moving entity must move with
    // their parent. Jolt bodies are world-space, so without this the body
    // stays behind while the child's local transform is rewritten every frame
    // to compensate. A camera parented below such a collider then appears to
    // shake or stay fixed while its character moves.
    std::vector<entt::entity> parentedStaticBodies;

    for (auto e : view) {
        auto& rb = view.get<RigidBodyComponent>(e);
        auto& tf = view.get<TransformComponent>(e);

        if (rb.bodyId == 0xFFFFFFFFu) continue;
        JPH::BodyID id(rb.bodyId);
        if (!m_impl->bodyInterface->IsAdded(id)) continue;

        const bool parentedStatic =
            reg.all_of<ParentComponent>(e) &&
            rb.motionType == RigidBodyComponent::MotionType::Static;
        if (parentedStatic) {
            // Keep the authored local transform; the body is moved to the
            // composed parent pose after all regular body transforms have
            // been read back into ECS.
            parentedStaticBodies.push_back(e);
            continue;
        }

        const JPH::RVec3 p = m_impl->bodyInterface->GetPosition(id);
        const glm::vec3 worldPosition(static_cast<float>(p.GetX()),
                                      static_cast<float>(p.GetY()),
                                      static_cast<float>(p.GetZ()));
        const JPH::Quat q = m_impl->bodyInterface->GetRotation(id);
        const glm::quat worldRotation(q.GetW(), q.GetX(), q.GetY(), q.GetZ());

        // Jolt bodies live in world space, while TransformComponent stores a
        // local transform. For a nested collider (for example a collision
        // capsule parented to a character), writing the world transform
        // directly to the local transform applies the parent offset twice and
        // moves all attached meshes away. Convert the body pose back into the
        // parent's local space instead.
        if (reg.all_of<ParentComponent>(e)) {
            const glm::mat4 parentWorld = computeParentWorld(e);
            const glm::mat4 invParent = glm::inverse(parentWorld);
            tf.position = glm::vec3(
                invParent * glm::vec4(worldPosition, 1.0f));
            const glm::quat parentRotation =
                glm::normalize(glm::quat_cast(glm::mat3(parentWorld)));
            tf.rotation =
                glm::normalize(glm::inverse(parentRotation) * worldRotation);
        } else {
            tf.position = worldPosition;
            tf.rotation = worldRotation;
        }
    }

    for (entt::entity e : parentedStaticBodies) {
        auto& rb = reg.get<RigidBodyComponent>(e);
        auto& tf = reg.get<TransformComponent>(e);
        if (rb.bodyId == 0xFFFFFFFFu) continue;
        JPH::BodyID id(rb.bodyId);
        if (!m_impl->bodyInterface->IsAdded(id)) continue;

        const glm::mat4 world = computeParentWorld(e) * tf.LocalMatrix();
        const glm::vec3 worldPosition(world[3]);
        const glm::quat worldRotation =
            glm::normalize(glm::quat_cast(glm::mat3(world)));

        const JPH::RVec3 p = m_impl->bodyInterface->GetPosition(id);
        const JPH::Quat q = m_impl->bodyInterface->GetRotation(id);
        const float dx = worldPosition.x - static_cast<float>(p.GetX());
        const float dy = worldPosition.y - static_cast<float>(p.GetY());
        const float dz = worldPosition.z - static_cast<float>(p.GetZ());
        const float rotationDot = std::abs(
            worldRotation.w * q.GetW() + worldRotation.x * q.GetX() +
            worldRotation.y * q.GetY() + worldRotation.z * q.GetZ());
        if (dx * dx + dy * dy + dz * dz > 1e-10f ||
            rotationDot < 0.999999f) {
            m_impl->bodyInterface->SetPositionAndRotation(
                id,
                JPH::RVec3(worldPosition.x, worldPosition.y, worldPosition.z),
                JPH::Quat(worldRotation.x, worldRotation.y, worldRotation.z,
                          worldRotation.w),
                JPH::EActivation::DontActivate);
        }
    }
}

void EnginePhysicsSystem::SetOnContactCallback(std::function<void(entt::entity, entt::entity)> cb) {
    m_impl->contactListener.onContact = std::move(cb);
}

void EnginePhysicsSystem::SetOnTriggerBeginOverlap(
    std::function<void(entt::entity, entt::entity)> cb) {
    m_impl->contactListener.onTriggerBegin = std::move(cb);
}

void EnginePhysicsSystem::SetOnTriggerEndOverlap(
    std::function<void(entt::entity, entt::entity)> cb) {
    m_impl->contactListener.onTriggerEnd = std::move(cb);
}

} // namespace Galeshi

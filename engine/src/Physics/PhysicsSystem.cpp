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
#include <Jolt/Physics/SoftBody/SoftBodySharedSettings.h>
#include <Jolt/Physics/SoftBody/SoftBodyCreationSettings.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>

#include "GaleshiEngine/Physics/PhysicsSystem.hpp"
#include <spdlog/spdlog.h>
#include "GaleshiEngine/Scene/Scene.hpp"
#include "GaleshiEngine/Assets/Mesh.hpp"
#include <Jolt/Physics/SoftBody/SoftBodyMotionProperties.h>
#include <Jolt/Physics/Body/BodyLockInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include "GaleshiEngine/Scene/Components.hpp"

#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_map>
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
    std::unique_ptr<JPH::TempAllocatorImpl>   tempAllocator;
    std::unique_ptr<JPH::JobSystemThreadPool> jobSystem;
    std::unique_ptr<JPH::PhysicsSystem>       physicsSystem;

    BPFilter  bpFilter;
    ObjVsBP   objVsBp;
    ObjPair   objPair;

    JPH::BodyInterface* bodyInterface = nullptr;
    ContactListenerImpl contactListener;
    std::unordered_map<uint32_t, entt::entity> bodyToEntity;
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

void EnginePhysicsSystem::Update(Scene& /*scene*/, float dt) {
    if (!m_impl->initialized) return;
    constexpr int kCollisionSteps = 1;
    m_impl->physicsSystem->Update(dt, kCollisionSteps,
        m_impl->tempAllocator.get(), m_impl->jobSystem.get());

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


uint32_t EnginePhysicsSystem::CreateCloth(int gridX, int gridY, float spacing, const glm::vec3& origin, entt::entity entity) {
    if (!m_impl->initialized) return 0xFFFFFFFFu;

    // Jolt 5.2 soft-body API: shared settings, faces, then constraints.
    JPH::Ref<JPH::SoftBodySharedSettings> settings = new JPH::SoftBodySharedSettings;

    for (int y = 0; y < gridY; ++y) {
        for (int x = 0; x < gridX; ++x) {
            JPH::SoftBodySharedSettings::Vertex v;
            JPH::Vec3 pos(
                origin.x + (x - gridX * 0.5f) * spacing,
                origin.y,
                origin.z + (y - gridY * 0.5f) * spacing
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

    JPH::SoftBodySharedSettings::VertexAttributes attr;
    attr.mCompliance = 1.0e-4f;
    attr.mShearCompliance = 1.0e-4f;
    attr.mBendCompliance = 1.0e-3f;
    settings->CreateConstraints(&attr, 1, JPH::SoftBodySharedSettings::EBendType::Distance);
    settings->Optimize();

    JPH::SoftBodyCreationSettings sbSettings(
        settings,
        JPH::RVec3(origin.x, origin.y, origin.z),
        JPH::Quat::sIdentity(),
        LAYER_MOVING);
    sbSettings.mFriction = 0.5f;
    sbSettings.mRestitution = 0.0f;
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
        std::vector<glm::vec3> positions(joltVertices.size());
        JPH::RVec3 bodyPos = body.GetPosition();
        JPH::Quat bodyRot = body.GetRotation();
        for (size_t i = 0; i < joltVertices.size(); ++i) {
            JPH::RVec3 pos = bodyPos + bodyRot * joltVertices[i].mPosition;
            positions[i] = glm::vec3(pos.GetX(), pos.GetY(), pos.GetZ());
        }
        mc.mesh->UpdateVertices(positions);
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

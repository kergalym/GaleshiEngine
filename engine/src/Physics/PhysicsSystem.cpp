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
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
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

#include <algorithm>
#include <unordered_map>
#include <entt/entt.hpp>
#include <functional>
#include <thread>

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
    std::unordered_map<uint32_t, entt::entity>* bodyToEntity = nullptr;

    JPH::ValidateResult OnContactValidate(const JPH::Body&, const JPH::Body&,
                                          JPH::RVec3Arg,
                                          const JPH::CollideShapeResult&) override {
        return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
    }

    void OnContactAdded(const JPH::Body& b1, const JPH::Body& b2,
                        const JPH::ContactManifold&, JPH::ContactSettings&) override {
        if (!bodyToEntity || !onContact) return;
        uint32_t id1 = b1.GetID().GetIndexAndSequenceNumber();
        uint32_t id2 = b2.GetID().GetIndexAndSequenceNumber();
        auto it1 = bodyToEntity->find(id1);
        auto it2 = bodyToEntity->find(id2);
        if (it1 != bodyToEntity->end() && it2 != bodyToEntity->end())
            onContact(it1->second, it2->second);
    }

    void OnContactPersisted(const JPH::Body&, const JPH::Body&,
                            const JPH::ContactManifold&, JPH::ContactSettings&) override {}
    void OnContactRemoved(const JPH::SubShapeIDPair&) override {}
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
    JPH::BodyID id = m_impl->bodyInterface->CreateAndAddBody(s, JPH::EActivation::Activate);
    if (id.IsInvalid()) return 0xFFFFFFFFu;
    uint32_t rawId = id.GetIndexAndSequenceNumber();
    if (entity != entt::null) m_impl->bodyToEntity[rawId] = entity;
    return rawId;
}


uint32_t EnginePhysicsSystem::CreateCloth(int gridX, int gridY, float spacing, const glm::vec3& origin, entt::entity entity) {
    if (!m_impl->initialized) return 0xFFFFFFFFu;

    // Правильный API Jolt 5.2: SoftBodySharedSettings + AddFace + CreateConstraints
    JPH::Ref<JPH::SoftBodySharedSettings> settings = new JPH::SoftBodySharedSettings;

    // 1. Вершины
    for (int y = 0; y < gridY; ++y) {
        for (int x = 0; x < gridX; ++x) {
            JPH::SoftBodySharedSettings::Vertex v;
            JPH::Vec3 pos(
                origin.x + (x - gridX * 0.5f) * spacing,
                origin.y,
                origin.z + (y - gridY * 0.5f) * spacing
            );
            pos.StoreFloat3(&v.mPosition);
            // Углы фиксированы (invMass = 0), остальные подвижны
            bool isCorner = (x == 0 || x == gridX - 1) && (y == 0 || y == gridY - 1);
            v.mInvMass = isCorner ? 0.0f : 1.0f;
            settings->mVertices.push_back(v);
        }
    }

    // 2. Треугольники (2 на каждый квадрат)
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

    // 3. Ограничения (пружины)
    JPH::SoftBodySharedSettings::VertexAttributes attr;
    attr.mCompliance = 1.0e-4f;
    attr.mShearCompliance = 1.0e-4f;
    attr.mBendCompliance = 1.0e-3f;
    settings->CreateConstraints(&attr, 1, JPH::SoftBodySharedSettings::EBendType::Distance);
    settings->Optimize();

    // 4. Создание тела
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
    
    // Jolt использует XYZW для quat, glm использует WXYZ.
    JPH::Quat joltRot(rotation.x, rotation.y, rotation.z, rotation.w);
    JPH::RVec3 joltPos(position.x, position.y, position.z);
    
    // Телепортируем тело (отключаем сон, чтобы оно не висело в воздухе, если его разбудили)
    m_impl->bodyInterface->SetPositionAndRotation(id, joltPos, joltRot, JPH::EActivation::Activate);
    m_impl->bodyInterface->SetLinearAndAngularVelocity(id, JPH::Vec3::sZero(), JPH::Vec3::sZero());
}

void EnginePhysicsSystem::SyncTransformsToECS(Scene& scene) {
    if (!m_impl->initialized) return;
    auto& reg = scene.Registry();
    auto view = reg.view<RigidBodyComponent, TransformComponent>();

    for (auto e : view) {
        auto& rb = view.get<RigidBodyComponent>(e);
        auto& tf = view.get<TransformComponent>(e);

        if (rb.bodyId == 0xFFFFFFFFu) continue;
        JPH::BodyID id(rb.bodyId);
        if (!m_impl->bodyInterface->IsAdded(id)) continue;

        const JPH::RVec3 p = m_impl->bodyInterface->GetPosition(id);
        tf.position = glm::vec3(static_cast<float>(p.GetX()),
                                static_cast<float>(p.GetY()),
                                static_cast<float>(p.GetZ()));

        const JPH::Quat q = m_impl->bodyInterface->GetRotation(id);
        tf.rotation = glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
    }
}

void EnginePhysicsSystem::SetOnContactCallback(std::function<void(entt::entity, entt::entity)> cb) {
    m_impl->contactListener.onContact = std::move(cb);
}

} // namespace Galeshi

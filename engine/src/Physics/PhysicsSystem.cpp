#include "GaleshiEngine/Physics/PhysicsSystem.hpp"
#include "GaleshiEngine/Scene/Scene.hpp"

#include <spdlog/spdlog.h>

#ifdef GALESHI_PHYSICS_ENABLED

// ============================================================
// Полная реализация с Jolt (включается флагом GALESHI_PHYSICS_ENABLED)
// ============================================================

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>

#include <glm/gtc/quaternion.hpp>
#include <unordered_map>
#include <thread>

using namespace JPH;

namespace Galeshi {

namespace Layers {
    static constexpr ObjectLayer NON_MOVING = 0;
    static constexpr ObjectLayer MOVING     = 1;
    static constexpr ObjectLayer NUM_LAYERS = 2;
};

namespace BroadPhaseLayers {
    static constexpr BroadPhaseLayer NON_MOVING(0);
    static constexpr BroadPhaseLayer MOVING(1);
    static constexpr uint NUM_LAYERS(2);
};

class BPLayerInterfaceImpl final : public BroadPhaseLayerInterface {
public:
    BPLayerInterfaceImpl() {
        mObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
        mObjectToBroadPhase[Layers::MOVING]     = BroadPhaseLayers::MOVING;
    }
    virtual uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NUM_LAYERS; }
    virtual BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer inLayer) const override { return mObjectToBroadPhase[inLayer]; }
private:
    BroadPhaseLayer mObjectToBroadPhase[Layers::NUM_LAYERS];
};

class ObjectVsBroadPhaseLayerFilterImpl : public ObjectVsBroadPhaseLayerFilter {
public:
    virtual bool ShouldCollide(ObjectLayer inLayer1, BroadPhaseLayer inLayer2) const override {
        if (inLayer1 == Layers::NON_MOVING) return inLayer2 == BroadPhaseLayers::MOVING;
        return true;
    }
};

class ObjectLayerPairFilterImpl : public ObjectLayerPairFilter {
public:
    virtual bool ShouldCollide(ObjectLayer inObject1, ObjectLayer inObject2) const override {
        if (inObject1 == Layers::NON_MOVING) return inObject2 == Layers::MOVING;
        return true;
    }
};

struct PhysicsSystem::Impl {
    TempAllocatorImpl* tempAllocator = nullptr;
    JobSystemThreadPool* jobSystem = nullptr;
    JPH::PhysicsSystem* physics = nullptr;
    
    BPLayerInterfaceImpl bpLayerInterface;
    ObjectVsBroadPhaseLayerFilterImpl objVsBpFilter;
    ObjectLayerPairFilterImpl objVsObjFilter;
    std::unordered_map<entt::entity, BodyID> bodyMap;

    void RemoveBody(entt::entity e) {
        auto it = bodyMap.find(e);
        if (it != bodyMap.end() && physics) {
            physics->GetBodyInterface().RemoveBody(it->second);
            physics->GetBodyInterface().DestroyBody(it->second);
            bodyMap.erase(it);
        }
    }
};

PhysicsSystem::PhysicsSystem() : m_impl(std::make_unique<Impl>()) {
    spdlog::info("[Physics] Init Jolt");
    RegisterDefaultAllocator();
    Factory::sInstance = new Factory();
    RegisterTypes();
}

PhysicsSystem::~PhysicsSystem() {
    Shutdown();
    UnregisterTypes();
    delete Factory::sInstance;
    Factory::sInstance = nullptr;
}

void PhysicsSystem::Init() {
    m_impl->tempAllocator = new TempAllocatorImpl(10 * 1024 * 1024);
    int numThreads = static_cast<int>(std::thread::hardware_concurrency()) - 1;
    if (numThreads < 1) numThreads = 1;
    m_impl->jobSystem = new JobSystemThreadPool(cMaxPhysicsJobs, cMaxPhysicsBarriers, numThreads);
    m_impl->physics = new JPH::PhysicsSystem();

    m_impl->physics->Init(1024, 0, 1024, 1024,
                          m_impl->bpLayerInterface, m_impl->objVsBpFilter, m_impl->objVsObjFilter);
    m_impl->physics->SetGravity(Vec3(0, -9.81f, 0));
}

void PhysicsSystem::Shutdown() {
    if (m_impl->physics) {
        m_impl->bodyMap.clear();
        delete m_impl->physics;
        m_impl->physics = nullptr;
    }
    delete m_impl->jobSystem; m_impl->jobSystem = nullptr;
    delete m_impl->tempAllocator; m_impl->tempAllocator = nullptr;
}

void PhysicsSystem::Reset() {
    if (!m_impl->physics) return;
    for (auto& [e, bodyID] : m_impl->bodyMap) {
        m_impl->physics->GetBodyInterface().RemoveBody(bodyID);
        m_impl->physics->GetBodyInterface().DestroyBody(bodyID);
    }
    m_impl->bodyMap.clear();
}

void PhysicsSystem::Update(Scene& scene, float dt) {
    if (!m_impl->physics) return;
    auto& reg = scene.Registry();
    auto view = reg.view<TransformComponent, BoxColliderComponent>();

    for (auto e : view) {
        if (m_impl->bodyMap.find(e) == m_impl->bodyMap.end()) {
            const auto& tc = view.get<TransformComponent>(e);
            const auto& bc = view.get<BoxColliderComponent>(e);
            bool isDynamic = true;
            float mass = 1.0f;
            if (reg.all_of<RigidBodyComponent>(e)) {
                const auto& rb = reg.get<RigidBodyComponent>(e);
                isDynamic = (rb.motionType == RigidBodyComponent::MotionType::Dynamic);
                mass = rb.mass;
            }
            Vec3 halfExt(bc.halfExtents.x * tc.scale.x, bc.halfExtents.y * tc.scale.y, bc.halfExtents.z * tc.scale.z);
            BoxShapeSettings shapeSettings(halfExt);
            ShapeRefC shape = shapeSettings.Create().Get();
            BodyCreationSettings settings(shape, Vec3(tc.position.x, tc.position.y, tc.position.z),
                                          Quat(tc.rotation.x, tc.rotation.y, tc.rotation.z, tc.rotation.w),
                                          isDynamic ? EMotionType::Dynamic : EMotionType::Static,
                                          isDynamic ? Layers::MOVING : Layers::NON_MOVING);
            settings.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = mass;
            BodyID bodyID = m_impl->physics->GetBodyInterface().CreateAndAddBody(settings, EActivation::Activate);
            m_impl->bodyMap[e] = bodyID;
        }
    }

    std::vector<entt::entity> toRemove;
    for (auto& [e, bodyID] : m_impl->bodyMap) {
        if (!reg.valid(e) || !reg.all_of<BoxColliderComponent>(e)) toRemove.push_back(e);
    }
    for (auto e : toRemove) m_impl->RemoveBody(e);

    m_impl->physics->Update(dt, 1, m_impl->tempAllocator, m_impl->jobSystem);

    for (auto& [e, bodyID] : m_impl->bodyMap) {
        if (reg.all_of<RigidBodyComponent>(e)) {
            if (reg.get<RigidBodyComponent>(e).motionType != RigidBodyComponent::MotionType::Dynamic) continue;
        }
        Vec3 pos = m_impl->physics->GetBodyInterface().GetPosition(bodyID);
        Quat rot = m_impl->physics->GetBodyInterface().GetRotation(bodyID);
        auto& tc = reg.get<TransformComponent>(e);
        tc.position = glm::vec3(pos.GetX(), pos.GetY(), pos.GetZ());
        tc.rotation = glm::quat(rot.GetW(), rot.GetX(), rot.GetY(), rot.GetZ());
    }
}

} // namespace Galeshi

#else // GALESHI_PHYSICS_ENABLED

// ============================================================
// Пустая реализация (физика отключена)
// ============================================================

namespace Galeshi {

struct PhysicsSystem::Impl {};

PhysicsSystem::PhysicsSystem() : m_impl(std::make_unique<Impl>()) {
    spdlog::info("[Physics] DISABLED (rebuild with GALESHI_PHYSICS_ENABLED=1 to enable)");
}

PhysicsSystem::~PhysicsSystem() = default;
void PhysicsSystem::Init() {}
void PhysicsSystem::Shutdown() {}
void PhysicsSystem::Reset() {}
void PhysicsSystem::Update(Scene&, float) {}

} // namespace Galeshi

#endif // GALESHI_PHYSICS_ENABLED

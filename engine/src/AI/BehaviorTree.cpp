#include "GaleshiEngine/AI/BehaviorTree.hpp"
#include "GaleshiEngine/AI/NavigationSystem.hpp"
#include "GaleshiEngine/Animation/AnimationSystem.hpp"
#include "GaleshiEngine/Scene/Scene.hpp"
#include "GaleshiEngine/Assets/Mesh.hpp"
#include "GaleshiEngine/Assets/Material.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <random>
#include <spdlog/spdlog.h>

namespace Galeshi {
namespace {

constexpr int kFailure = 0;
constexpr int kSuccess = 1;
constexpr int kRunning = 2;

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

glm::mat4 WorldMatrix(Scene& scene, entt::entity entity) {
    glm::mat4 world(1.0f);
    std::vector<entt::entity> chain;
    entt::entity current = entity;
    int depth = 0;
    while (current != entt::null && scene.Registry().valid(current) && depth++ < 128) {
        chain.push_back(current);
        if (scene.Registry().all_of<ParentComponent>(current)) {
            current = scene.Registry().get<ParentComponent>(current).parent;
        } else {
            break;
        }
    }
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        if (scene.Registry().all_of<TransformComponent>(*it)) {
            world *= scene.Registry().get<TransformComponent>(*it).LocalMatrix();
        }
    }
    return world;
}

glm::vec3 WorldPosition(Scene& scene, entt::entity entity) {
    return glm::vec3(WorldMatrix(scene, entity) * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
}

bool IsTargetEntity(Scene& scene, entt::entity entity,
                    const PerceptionComponent& perception) {
    if (entity == entt::null || !scene.Registry().valid(entity)) return false;
    if (scene.Registry().all_of<PlayerTagComponent>(entity)) return true;

    if (!perception.targetName.empty()) {
        if (scene.Registry().all_of<NameComponent>(entity)) {
            const std::string name = Lower(scene.Registry().get<NameComponent>(entity).name);
            const std::string wanted = Lower(perception.targetName);
            if (!wanted.empty() && name.find(wanted) != std::string::npos) return true;
        }
        return false;
    }

    if (perception.detectPlayerOnly) return false;
    return scene.Registry().all_of<MeshComponent>(entity) &&
           !scene.Registry().all_of<BehaviorTreeComponent>(entity) &&
           !scene.Registry().all_of<AIControllerComponent>(entity);
}

bool RayAabbLocal(const glm::vec3& origin, const glm::vec3& dir,
                  const glm::vec3& bmin, const glm::vec3& bmax,
                  float& tEnter, float& tExit) {
    tEnter = 0.0f;
    tExit = std::numeric_limits<float>::max();
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(dir[axis]) < 1e-8f) {
            if (origin[axis] < bmin[axis] || origin[axis] > bmax[axis]) return false;
            continue;
        }
        float t0 = (bmin[axis] - origin[axis]) / dir[axis];
        float t1 = (bmax[axis] - origin[axis]) / dir[axis];
        if (t0 > t1) std::swap(t0, t1);
        tEnter = std::max(tEnter, t0);
        tExit = std::min(tExit, t1);
        if (tEnter > tExit) return false;
    }
    return tExit >= 0.0f;
}

bool HasLineOfSight(Scene& scene, entt::entity viewer, entt::entity target,
                    const glm::vec3& from, const glm::vec3& to) {
    const glm::vec3 delta = to - from;
    const float distance = glm::length(delta);
    if (distance < 1e-4f) return true;
    const glm::vec3 dir = delta / distance;

    auto view = scene.Registry().view<MeshComponent>();
    for (auto candidate : view) {
        if (candidate == viewer || candidate == target) continue;
        const auto& meshComponent = view.get<MeshComponent>(candidate);
        if (!meshComponent.mesh || !meshComponent.mesh->Valid()) continue;
        if (meshComponent.primitiveType == "landscape") continue;
        if (scene.Registry().all_of<MaterialComponent>(candidate)) {
            const auto& mat = scene.Registry().get<MaterialComponent>(candidate).material;
            if (mat && mat->isGlass) continue;
        }

        const glm::mat4 world = WorldMatrix(scene, candidate);
        const glm::mat4 inverse = glm::inverse(world);
        const glm::vec3 localOrigin = glm::vec3(inverse * glm::vec4(from, 1.0f));
        // Keep the inverse-scaled direction unnormalized.  The ray parameter
        // then remains in world units, so tEnter can be compared with the
        // world-space distance to the target.
        const glm::vec3 localDir = glm::vec3(inverse * glm::vec4(dir, 0.0f));
        if (glm::dot(localDir, localDir) < 1e-12f) continue;
        glm::vec3 bmin, bmax;
        meshComponent.mesh->LocalAABB(bmin, bmax);
        float tEnter = 0.0f, tExit = 0.0f;
        if (RayAabbLocal(localOrigin, localDir, bmin, bmax, tEnter, tExit)) {
            if (tEnter > 0.05f && tEnter < distance - 0.2f) return false;
        }
    }
    return true;
}

AnimationStateMachineComponent* FindAnimationMachine(Scene& scene, entt::entity entity) {
    entt::entity current = entity;
    int depth = 0;
    while (current != entt::null && scene.Registry().valid(current) && depth++ < 128) {
        if (scene.Registry().all_of<AnimationStateMachineComponent>(current)) {
            return &scene.Registry().get<AnimationStateMachineComponent>(current);
        }
        if (scene.Registry().all_of<ParentComponent>(current)) {
            current = scene.Registry().get<ParentComponent>(current).parent;
        } else {
            break;
        }
    }
    return nullptr;
}

void SetStateIfPresent(Scene& scene, entt::entity entity, const std::string& state) {
    if (state.empty()) return;
    if (auto* machine = FindAnimationMachine(scene, entity)) {
        AnimationSystem::SetState(*machine, state);
    }
}

float ExtractionDistance(const glm::vec3& a, const glm::vec3& b) {
    return glm::length(glm::vec3(a.x - b.x, 0.0f, a.z - b.z));
}

void FaceTarget(TransformComponent& transform, const glm::vec3& target) {
    const glm::vec3 delta = target - transform.position;
    if (glm::dot(delta, delta) < 1e-6f) return;
    const float yaw = std::atan2(delta.x, delta.z);
    transform.rotation = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
}

bool MoveDirectly(TransformComponent& transform, const glm::vec3& target,
                  float speed, float dt, float stoppingDistance) {
    glm::vec3 delta = target - transform.position;
    const float distance = glm::length(delta);
    if (distance <= stoppingDistance) return true;
    if (distance > 1e-5f) {
        const float step = std::min(distance, std::max(0.0f, speed) * std::max(0.0f, dt));
        transform.position += delta / distance * step;
        FaceTarget(transform, target);
    }
    return distance <= stoppingDistance;
}

struct TickContext {
    TickContext(Scene& s, entt::entity e, BehaviorTreeComponent& t)
        : scene(s), entity(e), tree(t) {}

    Scene& scene;
    entt::entity entity;
    BehaviorTreeComponent& tree;
    PerceptionComponent* perception = nullptr;
    NavAgentComponent* agent = nullptr;
    TransformComponent* transform = nullptr;
    float dt = 0.0f;
    int runningNode = -1;
};

int TickNode(TickContext& ctx, int nodeIndex, int depth = 0);

int TickSelector(TickContext& ctx, BehaviorTreeNode& node, int depth) {
    for (int child : node.children) {
        if (child < 0 || child >= static_cast<int>(ctx.tree.nodes.size())) continue;
        const int status = TickNode(ctx, child, depth + 1);
        if (status != kFailure) {
            if (status == kRunning) ctx.runningNode = child;
            return status;
        }
    }
    return kFailure;
}

int TickSequence(TickContext& ctx, BehaviorTreeNode& node, int depth) {
    for (int child : node.children) {
        if (child < 0 || child >= static_cast<int>(ctx.tree.nodes.size())) continue;
        const int status = TickNode(ctx, child, depth + 1);
        if (status == kRunning) {
            ctx.runningNode = child;
            return kRunning;
        }
        if (status == kFailure) return kFailure;
    }
    return kSuccess;
}

bool TargetValid(Scene& scene, const PerceptionComponent* perception) {
    if (!perception) return false;
    if (perception->targetEntity == entt::null) return false;
    return scene.Registry().valid(perception->targetEntity);
}

glm::vec3 TargetPosition(Scene& scene, const PerceptionComponent& perception) {
    if (TargetValid(scene, &perception)) {
        return WorldPosition(scene, perception.targetEntity);
    }
    return perception.lastKnownPosition;
}

bool InAttackRange(Scene& scene, TickContext& ctx, const BehaviorTreeNode& node) {
    if (!ctx.perception || !ctx.perception->canSeeTarget) return false;
    if (!TargetValid(scene, ctx.perception)) return false;
    const glm::vec3 target = TargetPosition(scene, *ctx.perception);
    return ExtractionDistance(ctx.transform->position, target) <= node.range;
}

int TickCondition(Scene& scene, TickContext& ctx, BehaviorTreeNode& node) {
    switch (node.condition) {
        case BehaviorCondition::AlwaysTrue:
            return kSuccess;
        case BehaviorCondition::HasTarget:
            return TargetValid(scene, ctx.perception) ? kSuccess : kFailure;
        case BehaviorCondition::TargetVisible:
            return (ctx.perception && ctx.perception->canSeeTarget) ? kSuccess : kFailure;
        case BehaviorCondition::TargetInRange:
            return InAttackRange(scene, ctx, node) ? kSuccess : kFailure;
        case BehaviorCondition::TargetLost:
            return (!ctx.perception || !ctx.perception->canSeeTarget) ? kSuccess : kFailure;
        case BehaviorCondition::ParameterTrue: {
            auto* machine = FindAnimationMachine(scene, ctx.entity);
            if (!machine || node.parameter.empty()) return kFailure;
            return AnimationSystem::GetParameter(*machine, node.parameter) > 0.5f
                ? kSuccess : kFailure;
        }
    }
    return kFailure;
}

int TickAction(Scene& scene, TickContext& ctx, BehaviorTreeNode& node) {
    if (!ctx.transform) return kFailure;

    switch (node.action) {
        case BehaviorAction::Wait: {
            if (node.lastStatus != kRunning) {
                node.elapsed = 0.0f;
                node.running = true;
            }
            node.elapsed += std::max(0.0f, ctx.dt);
            node.lastStatus = kRunning;
            if (node.elapsed >= std::max(0.0f, node.duration)) {
                node.elapsed = 0.0f;
                node.running = false;
                node.lastStatus = kSuccess;
                return kSuccess;
            }
            return kRunning;
        }

        case BehaviorAction::SetTargetToPlayer: {
            if (!ctx.perception || !TargetValid(scene, ctx.perception) || !ctx.agent) return kFailure;
            ctx.agent->targetPosition = TargetPosition(scene, *ctx.perception);
            ctx.agent->hasTarget = true;
            return kSuccess;
        }

        case BehaviorAction::MoveToTarget: {
            if (!ctx.perception || !TargetValid(scene, ctx.perception)) return kFailure;
            if (ctx.perception->timeSinceSeen > ctx.perception->memoryDuration) return kFailure;

            const glm::vec3 target = TargetPosition(scene, *ctx.perception);
            const float distance = ExtractionDistance(ctx.transform->position, target);
            if (distance <= std::max(0.25f, node.range * 0.25f)) return kSuccess;

            SetStateIfPresent(scene, ctx.entity, "Run");
            if (ctx.agent) {
                const bool targetMoved = glm::distance(ctx.agent->targetPosition, target) > 0.75f;
                if (ctx.agent->path.empty() && (!ctx.agent->hasTarget || targetMoved)) {
                    ctx.agent->targetPosition = target;
                    ctx.agent->hasTarget = true;
                }
                return kRunning;
            }

            const bool arrived = MoveDirectly(*ctx.transform, target, 3.0f, ctx.dt, 0.35f);
            return arrived ? kSuccess : kRunning;
        }

        case BehaviorAction::Patrol: {
            if (!ctx.tree.homeInitialized) {
                ctx.tree.homePosition = ctx.transform->position;
                ctx.tree.homeInitialized = true;
                node.elapsed = 0.0f;
            }

            SetStateIfPresent(scene, ctx.entity, "Walk");
            if (!ctx.agent) return kFailure;

            if (ctx.agent->path.empty() && !ctx.agent->hasTarget) {
                if (node.elapsed > 0.0f) {
                    node.elapsed -= std::max(0.0f, ctx.dt);
                    return kRunning;
                }

                std::mt19937 rng(static_cast<uint32_t>(ctx.entity) * 747796405u +
                                 node.randomIteration * 2891336453u);
                ++node.randomIteration;
                std::uniform_real_distribution<float> angleDist(0.0f, 6.2831853f);
                std::uniform_real_distribution<float> radiusDist(1.5f, std::max(1.5f, node.radius));
                const float angle = angleDist(rng);
                const float radius = radiusDist(rng);
                const glm::vec3 target = ctx.tree.homePosition +
                    glm::vec3(std::cos(angle) * radius, 0.0f, std::sin(angle) * radius);
                ctx.agent->targetPosition = target;
                ctx.agent->hasTarget = true;
                node.elapsed = std::max(0.5f, node.duration);
                return kRunning;
            }
            ctx.agent->speed = std::max(0.1f, ctx.agent->speed);
            return kRunning;
        }

        case BehaviorAction::Attack: {
            if (!InAttackRange(scene, ctx, node)) {
                node.elapsed = 0.0f;
                node.lastStatus = kFailure;
                return kFailure;
            }
            const glm::vec3 target = TargetPosition(scene, *ctx.perception);
            FaceTarget(*ctx.transform, target);
            SetStateIfPresent(scene, ctx.entity, "Attack");
            node.elapsed += std::max(0.0f, ctx.dt);
            if (node.elapsed >= std::max(0.1f, node.duration)) {
                node.elapsed = 0.0f;
                spdlog::info("[AI] Entity {} attacks target {}",
                             static_cast<uint32_t>(ctx.entity),
                             static_cast<uint32_t>(ctx.perception->targetEntity));
            }
            node.lastStatus = kRunning;
            return kRunning;
        }

        case BehaviorAction::LookAtTarget: {
            if (!ctx.perception || !TargetValid(scene, ctx.perception)) return kFailure;
            FaceTarget(*ctx.transform, TargetPosition(scene, *ctx.perception));
            return kSuccess;
        }

        case BehaviorAction::SetAnimationState: {
            SetStateIfPresent(scene, ctx.entity, node.value);
            return kSuccess;
        }

        case BehaviorAction::ClearTarget: {
            if (ctx.perception) {
                ctx.perception->targetEntity = entt::null;
                ctx.perception->canSeeTarget = false;
                ctx.perception->timeSinceSeen = ctx.perception->memoryDuration + 1.0f;
            }
            return kSuccess;
        }

        case BehaviorAction::SetNavSpeed: {
            if (!ctx.agent) return kFailure;
            try {
                if (!node.value.empty()) ctx.agent->speed = std::max(0.1f, std::stof(node.value));
            } catch (...) {
                return kFailure;
            }
            return kSuccess;
        }
    }
    return kFailure;
}

int TickNode(TickContext& ctx, int nodeIndex, int depth) {
    if (depth > 64 || nodeIndex < 0 ||
        nodeIndex >= static_cast<int>(ctx.tree.nodes.size())) {
        return kFailure;
    }
    BehaviorTreeNode& node = ctx.tree.nodes[nodeIndex];
    int status = kFailure;
    switch (node.type) {
        case BehaviorNodeType::Selector:
            status = TickSelector(ctx, node, depth);
            break;
        case BehaviorNodeType::Sequence:
            status = TickSequence(ctx, node, depth);
            break;
        case BehaviorNodeType::Condition:
            status = TickCondition(ctx.scene, ctx, node);
            break;
        case BehaviorNodeType::Action:
            status = TickAction(ctx.scene, ctx, node);
            break;
    }
    node.lastStatus = status;
    if (status != kRunning) {
        node.running = false;
        ctx.runningNode = -1;
    }
    return status;
}

void UpdatePerception(Scene& scene, entt::entity entity, PerceptionComponent& perception,
                      float dt) {
    perception.scanTimer -= std::max(0.0f, dt);
    if (perception.scanTimer > 0.0f) {
        if (!perception.canSeeTarget) {
            perception.timeSinceSeen += std::max(0.0f, dt);
        }
        return;
    }
    perception.scanTimer = std::max(0.02f, perception.detectionInterval);

    if (perception.targetEntity != entt::null &&
        !scene.Registry().valid(perception.targetEntity)) {
        perception.targetEntity = entt::null;
        perception.canSeeTarget = false;
    }

    const glm::mat4 world = WorldMatrix(scene, entity);
    const glm::vec3 eye = glm::vec3(world * glm::vec4(0.0f, perception.eyeHeight, 0.0f, 1.0f));
    const glm::vec3 forward = glm::normalize(glm::vec3(world * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)));
    const float halfFov = glm::radians(std::clamp(perception.fieldOfViewDegrees, 1.0f, 360.0f) * 0.5f);
    const float cosHalfFov = std::cos(halfFov);

    entt::entity bestTarget = entt::null;
    float bestDistance = std::numeric_limits<float>::max();
    glm::vec3 bestTargetPosition{0.0f};

    auto candidates = scene.Registry().view<TransformComponent>();
    for (auto candidate : candidates) {
        if (candidate == entity) continue;
        if (!IsTargetEntity(scene, candidate, perception)) continue;
        if (scene.Registry().all_of<BehaviorTreeComponent>(candidate) ||
            scene.Registry().all_of<AIControllerComponent>(candidate)) continue;

        const glm::vec3 targetPos = WorldPosition(scene, candidate) + glm::vec3(0.0f, 1.0f, 0.0f);
        const glm::vec3 delta = targetPos - eye;
        const float distance = glm::length(delta);
        if (distance > perception.sightRadius + perception.hearingRadius) continue;

        bool detected = distance <= perception.hearingRadius;
        if (!detected && distance <= perception.sightRadius && distance > 1e-4f) {
            const float dot = glm::dot(forward, delta / distance);
            const bool inFov = perception.fieldOfViewDegrees >= 359.0f || dot >= cosHalfFov;
            if (inFov) {
                detected = !perception.requireLineOfSight ||
                           HasLineOfSight(scene, entity, candidate, eye, targetPos);
            }
        }
        if (!detected) continue;

        if (distance < bestDistance) {
            bestDistance = distance;
            bestTarget = candidate;
            bestTargetPosition = targetPos;
        }
    }

    if (bestTarget != entt::null) {
        perception.targetEntity = bestTarget;
        perception.canSeeTarget = true;
        perception.timeSinceSeen = 0.0f;
        perception.lastKnownPosition = bestTargetPosition;
    } else {
        perception.canSeeTarget = false;
        if (perception.targetEntity != entt::null &&
            scene.Registry().valid(perception.targetEntity)) {
            perception.lastKnownPosition = WorldPosition(scene, perception.targetEntity);
        }
        perception.timeSinceSeen += std::max(0.0f, dt);
    }

    if (auto* machine = FindAnimationMachine(scene, entity)) {
        AnimationSystem::SetParameter(*machine, "PlayerVisible",
                                      perception.canSeeTarget ? 1.0f : 0.0f);
        AnimationSystem::SetParameter(*machine, "TargetDistance",
                                      bestTarget != entt::null ? bestDistance : 1000.0f);
    }
}

} // namespace

BehaviorTreeComponent BehaviorTreeSystem::MakeDefaultBehaviorTree() {
    BehaviorTreeComponent tree;
    tree.nodes = {
        // 0: Root selector.
        {"Root", BehaviorNodeType::Selector, BehaviorCondition::AlwaysTrue,
         BehaviorAction::Wait, {1, 4, 8}, 0.0f, 0.0f, 0.0f, {}, {}},
        // 1: Attack branch.
        {"Attack", BehaviorNodeType::Sequence, BehaviorCondition::AlwaysTrue,
         BehaviorAction::Wait, {2, 3}, 0.0f, 0.0f, 0.0f, {}, {}},
        {"TargetInRange", BehaviorNodeType::Condition, BehaviorCondition::TargetInRange,
         BehaviorAction::Wait, {}, 2.5f, 0.0f, 0.0f, {}, {}},
        {"Attack", BehaviorNodeType::Action, BehaviorCondition::AlwaysTrue,
         BehaviorAction::Attack, {}, 2.5f, 0.8f, 0.0f, {}, "Attack"},
        // 4: Chase branch.
        {"Chase", BehaviorNodeType::Sequence, BehaviorCondition::AlwaysTrue,
         BehaviorAction::Wait, {5, 6, 7}, 0.0f, 0.0f, 0.0f, {}, {}},
        {"TargetVisible", BehaviorNodeType::Condition, BehaviorCondition::TargetVisible,
         BehaviorAction::Wait, {}, 0.0f, 0.0f, 0.0f, {}, {}},
        {"MoveToTarget", BehaviorNodeType::Action, BehaviorCondition::AlwaysTrue,
         BehaviorAction::MoveToTarget, {}, 0.35f, 0.0f, 0.0f, {}, {}},
        {"RunAnimation", BehaviorNodeType::Action, BehaviorCondition::AlwaysTrue,
         BehaviorAction::SetAnimationState, {}, 0.0f, 0.0f, 0.0f, {}, "Run"},
        // 8: Idle patrol branch.
        {"Patrol", BehaviorNodeType::Action, BehaviorCondition::AlwaysTrue,
         BehaviorAction::Patrol, {}, 0.0f, 2.5f, 12.0f, {}, "Walk"},
    };
    tree.root = 0;
    return tree;
}

void BehaviorTreeSystem::ResetRuntime(BehaviorTreeComponent& tree) {
    tree.statusText = "Idle";
    tree.runningNode = -1;
    tree.tickAccumulator = 0.0f;
    tree.homeInitialized = false;
    for (auto& node : tree.nodes) {
        node.elapsed = 0.0f;
        node.running = false;
        node.lastStatus = 0;
        node.randomIteration = 0;
    }
}

void BehaviorTreeSystem::ResetPerception(PerceptionComponent& perception) {
    perception.canSeeTarget = false;
    perception.targetEntity = entt::null;
    perception.timeSinceSeen = 1000.0f;
    perception.scanTimer = 0.0f;
    perception.lastKnownPosition = glm::vec3(0.0f);
}

void BehaviorTreeSystem::Update(Scene& scene, float dt, NavigationSystem* navSystem) {
    (void)navSystem; // Path following is done by AISystem after trees tick.

    // Perception is useful on its own, so tick it for every entity that has
    // the component even when no behavior tree is attached.
    auto perceptionView = scene.Registry().view<PerceptionComponent>();
    for (auto entity : perceptionView) {
        UpdatePerception(scene, entity, perceptionView.get<PerceptionComponent>(entity), dt);
    }

    auto view = scene.Registry().view<BehaviorTreeComponent>();
    for (auto entity : view) {
        auto& tree = view.get<BehaviorTreeComponent>(entity);
        if (!tree.enabled) {
            tree.statusText = "Disabled";
            continue;
        }
        if (tree.nodes.empty() || tree.root < 0 ||
            tree.root >= static_cast<int>(tree.nodes.size())) {
            tree.statusText = "No tree";
            continue;
        }

        // Optional throttling. Zero means tick every frame.  Keep the frame dt
        // intact for the other trees in this view; only this tree uses the
        // accumulated interval.
        float treeDt = dt;
        if (tree.tickInterval > 0.0f) {
            tree.tickAccumulator += dt;
            if (tree.tickAccumulator < tree.tickInterval) continue;
            treeDt = tree.tickAccumulator;
            tree.tickAccumulator = 0.0f;
        }

        TickContext ctx(scene, entity, tree);
        ctx.perception = scene.Registry().try_get<PerceptionComponent>(entity);
        ctx.agent = scene.Registry().try_get<NavAgentComponent>(entity);
        ctx.transform = scene.Registry().try_get<TransformComponent>(entity);
        ctx.dt = treeDt;

        if (!ctx.transform) {
            tree.statusText = "No transform";
            continue;
        }

        const int status = TickNode(ctx, tree.root);
        tree.runningNode = ctx.runningNode;
        tree.statusText = status == kSuccess ? "Success"
                        : status == kRunning ? "Running"
                        : "Failure";

        // If a tree succeeds, start from the root on the next frame so
        // selector branches can re-evaluate immediately.
        if (status != kRunning) {
            for (auto& node : tree.nodes) {
                if (node.type == BehaviorNodeType::Action) node.running = false;
            }
        }
    }
}

} // namespace Galeshi

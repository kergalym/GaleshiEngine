#pragma once

#include <cstdint>

#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <string>
#include <vector>

#include "GaleshiEngine/UI/UIWidgets.hpp"

namespace Galeshi {

class Mesh;
class Material;
class Texture;

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

// ---------------------------------------------------------------------------
// Animation state machine
// ---------------------------------------------------------------------------

enum class AnimationCondition {
    BoolTrue = 0,
    BoolFalse,
    Greater,
    Less,
    Trigger,
    Always
};

struct AnimationState {
    std::string name = "State";
    int clip = 0;
    bool loop = true;
    float speed = 1.0f;
};

struct AnimationTransition {
    int fromState = -1;             // -1 = any state
    int toState = 0;
    std::string parameter;          // empty for Always/OnFinish conditions
    AnimationCondition condition = AnimationCondition::BoolTrue;
    float threshold = 0.5f;
    float duration = 0.25f;
    bool enabled = true;
};

struct AnimationParameter {
    std::string name;
    float value = 0.0f;
};

struct AnimationStateMachineComponent {
    bool enabled = true;
    std::vector<AnimationState> states;
    std::vector<AnimationTransition> transitions;
    std::vector<AnimationParameter> parameters;

    // Runtime state. currentState is persisted so a saved scene resumes on
    // the same state; the remaining fields are reset when needed.
    int currentState = 0;
    int previousState = -1;
    float stateTime = 0.0f;
    float previousTime = 0.0f;
    float blendElapsed = 0.0f;
    float blendDuration = 0.25f;
    float playbackSpeed = 1.0f;
    bool initialized = false;
    float currentClipDuration = 0.0f;
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

enum class ColliderShapeType {
    Box = 0,
    Sphere = 1,
    Capsule = 2,
};

// Editor-authored collision volume. A collider can be attached to a mesh
// entity or live on its own as an invisible collision shape. Physics
// properties are applied when the Jolt body is created.
struct ColliderComponent {
    ColliderShapeType shapeType = ColliderShapeType::Box;

    glm::vec3 halfExtents{0.5f};  // Box half extents
    float     radius     = 0.5f;  // Sphere / capsule radius
    float     halfHeight = 0.5f;  // Capsule cylinder half height

    // Local-space offset from the entity origin. X/Y are the primary axes
    // used to place shapes in the editor.
    glm::vec3 offset{0.0f};

    // Physics material.
    float friction    = 0.5f;
    float restitution = 0.0f;
    bool  isTrigger   = false;

    // Editor-only visualization. Collider wireframes are always hidden while
    // the scene is playing, regardless of this flag.
    bool visibleInEditor = true;
};

// Backwards-compatible alias for scenes and code that still refer to the
// original box-only collider.
using BoxColliderComponent = ColliderComponent;

struct LandscapeComponent {
    int sizeX = 100;
    int sizeZ = 100;
    float scale = 1.0f;
    std::vector<float> heights;
};

struct VegetationComponent {
    bool enabled = true;
    bool castShadows = true;
    bool grassCastShadows = false;

    int treeCount = 120;
    int grassCount = 2000;

    float treeMinScale = 0.75f;
    float treeMaxScale = 1.45f;
    float grassMinScale = 0.65f;
    float grassMaxScale = 1.25f;
    // Maximum terrain gradient (rise over run) where vegetation may spawn.
    float slopeLimit = 0.65f;
    uint32_t seed = 1337;

    glm::vec4 trunkColor{0.28f, 0.16f, 0.08f, 1.0f};
    glm::vec4 canopyColor{0.16f, 0.42f, 0.12f, 1.0f};
    glm::vec4 grassColor{0.22f, 0.52f, 0.14f, 1.0f};

    // Local-space instance transforms (landscape entity space).
    std::vector<glm::mat4> treeInstances;
    std::vector<glm::mat4> grassInstances;
    bool dirty = true;
};

struct NavAgentComponent {
    std::vector<glm::vec3> path;
    size_t currentWaypoint = 0;
    float speed = 3.0f;
    float stoppingDistance = 0.5f;

    // AI pathfinding
    glm::vec3 targetPosition{0.0f};
    bool hasTarget = false;
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


struct AIControllerComponent {
    std::string scriptPath = "assets/scripts/ai_controller.lua";
};

// Tag used by perception and gameplay code to identify the player.  The Lua
// `new_physics_cube` helper adds this automatically for an entity named
// "Player"; the editor also offers it for manually authored actors.
struct PlayerTagComponent {};

struct PerceptionComponent {
    float sightRadius = 20.0f;
    float fieldOfViewDegrees = 110.0f;
    float hearingRadius = 8.0f;
    float eyeHeight = 1.6f;
    float detectionInterval = 0.15f;
    float memoryDuration = 4.0f;
    bool requireLineOfSight = true;
    bool detectPlayerOnly = true;
    std::string targetName = "Player";

    // Runtime perception state.
    bool canSeeTarget = false;
    float timeSinceSeen = 1000.0f;
    glm::vec3 lastKnownPosition{0.0f};
    entt::entity targetEntity = entt::null;
    float scanTimer = 0.0f;
};

enum class BehaviorNodeType : uint8_t {
    Selector = 0,
    Sequence,
    Condition,
    Action
};

enum class BehaviorCondition : uint8_t {
    AlwaysTrue = 0,
    HasTarget,
    TargetVisible,
    TargetInRange,
    TargetLost,
    ParameterTrue
};

enum class BehaviorAction : uint8_t {
    Wait = 0,
    SetTargetToPlayer,
    MoveToTarget,
    Patrol,
    Attack,
    LookAtTarget,
    SetAnimationState,
    ClearTarget,
    SetNavSpeed
};

struct BehaviorTreeNode {
    std::string name = "Node";
    BehaviorNodeType type = BehaviorNodeType::Action;
    BehaviorCondition condition = BehaviorCondition::AlwaysTrue;
    BehaviorAction action = BehaviorAction::Wait;
    std::vector<int> children;

    float range = 2.0f;
    float duration = 1.0f;
    float radius = 8.0f;
    std::string parameter;
    std::string value;

    // Per-node runtime data (not serialized).
    float elapsed = 0.0f;
    bool running = false;
    int lastStatus = 0; // 0 = failure, 1 = success, 2 = running
    uint32_t randomIteration = 0;
};

struct BehaviorTreeComponent {
    bool enabled = true;
    int root = 0;
    std::vector<BehaviorTreeNode> nodes;

    // Runtime state (not serialized).
    std::string statusText = "Idle";
    int runningNode = -1;
    float tickInterval = 0.0f;
    float tickAccumulator = 0.0f;
    glm::vec3 homePosition{0.0f};
    bool homeInitialized = false;
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

// CPU-simulated camera-facing billboard emitter.  Particles are spawned in
// world space around the entity origin and simulated by ParticleSystem.
struct ParticleEmitterComponent {
    bool      enabled          = true;
    float     emitRate         = 40.0f;   // particles per second
    float     lifetime         = 2.0f;    // average particle lifetime (s)
    float     lifetimeVariance = 0.4f;    // +/- random lifetime (s)
    float     startSize        = 0.30f;   // billboard half-extent at birth
    float     endSize          = 0.05f;   // billboard half-extent at death
    glm::vec4 startColor       = {1.0f, 0.65f, 0.20f, 1.0f};
    glm::vec4 endColor         = {0.9f, 0.15f, 0.02f, 0.0f};
    glm::vec3 velocity         = {0.0f, 1.5f, 0.0f};  // base spawn velocity (m/s)
    float     velocityVariance = 1.2f;    // random velocity spread (m/s)
    float     spawnRadius      = 0.2f;    // spawn sphere radius around origin
    // Half extents of a box-shaped spawn volume. When any component is
    // greater than zero, particles spawn uniformly inside the box instead of
    // the spawn sphere. Useful for rain/snow areas.
    glm::vec3 spawnBoxSize     = {0.0f, 0.0f, 0.0f};
    glm::vec3 gravity          = {0.0f, -1.5f, 0.0f};
    float     drag             = 0.4f;    // velocity damping per second
    uint32_t  maxParticles     = 512;
    // Optional sprite texture used to draw the particles. When empty the
    // renderer uses the procedural soft round sprite.
    std::shared_ptr<Texture> texture;
};

// Screen-space UI canvas.  Rendered by UIRuntime through ImGui; the widget
// list is authored in the editor or loaded from the scene file.
struct UICanvasComponent {
    UICanvas canvas;
};

} // namespace Galeshi

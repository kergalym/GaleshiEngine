#pragma once

#include <cstdint>

#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <string>
#include <vector>

#include "GaleshiEngine/Audio/AudioTypes.hpp"
#include "GaleshiEngine/Renderer/PostProcessSettings.hpp"
#include "GaleshiEngine/UI/UIWidgets.hpp"

namespace Galeshi {

class Mesh;
class Material;
class Texture;
class GroomData;

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

    // Imported model assets default to authored LOD 0 because their generated
    // cluster LODs are not a good match for characters. Procedural meshes can
    // opt into automatic LOD, and every mesh can force a specific level for
    // inspection.
    bool                  lodEnabled = true;
    int                   forcedLod = -1; // -1 = auto
    float                 lodBias = 1.0f;

    MeshComponent() = default;
    MeshComponent(std::shared_ptr<Mesh> m, std::string path = {}, std::string prim = {}, int mi = 0)
        : mesh(std::move(m)), sourceAssetPath(std::move(path)),
          primitiveType(std::move(prim)), meshIndex(mi),
          lodEnabled(sourceAssetPath.empty()) {}
};

// Per-model render settings shared by every imported submesh. Model roots
// receive this component on spawn/replacement; older scenes without it keep
// the legacy per-material fallback.
struct ModelComponent {
    bool castsShadow = true;
};

struct SkeletalComponent {
    bool isAnimated = false;
    std::string sourcePath;
    float currentTime = 0.0f;
    float animationSpeed = 1.0f;
    int activeAnimation = 0;
    std::vector<glm::mat4> boneMatrices;
};

// Entity attached to a bone of a skeletal character. local* is the authored
// transform relative to the bone; the runtime writes the resulting world
// transform into the entity's TransformComponent.
struct BoneAttachmentComponent {
    entt::entity character = entt::null;
    std::string boneName;
    glm::vec3 localPosition{0.0f};
    glm::quat localRotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 localScale{1.0f};
    // Runtime-only: last local-to-character matrix written by the attachment
    // system. If another editor/tool changes TransformComponent, the change
    // is folded back into bone space instead of being overwritten next frame.
    glm::mat4 appliedLocal{1.0f};
    bool appliedLocalValid = false;
};

// Editor selection state for the Bone Attachments controls. Stored on the
// character so the chosen bone and target entity survive scene save/load.
struct BoneAttachmentSelectionComponent {
    std::string boneName;
    entt::entity entity = entt::null;
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
    // Optional stable clip identity. Blender FBX action order is not guaranteed
    // to survive a re-export, so the clip name is preferred over the index
    // when it still exists in the loaded animation list.
    std::string animationName;
    bool loop = true;
    float speed = 1.0f;

    // Empty = use clips embedded in the character/skeleton model. A path
    // points to a separate animation FBX whose clips are retargeted by bone
    // name onto this character's skeleton.
    std::string animationPath;
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
    // Scale applied to retargeted animation translation. This is the unit
    // bridge for source clips authored in centimeters (0.01 for cm -> m).
    float animationScale = 1.0f;
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
    // Vertical field of view in degrees, clip planes and selection priority.
    // The entity Transform is the camera's pose. When the entity is parented,
    // Position/Rotation are local to the parent, so moving the camera entity
    // adjusts first-person/third-person distance without a separate offset.
    float fovY    = 60.0f;
    float nearZ   = 0.1f;
    float farZ    = 1000.0f;
    bool  primary = true;
    int   priority = 0;
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

    // Keeps the body upright by disabling rotational degrees of freedom.
    // New rigid bodies start upright; disable it for objects that must tumble.
    bool       lockRotation = true;

    // Runtime handle to Jolt body; 0xFFFFFFFF = not created.
    uint32_t   bodyId     = 0xFFFFFFFFu;

    // Runtime-only: skip writing the Jolt body pose back into this entity's
    // transform. Used by the Ground entity when its body is a landscape
    // heightfield (the entity transform is only an authoring placeholder).
    bool       syncTransform = true;
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

    // Editor-only visualization. This flag must only control the collider
    // wireframe; it never hides meshes attached to the collider entity or its
    // children. Collider wireframes are always hidden while the scene is
    // playing, regardless of this flag.
    bool visibleInEditor = true;
};

// Backwards-compatible alias for scenes and code that still refer to the
// original box-only collider.
using BoxColliderComponent = ColliderComponent;

struct LandscapeLayer {
    std::shared_ptr<Texture> albedoMap;
    std::shared_ptr<Texture> ormMap;
    std::shared_ptr<Texture> normalMap;
    bool albedoSrgb = true;
    bool ormSrgb = false;
    bool normalSrgb = false;

    // Texture-space paint mask for this layer. The vertex weights remain the
    // low-resolution shader preview; this mask gives the baked textures their
    // full brush resolution. Runtime-only except for the persisted path.
    std::vector<uint8_t> paintMask;
    int paintMaskSize = 0;
    std::string paintMaskPath;
};

struct LandscapeComponent {
    int sizeX = 100;
    int sizeZ = 100;
    float scale = 1.0f;
    std::vector<float> heights;
    // Generated bed owned by a water body. Excluded from landscape carving
    // and used only for the water's own vegetation.
    bool waterBed = false;
    // Per-vertex auto vegetation exclusion. 1 suppresses generated trees and
    // grass at that vertex; painted instances are unaffected.
    std::vector<uint8_t> vegetationMask;
    // Runtime-only base heights captured before the first water erosion.
    // Not serialized; used by the editor to re-carve after water edits.
    std::vector<float> erosionBaseHeights;
    std::vector<float> erosionBasePaintWeights;
    bool hasErosionBase = false;
    bool hasErosionPaintBase = false;
    // Legacy single-layer weights retained for scene migration.
    std::vector<float> paintWeights;
    // Unlimited painted material layers and their per-vertex weights.
    std::vector<LandscapeLayer> layers;
    std::vector<std::vector<float>> layerWeights;
    int activeLayer = 0;
    bool legacyLayerMigrated = false;
    // Source maps used as the unweighted base during texture baking. The
    // captured flag prevents a second bake from treating the generated maps
    // as the base.
    bool baseCaptured = false;
    std::string baseAlbedoPath;
    std::string baseOrmPath;
    std::string baseMetallicPath;
    std::string baseRoughnessPath;
    std::string baseOcclusionPath;
    std::string baseNormalPath;
    // Output resolution of the procedurally baked albedo/ORM/normal maps.
    // Square so the three maps stay aligned; 2048 matches the old maximum.
    int bakedTextureSize = 2048;
    // Last baked textures written under the project's textures directory.
    std::string bakedAlbedoPath;
    std::string bakedOrmPath;
    std::string bakedNormalPath;
};

// One user-defined leaf material assigned to a specific mesh of an imported
// tree model. Several sets can be added to one tree type; each set overrides
// only its target mesh so trunks and other parts keep their imported
// materials.
struct VegetationLeafSet {
    int meshIndex = -1;
    std::shared_ptr<Material> material;

    // Explicit per-mesh processing. Material names are never inspected, so
    // imported names remain free-form and localized.
    bool foliage = true;
    bool alphaTest = true;
    bool twoSided = true;
};

// Editable copy of one imported tree-model material. Stored on the tree type
// so every generated and painted instance uses the same material state.
struct VegetationMaterialOverride {
    int materialIndex = -1;
    std::shared_ptr<Material> material;
};

// One independently configured tree species used by landscape vegetation.
// Density scales the number of automatically generated instances of this
// species relative to the base Trees count; Size is a model-space multiplier
// applied to generated and painted instances.
struct VegetationTreeType {
    std::string meshPath;
    float density = 1.0f;
    float scale = 0.010f;

    // Optional per-species texture override. Empty texture slots use the
    // imported model's own materials; once any slot is assigned, this
    // material is applied to the species' tree meshes.
    std::shared_ptr<Material> material;

    // Per-mesh leaf overrides. These take precedence over `material` on the
    // targeted mesh.
    std::vector<VegetationLeafSet> leafSets;

    // Per-material edits of the imported model. The Material tab edits these
    // copies; all instances of this tree type share them.
    std::vector<VegetationMaterialOverride> materialOverrides;

    // Runtime-generated instances are deterministic from the vegetation seed
    // and are not serialized. Painted instances are authored by the brush and
    // are saved with the scene.
    std::vector<glm::mat4> generatedInstances;
    std::vector<glm::mat4> paintedInstances;
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

    // Multiple draggable/static tree species. When this list is non-empty,
    // generated tree counts are driven by each species' Density.
    std::vector<VegetationTreeType> treeTypes;

    // Hides generated procedural grass and legacy default trees. Generated
    // instances of explicitly authored tree species and painted instances
    // remain visible.
    bool hideDefaultInstances = false;

    // Brush painting controls. Size is multiplied by a tree type's Size when
    // painting a custom tree.
    float paintDensity = 1.0f;
    float paintSize = 1.0f;

    // Legacy single-model setting. It is migrated into treeTypes when an old
    // scene is loaded and is no longer edited by the new UI.
    std::string treeMeshPath;

    // Local-space instance transforms (landscape entity space).
    // treeInstances/grassInstances are deterministic generated defaults.
    // painted* arrays are authored by the vegetation brush.
    std::vector<glm::mat4> treeInstances;
    std::vector<glm::mat4> grassInstances;
    std::vector<glm::mat4> paintedTreeInstances;
    std::vector<glm::mat4> paintedGrassInstances;
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

    // Painted mesh cloth. paintWeights is one entry per source mesh vertex;
    // zero means pinned, positive values make the vertex dynamic.
    bool meshCloth = false;
    std::vector<float> paintWeights;
    // Source mesh vertex -> welded soft-body vertex. Rebuilt at runtime.
    std::vector<uint32_t> simVertexMap;

    // Physical parameters used when the soft body is rebuilt.
    float mass = 1.0f;
    float friction = 0.5f;
    float restitution = 0.0f;
    float gravityFactor = 1.0f;
    float linearDamping = 0.1f;
    float pressure = 0.0f;
    float stretchCompliance = 1.0e-4f;
    float shearCompliance = 1.0e-4f;
    float bendCompliance = 1.0e-3f;

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
    bool        playing  = false;  // Runtime state, not serialized.

    // 3D spatialization. Non-spatial sources play as 2D through their bus.
    bool        spatial = true;
    AudioBus    bus     = AudioBus::SFX;
    AudioAttenuationModel attenuation = AudioAttenuationModel::Inverse;
    float       volume        = 1.0f;
    float       pitch         = 1.0f;
    float       minDistance   = 1.0f;
    float       maxDistance   = 50.0f;
    float       rolloff       = 1.0f;
    float       dopplerFactor = 1.0f;

    // Local-space offset from the entity origin.
    glm::vec3   offset{0.0f};
};

// Marks an entity as the 3D audio listener. If no primary listener exists the
// editor/game camera is used instead.
struct AudioListenerComponent {
    bool primary = true;
};

// Scene-wide post-processing volume. Its settings apply to the entire scene
// (independent of camera position), with the highest-priority enabled volume
// winning. The size is only an editor visualization/bounds helper.
struct PostProcessVolumeComponent {
    bool      enabled  = true;
    int       priority = 0;
    glm::vec3 size{20.0f, 10.0f, 20.0f};
    bool      visibleInEditor = true;

    PostProcessSettings settings;

    // A volume is an explicit "full post-processing" override. Enable all
    // standard effects when one is added so it does not silently switch off
    // effects that were active in the scene. Stylized grading (palette) and
    // the debug invert pass stay opt-in; FXAA is redundant with SMAA/TAA.
    PostProcessVolumeComponent() {
        settings.bloomEnabled = true;
        settings.dofEnabled = true;
        settings.motionBlurEnabled = true;
        settings.ssaoEnabled = true;
        settings.ssrEnabled = true;
        settings.ssgiEnabled = true;
        settings.smaaEnabled = true;
        settings.taaEnabled = true;
        settings.godRaysEnabled = true;
        settings.atmosphereEnabled = true;
        settings.fogEnabled = true;
        settings.fogVolumetric = true;
        settings.atmosphereIntensity = 8.0f;
    }
};

// Tag: entity was created by a Lua script and should be cleaned on hot-reload.
struct LuaEntityComponent {};


struct SpotLightComponent {
    glm::vec3 direction = {0.0f, -1.0f, 0.0f}; // Spot look direction.
    float innerConeAngle = 15.0f; // Full-intensity cone angle in degrees.
    float outerConeAngle = 30.0f; // Cone angle in degrees where the light reaches zero.
    float radius = 10.0f; // Light range.
    glm::vec3 color = {1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
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

// Procedural water surface. The renderer builds a subdivided plane from this
// component, displaces it with animated waves, and shades reflections,
// screen-space refraction, depth absorption, foam, and sun glints.
enum class WaterBodyType {
    Sea = 0,
    River = 1,
    Ocean = 2,
    Lake = 3,
};

// Runtime splash ripple used by the water shader. Not serialized.
struct WaterRipple {
    glm::vec2 uv{0.0f};
    float age = 0.0f;
    float strength = 0.0f;
};

struct WaterComponent {
    WaterBodyType bodyType = WaterBodyType::Sea;
    bool enabled = true;
    bool visibleInEditor = true;
    // Water acts as a script trigger volume. Bodies entering/leaving the
    // procedural water footprint receive OnTriggerBegin/EndOverlap with the
    // water entity as the trigger.
    bool trigger = true;
    // Terrain sculpting under/around this water body.
    bool  carveLandscape = true;
    bool  terrainCarved = false;
    float basinDepth = 1.5f;   // depression depth below the water surface
    float bankWidth = 1.5f;    // eroded shore width in world units
    float bankErosion = 0.35f; // 0..1 bank height noise

    // The water body owns a generated child terrain at the basin bed with a
    // dedicated vegetation instance set.
    bool  underwaterTerrain = true;
    int   underwaterGrassCount = 240;
    glm::vec4 underwaterGrassColor{0.08f, 0.32f, 0.20f, 1.0f};

    // Character skeleton bone used as the bubble emitter when the player is
    // underwater. Empty uses the player body center.
    std::string bubbleCharacter;
    std::string bubbleBone;

    // Carved shore material detail. Detail Size is the world-space size of
    // one macro texture tile; the octave adds a higher-frequency pass.
    float shoreDetailSize = 6.0f;
    float shoreDetailStrength = 0.35f;
    float shoreDetailOctave = 4.0f;
    float shoreNormalStrength = 1.0f;

    // World-space extent of the surface.
    float width = 20.0f;
    float depth = 20.0f;

    // Wave field.
    float waveHeight   = 0.32f;
    float waveScale    = 1.0f;
    float waveSpeed    = 1.0f;
    float choppiness   = 0.35f;
    float detailStrength = 0.45f;
    glm::vec2 flowDirection{1.0f, 0.0f};

    // Optical properties.
    float refractionStrength = 0.35f;
    float reflectionStrength = 0.82f;
    // High-quality mirror pass for the nearest water plane. Disable it to
    // use only the environment cubemap reflection.
    bool  planarReflections  = true;
    float fresnelPower       = 5.0f;
    float depthFade          = 4.0f;
    float absorption         = 0.55f;
    float clarity            = 0.65f;
    float transparency       = 0.82f;

    // Shore and crest foam.
    float foamDistance  = 0.35f;
    float foamIntensity = 0.25f;

    // Specular sparkle strength.
    float sparkle = 0.35f;

    glm::vec3 shallowColor{0.08f, 0.42f, 0.46f};
    glm::vec3 deepColor{0.008f, 0.045f, 0.12f};
    glm::vec3 foamColor{0.92f, 0.96f, 1.0f};

    // Runtime-only splash disturbances. The water shader renders expanding
    // foam rings at these water-plane UV positions.
    std::vector<WaterRipple> ripples;
};

// Procedural groom hair. The runtime generates guide curves from the scalp
// region of a mesh, derives dense strands from those guides, simulates them in
// world space, and renders camera-facing ribbons.
struct GroomComponent {
    bool enabled = true;
    bool simulate = true;
    bool visibleInEditor = true;

    // Groom transform relative to the target character.
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};

    // Optional target mesh name. Empty means "use this entity's mesh, or the
    // highest descendant mesh" (the head cap of a character).
    std::string targetMeshName;
    // Optional character bone that the groom roots are rigidly bound to.
    // Empty uses the target mesh's per-vertex skin weights.
    std::string targetBoneName;

    // Groom topology.
    int guideCount = 96;
    int strandsPerGuide = 12;
    int segments = 8;

    // Shape.
    float length = 0.22f;
    float width = 0.006f;
    float rootWidth = 1.0f;
    float tipWidth = 0.08f;
    float clumpRadius = 0.012f;
    float clumping = 0.65f;
    float frizz = 0.05f;
    float curl = 0.04f;
    float volume = 0.35f;

    // Scalp selection in normalized mesh-Y space and root direction.
    float scalpStart = 0.62f;
    float scalpEnd = 1.0f;
    float normalThreshold = -0.15f;
    glm::vec3 groomDirection{0.0f, -1.0f, -0.4f};

    // Dynamics.
    float gravity = 0.35f;
    float stiffness = 0.7f;
    float damping = 0.25f;
    float windStrength = 0.15f;
    glm::vec3 windDirection{1.0f, 0.0f, 0.2f};
    float windFrequency = 0.8f;

    // Shading.
    float specular = 0.35f;
    float shininess = 36.0f;
    float ambient = 0.28f;
    float opacity = 0.9f;
    float randomTint = 0.12f;
    glm::vec3 rootColor{0.10f, 0.055f, 0.035f};
    glm::vec3 tipColor{0.34f, 0.19f, 0.10f};

    uint32_t seed = 1337;

    // Runtime-only generated data. Never serialized.
    std::shared_ptr<GroomData> runtime;
};

// Screen-space UI canvas.  Rendered by UIRuntime through ImGui; the widget
// list is authored in the editor or loaded from the scene file.
struct UICanvasComponent {
    UICanvas canvas;
};

// Runtime marker for a character driven by a Jolt ragdoll. Body ids and
// constraints are owned by EnginePhysicsSystem and are not serialized.
struct RagdollComponent {};

} // namespace Galeshi

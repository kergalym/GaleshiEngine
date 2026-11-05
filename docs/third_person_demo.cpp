// GaleshiEngine — third-person demo gameplay script.
//
// Copy this file into <project>/scripts/ or <project>/assets/scripts/.
// It is a hot-reload C++ module: it intentionally exports only the two
// GaleshiScript* entry points and does not link the engine.
//
// Required scene setup (see docs/THIRD_PERSON_DEMO.md):
//   - a ground/landscape with collision,
//   - a Camera entity named "Camera" (the script parents it to Player if it
//     has no parent),
//   - optionally a visual Player entity with a mesh/model. The Player root is
//     deliberately not a physics body. If it does not exist, the script
//     creates it and tries to spawn PLAYER_MODEL as a visual child.
//   - a separate "PlayerPhysics" entity carries the dynamic capsule body.
//     The script creates it when needed.
//
// Controls:
//   WASD       move relative to the third-person camera
//   Shift      sprint
//   Space      jump (requires the PlayerPhysics rigid body)
//   LMB        attack
//   R          respawn

#include "GaleshiEngine/Scripting/CppGameplayApi.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

using Galeshi::Script::Api;
using Galeshi::Script::Quat;
using Galeshi::Script::Vec2;
using Galeshi::Script::Vec3;
using Galeshi::Script::Vec4;

constexpr const char* kPlayerName = "Player";
constexpr const char* kPlayerPhysicsName = "PlayerPhysics";
constexpr const char* kCameraName = "Camera";
constexpr const char* kPlayerModel = "assets/models/player.glb";
constexpr const char* kFootstepSound = "assets/sounds/footstep.wav";
constexpr const char* kAttackSound = "assets/sounds/attack.wav";

constexpr bool kAutoCreatePlayer = true;
constexpr bool kAutoCreatePhysicsProxy = true;
constexpr bool kAutoParentCamera = true;

// Player root position is the character's feet. The physics proxy is a
// separate entity, so Jolt never overwrites the player's visual transform.
constexpr Vec3 kSpawn{0.0f, 2.0f, 0.0f};
constexpr float kWalkSpeed = 4.5f;
constexpr float kRunSpeed = 8.5f;
constexpr float kJumpSpeed = 7.0f;
constexpr float kGroundVelocity = 0.25f;
constexpr float kCapsuleHalfHeight = 0.85f;
constexpr float kCapsuleRadius = 0.35f;
constexpr float kCapsuleCenterY = kCapsuleHalfHeight + kCapsuleRadius;
constexpr float kCameraEyeHeight = 1.6f;
constexpr float kCameraDistance = 4.0f;

// Some imported characters face -Z instead of +Z. Set this to 3.14159265f
// when the model runs backwards.
constexpr float kModelYawOffset = 0.0f;

Quat YawQuat(float yaw) {
    return {0.0f, std::sin(yaw * 0.5f), 0.0f, std::cos(yaw * 0.5f)};
}

float HorizontalSpeed(Vec3 velocity) {
    return std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
}

bool IsMoving(Vec3 move) {
    return move.x * move.x + move.z * move.z > 0.0001f;
}

void SetupHud(Api& api, uint32_t& canvas, int32_t& widget) {
    if (canvas == UINT32_MAX || !api.world.Valid(canvas)) {
        canvas = api.ui.CreateCanvas("ThirdPersonHUD",
                                     "Third Person Demo");
        widget = -1;
    }
    if (canvas == UINT32_MAX) return;
    if (widget < 0) {
        widget = api.ui.FindWidget(canvas, "status");
        if (widget < 0) {
            widget = api.ui.AddWidget(canvas, "status", "text");
        }
    }
    if (widget < 0) return;
    api.ui.SetWidgetFlag(canvas, "status", "visible", true);
    api.ui.SetWidgetFlag(canvas, "status", "use_color", true);
    api.ui.SetWidgetVec2(canvas, "status", "position",
                         Vec2{24.0f, 24.0f});
    api.ui.SetWidgetVec2(canvas, "status", "size",
                         Vec2{620.0f, 44.0f});
    api.ui.SetWidgetFloat(canvas, "status", "font_size", 20.0f);
    api.ui.SetWidgetVec4(canvas, "status", "color",
                         Vec4{0.92f, 0.96f, 1.0f, 1.0f});
}

void UpdateHud(Api& api, uint32_t canvas, int32_t widget,
               float speed, bool grounded, bool hasBody) {
    if (canvas == UINT32_MAX || widget < 0) return;
    char text[256];
    std::snprintf(
        text, sizeof(text),
        "Third Person Demo  |  %.1f m/s  |  %s  |  %s",
        speed,
        grounded ? "grounded" : "airborne",
        hasBody ? "physics proxy" : "no rigid body");
    api.ui.SetWidgetString(canvas, "status", "label", text);
}

uint32_t FindOrCreatePhysicsProxy(Api& api, Vec3 playerPosition) {
    uint32_t proxy = api.world.Find(kPlayerPhysicsName);
    if (proxy != UINT32_MAX || !kAutoCreatePhysicsProxy) return proxy;

    proxy = api.world.Create(kPlayerPhysicsName);
    if (proxy == UINT32_MAX) return proxy;

    api.world.SetPosition(
        proxy, Vec3{playerPosition.x, playerPosition.y + kCapsuleCenterY,
                    playerPosition.z});
    return proxy;
}

} // namespace

static Galeshi::Script::Api* g_api = nullptr;
static uint32_t g_player = UINT32_MAX;
static uint32_t g_playerPhysics = UINT32_MAX;
static uint32_t g_animationEntity = UINT32_MAX;
static uint32_t g_hudCanvas = UINT32_MAX;
static int32_t g_hudWidget = -1;
static uint32_t g_preparedPlayer = UINT32_MAX;
static bool g_hasBody = false;
static bool g_jumpHeld = false;
static bool g_attackHeld = false;
static bool g_warnedPlayer = false;
static bool g_warnedPlayerBody = false;
static bool g_warnedCamera = false;
static float g_attackCooldown = 0.0f;
static float g_stepTimer = 0.0f;

extern "C" void GaleshiScriptRegister(const GaleshiScriptApi* api) {
    static Galeshi::Script::Api wrapper(api);
    g_api = &wrapper;
    g_player = UINT32_MAX;
    g_playerPhysics = UINT32_MAX;
    g_animationEntity = UINT32_MAX;
    g_hudCanvas = UINT32_MAX;
    g_hudWidget = -1;
    g_preparedPlayer = UINT32_MAX;
    g_hasBody = false;
    g_jumpHeld = false;
    g_attackHeld = false;
    g_warnedPlayer = false;
    g_warnedPlayerBody = false;
    g_warnedCamera = false;
    g_attackCooldown = 0.0f;
    g_stepTimer = 0.0f;
}

extern "C" void GaleshiScriptUpdate(float dt) {
    if (!g_api || !g_api->Compatible() || dt <= 0.0f) return;

    Api& api = *g_api;

    if (g_player == UINT32_MAX || !api.world.Valid(g_player)) {
        g_player = api.world.Find(kPlayerName);
        g_preparedPlayer = UINT32_MAX;
    }

    // Optional zero-scene setup: create a non-physical visual Player root and
    // try to attach a model as its child. The physics body lives separately.
    if (g_player == UINT32_MAX && kAutoCreatePlayer) {
        g_player = api.world.Create(kPlayerName);
        if (g_player != UINT32_MAX) {
            api.world.SetPosition(g_player, kSpawn);
            const uint32_t visual = api.animation.SpawnModel(
                kPlayerModel, "PlayerVisual", kSpawn);
            if (visual != UINT32_MAX) {
                api.world.SetParent(visual, g_player);
                api.world.SetPosition(visual, Vec3{0.0f, 0.0f, 0.0f});
                g_animationEntity = visual;
            }
            api.world.SetScale(g_player, Vec3{1.0f, 1.0f, 1.0f});
        }
    }

    if (g_player == UINT32_MAX || !api.world.Valid(g_player)) {
        if (!g_warnedPlayer) {
            g_warnedPlayer = true;
            api.Log("Third-person demo: create an entity named 'Player'.");
        }
        return;
    }

    if (g_playerPhysics != UINT32_MAX &&
        !api.world.Valid(g_playerPhysics)) {
        g_playerPhysics = UINT32_MAX;
        g_preparedPlayer = UINT32_MAX;
    }

    if (g_animationEntity == UINT32_MAX ||
        !api.world.Valid(g_animationEntity)) {
        g_animationEntity = g_player;
    }

    if (g_preparedPlayer != g_player) {
        g_preparedPlayer = g_player;

        // The Player root is visual-only. If an older scene still has a body
        // on it, disable that body so Jolt cannot overwrite its rotation.
        if (api.world.HasComponent(g_player, "rigid_body")) {
            api.physics.DestroyBody(g_player);
            if (!g_warnedPlayerBody) {
                g_warnedPlayerBody = true;
                api.Log(
                    "Third-person demo: Player must be non-physical; use "
                    "PlayerPhysics for the capsule body.");
            }
        }

        g_playerPhysics = FindOrCreatePhysicsProxy(
            api, api.world.Position(g_player));
        g_hasBody = g_playerPhysics != UINT32_MAX &&
                    api.world.HasComponent(g_playerPhysics, "rigid_body");
        if (!g_hasBody && g_playerPhysics != UINT32_MAX &&
            kAutoCreatePhysicsProxy) {
            const Vec3 authoredScale = api.world.Scale(g_playerPhysics);
            g_hasBody = api.physics.AttachCapsule(
                g_playerPhysics, kCapsuleHalfHeight, kCapsuleRadius);
            // AttachCapsule gives the entity a capsule-sized visual scale.
            // The body keeps its shape, so restore the authored scale.
            api.world.SetScale(g_playerPhysics, authoredScale);
            if (!g_hasBody) {
                api.Log(
                    "Third-person demo: could not attach the PlayerPhysics "
                    "capsule body.");
            }
        }

        api.camera.SetMode(GALESHI_CAMERA_THIRD_PERSON);
        api.player.SetStrafing(true);
        api.animation.SetParameter(g_animationEntity, "Speed", 0.0f);

        if (kAutoParentCamera) {
            const uint32_t camera = api.world.Find(kCameraName);
            if (camera != UINT32_MAX && camera != g_player &&
                api.world.Valid(camera) &&
                api.world.HasComponent(camera, "camera") &&
                api.world.Parent(camera) == UINT32_MAX) {
                api.world.SetParent(camera, g_player);
                api.world.SetPosition(
                    camera, Vec3{0.0f, kCameraEyeHeight, kCameraDistance});
            } else if (!g_warnedCamera) {
                g_warnedCamera = true;
                api.Log(
                    "Third-person demo: add a Camera named 'Camera' and "
                    "parent it to Player if it is not auto-parented.");
            }
        }
    }

    // The visual root follows the separate physics proxy. The proxy's
    // rotation never touches the Player transform.
    if (g_hasBody && g_playerPhysics != UINT32_MAX &&
        api.world.Valid(g_playerPhysics)) {
        const Vec3 bodyPosition = api.world.Position(g_playerPhysics);
        api.world.SetPosition(
            g_player, Vec3{bodyPosition.x,
                           bodyPosition.y - kCapsuleCenterY,
                           bodyPosition.z});
    }

    SetupHud(api, g_hudCanvas, g_hudWidget);
    g_attackCooldown = std::max(0.0f, g_attackCooldown - dt);
    g_stepTimer = std::max(0.0f, g_stepTimer - dt);

    float horizontal = 0.0f;
    float vertical = 0.0f;
    if (!api.ui.PointerOverUI()) {
        horizontal = api.input.Axis("Horizontal");
        vertical = api.input.Axis("Vertical");
    }
    const bool sprint = api.input.KeyDown("Left Shift");
    Vec3 move = api.camera.Movement(horizontal, vertical);
    const float targetSpeed = sprint ? kRunSpeed : kWalkSpeed;
    const bool moving = IsMoving(move);
    const float jumpAxis = api.input.Axis("Jump");
    const bool jumpDown = jumpAxis > 0.5f;
    bool grounded = true;

    if (g_hasBody) {
        Vec3 velocity = api.physics.Velocity(g_playerPhysics);
        grounded = std::abs(velocity.y) < kGroundVelocity;
        if (jumpDown && !g_jumpHeld && grounded) {
            velocity.y = kJumpSpeed;
            api.animation.Trigger(g_animationEntity, "Jump");
        }
        g_jumpHeld = jumpDown;

        const Vec3 desired{
            move.x * targetSpeed, 0.0f, move.z * targetSpeed};
        const Vec3 smoothed = VInterp(
            Vec3{velocity.x, 0.0f, velocity.z}, desired, dt, 12.0f);
        velocity.x = smoothed.x;
        velocity.z = smoothed.z;
        api.physics.SetVelocity(g_playerPhysics, velocity);
    } else {
        Vec3 position = api.world.Position(g_player);
        position.x += move.x * targetSpeed * dt;
        position.z += move.z * targetSpeed * dt;
        api.world.SetPosition(g_player, position);
        g_jumpHeld = jumpDown;
    }

    if (moving) {
        const float yaw =
            std::atan2(move.x, move.z) + kModelYawOffset;
        api.world.SetRotation(g_player, YawQuat(yaw));
    }

    const Vec3 position = api.world.Position(g_player);
    if (position.y < -30.0f || api.input.KeyDown("R")) {
        api.world.SetPosition(g_player, kSpawn);
        if (g_hasBody && g_playerPhysics != UINT32_MAX &&
            api.world.Valid(g_playerPhysics)) {
            // Recreate the proxy body at the new transform; setting the ECS
            // Transform alone does not teleport the Jolt body.
            const Vec3 authoredScale = api.world.Scale(g_playerPhysics);
            api.world.SetPosition(
                g_playerPhysics,
                Vec3{kSpawn.x, kSpawn.y + kCapsuleCenterY, kSpawn.z});
            g_hasBody = api.physics.AttachCapsule(
                g_playerPhysics, kCapsuleHalfHeight, kCapsuleRadius);
            api.world.SetScale(g_playerPhysics, authoredScale);
            api.physics.SetVelocity(g_playerPhysics, Vec3{});
        }
        api.animation.SetParameter(g_animationEntity, "Speed", 0.0f);
        api.animation.Trigger(g_animationEntity, "Respawn");
    }

    const bool attackDown =
        api.input.MouseDown("left") && !api.ui.PointerOverUI();
    if (attackDown && !g_attackHeld && g_attackCooldown <= 0.0f) {
        api.animation.Trigger(g_animationEntity, "Attack");
        g_attackCooldown = 0.45f;
        if (kAttackSound[0] != '\0') {
            api.audio.PlaySound(kAttackSound, false);
        }
    }
    g_attackHeld = attackDown;

    const Vec3 velocity =
        g_hasBody ? api.physics.Velocity(g_playerPhysics) : Vec3{};
    const float speed = g_hasBody
        ? HorizontalSpeed(velocity)
        : (moving ? targetSpeed : 0.0f);
    const float speedRatio = std::clamp(speed / kRunSpeed, 0.0f, 1.0f);
    api.animation.SetParameter(g_animationEntity, "Speed", speedRatio);

    if (moving && grounded && kFootstepSound[0] != '\0' &&
        g_stepTimer <= 0.0f) {
        api.audio.PlaySound(kFootstepSound, false);
        g_stepTimer = 0.32f;
    }

    UpdateHud(api, g_hudCanvas, g_hudWidget, speed, grounded, g_hasBody);
}

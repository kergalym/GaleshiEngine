// Player Controller (C++ hot-reload)
// Assign this file in Edit -> Scripts, or use it as the main script.
#include "GaleshiEngine/Scripting/CppGameplayApi.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace {

using Galeshi::Script::Api;
using Galeshi::Script::Quat;
using Galeshi::Script::Vec3;

constexpr const char* kPlayerName = "Player";
// Name the visible model root "PlayerVisual" to turn it independently from
// the Player controller; otherwise the Player root is rotated.
constexpr const char* kPlayerVisualName = "PlayerVisual";
// Common physics-proxy names created by the editor and the third-person demo.
constexpr const char* kBodyNames[] = {
    "PlayerPhysics",
    "CollisionCapsule",
    "PlayerCapsule",
};
constexpr float kMoveSpeed = 8.0f;
constexpr float kTurnSpeed = 1.5707963f; // 90 degrees/second
constexpr float kJumpImpulse = 8.0f;

Api* g_api = nullptr;
uint32_t g_player = UINT32_MAX;
uint32_t g_body = UINT32_MAX;
uint32_t g_visual = UINT32_MAX;
uint32_t g_animation = UINT32_MAX;

Quat YawQuat(float yaw) {
    return {0.0f, std::sin(yaw * 0.5f), 0.0f, std::cos(yaw * 0.5f)};
}

float YawFromRotation(Quat rotation) {
    return 2.0f * std::atan2(rotation.y, rotation.w);
}

float ParentWorldYaw(const Api& api, uint32_t entity) {
    float yaw = 0.0f;
    uint32_t parent = api.world.Parent(entity);
    for (int depth = 0; depth < 128 && parent != UINT32_MAX; ++depth) {
        if (!api.world.Valid(parent)) break;
        yaw += YawFromRotation(api.world.Rotation(parent));
        parent = api.world.Parent(parent);
    }
    return yaw;
}

void SetWorldYaw(Api& api, uint32_t entity, float yaw) {
    api.world.SetRotation(
        entity, YawQuat(yaw - ParentWorldYaw(api, entity)));
}

bool HasRigidBody(const Api& api, uint32_t entity) {
    return api.world.Valid(entity) &&
           api.world.HasComponent(entity, "rigid_body");
}

uint32_t FindBody(const Api& api, uint32_t player) {
    if (HasRigidBody(api, player)) return player;

    // A PlayerPhysics proxy is normally a root entity.
    const uint32_t proxy = api.world.Find(kBodyNames[0]);
    if (HasRigidBody(api, proxy)) return proxy;

    // Capsule children must belong to this Player so a stray collider cannot
    // capture movement.
    for (size_t i = 1; i < sizeof(kBodyNames) / sizeof(kBodyNames[0]); ++i) {
        const uint32_t candidate = api.world.Find(kBodyNames[i]);
        if (HasRigidBody(api, candidate) &&
            api.world.Parent(candidate) == player) {
            return candidate;
        }
    }
    return player;
}

uint32_t FindVisual(const Api& api, uint32_t player) {
    const uint32_t visual = api.world.Find(kPlayerVisualName);
    return api.world.Valid(visual) ? visual : player;
}

uint32_t FindAnimation(const Api& api, uint32_t player, uint32_t visual) {
    if (api.world.HasComponent(player, "animation_state_machine")) {
        return player;
    }
    if (api.world.Valid(visual) &&
        api.world.HasComponent(visual, "animation_state_machine")) {
        return visual;
    }
    return UINT32_MAX;
}

void AcquirePlayer(Api& api) {
    g_player = api.world.Find(kPlayerName);
    g_body = UINT32_MAX;
    g_visual = UINT32_MAX;
    g_animation = UINT32_MAX;
    if (!api.world.Valid(g_player)) {
        g_player = UINT32_MAX;
        return;
    }
    g_body = FindBody(api, g_player);
    g_visual = FindVisual(api, g_player);
    g_animation = FindAnimation(api, g_player, g_visual);
}

void RefreshEntities(Api& api) {
    if (!api.world.Valid(g_player)) {
        AcquirePlayer(api);
        return;
    }
    if (!HasRigidBody(api, g_body)) {
        g_body = FindBody(api, g_player);
    }
    if (!api.world.Valid(g_visual)) {
        g_visual = FindVisual(api, g_player);
    }
    if (g_animation == UINT32_MAX || !api.world.Valid(g_animation)) {
        g_animation = FindAnimation(api, g_player, g_visual);
    }
}

} // namespace

extern "C" void GaleshiScriptRegister(const GaleshiScriptApi* api) {
    static Galeshi::Script::Api wrapper(api);
    g_api = &wrapper;
    g_player = UINT32_MAX;
    g_body = UINT32_MAX;
    g_visual = UINT32_MAX;
    g_animation = UINT32_MAX;
}

extern "C" void GaleshiScriptUpdate(float dt) {
    if (!g_api || !g_api->Compatible() || dt <= 0.0f) return;
    Api& api = *g_api;

    if (g_player == UINT32_MAX || !api.world.Valid(g_player)) {
        AcquirePlayer(api);
        if (g_player == UINT32_MAX) return;
    }
    RefreshEntities(api);
    if (g_body == UINT32_MAX) return;

    const float horizontal = api.input.Axis("Horizontal");
    const float vertical = api.input.Axis("Vertical");
    const bool strafing = api.player.Strafing();
    const Vec3 forward = api.camera.Forward();
    const Vec3 move = api.camera.Movement(horizontal, vertical);
    const Vec3 velocity = api.physics.Velocity(g_body);
    float vx = 0.0f;
    float vz = 0.0f;

    if (std::abs(vertical) > 0.001f) {
        if (!strafing && vertical > 0.0f &&
            std::abs(horizontal) > 0.001f) {
            // W+A/W+D rotate the Player itself in world space.
            SetWorldYaw(api, g_player,
                        std::atan2(move.x, move.z));
        } else if (!strafing) {
            // W/S without lateral input keep the existing local facing.
            const Vec3 direction = vertical > 0.0f ? move : forward;
            api.world.SetRotation(
                g_visual, YawQuat(std::atan2(direction.x, direction.z)));
        }
        vx = move.x * kMoveSpeed;
        vz = move.z * kMoveSpeed;
    } else if (!strafing && std::abs(horizontal) > 0.001f) {
        // Without strafing, A/D keep their original local turn-in-place.
        const float yaw = YawFromRotation(api.world.Rotation(g_visual)) -
                          horizontal * kTurnSpeed * dt;
        api.world.SetRotation(g_visual, YawQuat(yaw));
    } else if (strafing) {
        // With strafing enabled, A/D move sideways without world/local
        // rotation.
        vx = move.x * kMoveSpeed;
        vz = move.z * kMoveSpeed;
    }

    if (HasRigidBody(api, g_body)) {
        api.physics.SetVelocity(g_body, {vx, velocity.y, vz});
        if (api.input.Axis("Jump") > 0.5f &&
            std::abs(velocity.y) < 0.5f) {
            api.physics.ApplyImpulse(
                g_body, {0.0f, kJumpImpulse, 0.0f});
        }
    } else {
        Vec3 position = api.world.Position(g_body);
        position.x += vx * dt;
        position.z += vz * dt;
        api.world.SetPosition(g_body, position);
    }

    // A separate PlayerPhysics proxy does not move the visual Player root by
    // itself. Child capsules already carry the visible hierarchy, so only
    // root proxies need the position copied.
    if (g_body != g_player &&
        api.world.Parent(g_body) != g_player) {
        api.world.SetPosition(g_player, api.world.Position(g_body));
    }

    if (g_animation != UINT32_MAX) {
        const float speed = std::min(
            1.0f, std::sqrt(vx * vx + vz * vz) / kMoveSpeed);
        api.animation.SetParameter(g_animation, "Speed", speed);
    }
}

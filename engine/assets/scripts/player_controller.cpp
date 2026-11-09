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
constexpr const char* kBodyNames[] = {
    "PlayerPhysics",
    "CollisionCapsule",
    "PlayerCapsule",
};
constexpr float kMoveSpeed = 8.0f;
constexpr float kTurnSpeed = 1.5707963f; // 90 degrees/second in radians
constexpr float kJumpImpulse = 8.0f;
bool isSwim = false;

Api* g_api = nullptr;
uint32_t g_player = UINT32_MAX;
uint32_t g_body = UINT32_MAX;
uint32_t g_visual = UINT32_MAX;
uint32_t g_animation = UINT32_MAX;

Quat IdentityQuat() {
    return {0.0f, 0.0f, 0.0f, 1.0f};
}

// x,y,z,w quaternion multiplication.
Quat MultiplyQuat(const Quat& a, const Quat& b) {
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
    };
}

Quat NormalizeQuat(const Quat& q) {
    const float length = std::sqrt(q.x * q.x + q.y * q.y +
                                   q.z * q.z + q.w * q.w);
    if (length < 1e-8f) return IdentityQuat();
    const float inv = 1.0f / length;
    return {q.x * inv, q.y * inv, q.z * inv, q.w * inv};
}

Quat InverseQuat(const Quat& q) {
    const float lengthSq = q.x * q.x + q.y * q.y +
                           q.z * q.z + q.w * q.w;
    if (lengthSq < 1e-8f) return IdentityQuat();
    const float inv = 1.0f / lengthSq;
    return {-q.x * inv, -q.y * inv, -q.z * inv, q.w * inv};
}

// Rotation around world up, without extracting or accumulating Euler angles.
Quat YawQuat(float radians) {
    const float half = radians * 0.5f;
    return {0.0f, std::sin(half), 0.0f, std::cos(half)};
}

// Build the quaternion that makes +Z point along an XZ direction.
// This is the upright shortest-arc equivalent of quatLookAt; no atan2.
Quat DirectionQuatXZ(const Vec3& direction) {
    float x = direction.x;
    float z = direction.z;
    const float lengthSq = x * x + z * z;
    if (lengthSq < 1e-8f) return IdentityQuat();

    const float invLength = 1.0f / std::sqrt(lengthSq);
    x *= invLength;
    z *= invLength;

    // Exactly backwards: choose a deterministic 180-degree Y rotation.
    if (z < -0.999999f) {
        return {0.0f, 1.0f, 0.0f, 0.0f};
    }
    return NormalizeQuat({0.0f, x, 0.0f, 1.0f + z});
}

// Absolute parent rotation: root -> immediate parent.
Quat ParentWorldRotation(const Api& api, uint32_t entity) {
    uint32_t chain[128];
    int count = 0;
    uint32_t parent = api.world.Parent(entity);
    while (parent != UINT32_MAX && api.world.Valid(parent) &&
           count < 128) {
        chain[count++] = parent;
        parent = api.world.Parent(parent);
    }

    Quat world = IdentityQuat();
    // Parents are collected child -> root; multiply root -> child.
    for (int i = count - 1; i >= 0; --i) {
        world = MultiplyQuat(world, api.world.Rotation(chain[i]));
    }
    return NormalizeQuat(world);
}

Quat WorldRotation(const Api& api, uint32_t entity) {
    return NormalizeQuat(
        MultiplyQuat(ParentWorldRotation(api, entity),
                     api.world.Rotation(entity)));
}

// Set a world-space rotation while keeping the local transform valid.
void SetWorldYaw(Api& api, uint32_t entity, const Quat& targetWorld) {
    const Quat parentWorld = ParentWorldRotation(api, entity);
    const Quat local = MultiplyQuat(
        InverseQuat(parentWorld), NormalizeQuat(targetWorld));
    api.world.SetRotation(entity, local);
}

void AddWorldYaw(Api& api, uint32_t entity, float radians) {
    SetWorldYaw(api, entity,
                MultiplyQuat(YawQuat(radians),
                             WorldRotation(api, entity)));
}

bool HasRigidBody(const Api& api, uint32_t entity) {
    return api.world.Valid(entity) &&
           api.world.HasComponent(entity, "rigid_body");
}

uint32_t FindBody(const Api& api, uint32_t player) {
    if (HasRigidBody(api, player)) return player;

    const uint32_t proxy = api.world.Find(kBodyNames[0]);
    if (HasRigidBody(api, proxy)) return proxy;

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
    const uint32_t animation = api.world.FindAnimationEntity(player);
    return animation != UINT32_MAX ? animation : player;
}

uint32_t FindAnimation(const Api& api, uint32_t player) {
    return api.world.FindAnimationEntity(player);
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
    g_animation = FindAnimation(api, g_player);
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
        g_animation = FindAnimation(api, g_player);
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

    // Water triggers write this variable on the controller's player entity.
    isSwim = api.world.GetVariable(g_player, "isSwim", 0.0f) > 0.5f;

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
            // W+A/W+D: face the horizontal movement direction in world space.
            const Vec3 direction{move.x, 0.0f, move.z};
            SetWorldYaw(api, g_player, DirectionQuatXZ(direction));
        } else if (!strafing) {
            // W/S: keep the existing forward/backpedal facing.
            const Vec3 direction = vertical > 0.0f ? move : forward;
            const Vec3 horizontalDirection{direction.x, 0.0f,
                                           direction.z};
            SetWorldYaw(api, g_visual,
                        DirectionQuatXZ(horizontalDirection));
        }
        vx = move.x * kMoveSpeed;
        vz = move.z * kMoveSpeed;
    } else if (!strafing && std::abs(horizontal) > 0.001f) {
        // A/D turn around world up without Euler extraction.
        AddWorldYaw(api, g_visual, -horizontal * kTurnSpeed * dt);
    } else if (strafing) {
        // Strafing A/D: move sideways without changing facing.
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

    if (g_body != g_player &&
        api.world.Parent(g_body) != g_player) {
        api.world.SetPosition(g_player,
                              api.world.Position(g_body));
    }

    if (g_animation != UINT32_MAX) {
        const float speed = std::min(
            1.0f, std::sqrt(vx * vx + vz * vz) / kMoveSpeed);
        api.animation.SetParameter(g_animation, "Speed", speed);
    }

    if (g_animation != UINT32_MAX) {
        api.animation.SetParameter(g_animation, "IsSwim", isSwim);
    }
}

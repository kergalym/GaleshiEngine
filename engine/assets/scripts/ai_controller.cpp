// AI Controller (C++ hot-reload)
// Drives the first NavAgent in the scene toward the Player.
#include "GaleshiEngine/Scripting/CppGameplayApi.hpp"

#include <algorithm>
#include <cmath>

namespace {

using Galeshi::Script::Api;
using Galeshi::Script::Vec3;

constexpr const char* kTargetName = "Player";
constexpr float kMoveSpeed = 3.5f;
constexpr float kChaseRadius = 20.0f;
constexpr float kStopRadius = 1.0f;

Api* g_api = nullptr;
uint32_t g_agent = UINT32_MAX;
uint32_t g_animation = UINT32_MAX;
Vec3 g_lastPosition{};
bool g_hasLastPosition = false;

uint32_t FindAnimation(const Api& api, uint32_t agent) {
    // The character hierarchy can be nested under a body/wrapper root. Find
    // the animated skeleton without relying on an authored entity name.
    return api.world.FindAnimationEntity(agent);
}

void AcquireAgent(Api& api) {
    g_agent = api.ai.FindNavAgent();
    g_animation = UINT32_MAX;
    g_hasLastPosition = false;
    if (!api.world.Valid(g_agent)) {
        g_agent = UINT32_MAX;
        return;
    }
    g_animation = FindAnimation(api, g_agent);
    g_lastPosition = api.world.Position(g_agent);
    g_hasLastPosition = true;
}

void ClearTarget(Api& api, uint32_t agent) {
    api.ai.SetFlag(agent, "nav_agent", "has_target", false);
}

} // namespace

extern "C" void GaleshiScriptRegister(const GaleshiScriptApi* api) {
    static Galeshi::Script::Api wrapper(api);
    g_api = &wrapper;
    g_agent = UINT32_MAX;
    g_animation = UINT32_MAX;
    g_hasLastPosition = false;
}

extern "C" void GaleshiScriptUpdate(float dt) {
    if (!g_api || !g_api->Compatible() || dt <= 0.0f) return;
    Api& api = *g_api;

    if (g_agent == UINT32_MAX || !api.world.Valid(g_agent)) {
        AcquireAgent(api);
        if (g_agent == UINT32_MAX) return;
    }
    if (g_animation == UINT32_MAX || !api.world.Valid(g_animation)) {
        g_animation = FindAnimation(api, g_agent);
    }

    const Vec3 agentPosition = api.world.Position(g_agent);
    float actualSpeed = 0.0f;
    if (g_hasLastPosition) {
        const float dx = agentPosition.x - g_lastPosition.x;
        const float dz = agentPosition.z - g_lastPosition.z;
        actualSpeed = std::sqrt(dx * dx + dz * dz) / dt;
    }
    g_lastPosition = agentPosition;
    g_hasLastPosition = true;

    bool hasTarget = false;
    const uint32_t target = api.world.Find(kTargetName);
    if (api.world.Valid(target)) {
        const Vec3 targetPosition = api.world.Position(target);
        const float dx = targetPosition.x - agentPosition.x;
        const float dz = targetPosition.z - agentPosition.z;
        const float distanceSq = dx * dx + dz * dz;
        if (distanceSq <= kChaseRadius * kChaseRadius &&
            distanceSq > kStopRadius * kStopRadius) {
            api.ai.SetFloat(g_agent, "nav_agent", "speed", kMoveSpeed);
            api.ai.SetVec3(g_agent, "nav_agent", "target_position",
                           targetPosition);
            api.ai.SetFlag(g_agent, "nav_agent", "has_target", true);
            hasTarget = true;
        }
    }
    if (!hasTarget) ClearTarget(api, g_agent);

    if (g_animation != UINT32_MAX) {
        const float speed = std::min(
            1.0f, actualSpeed / kMoveSpeed);
        api.animation.SetParameter(g_animation, "Speed", speed);
    }
}

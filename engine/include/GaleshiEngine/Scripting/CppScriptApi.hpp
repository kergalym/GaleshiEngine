#pragma once

#include <cmath>
#include <cstdint>

// Stable C++ ABI for hot-reloaded scripts. Script modules must not link the
// engine directly: GaleshiScriptRegister() receives pointers to the services
// below, so an editor rebuild does not invalidate already compiled scripts.
struct GaleshiScriptVec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct GaleshiScriptApi {
    void* user = nullptr;

    void (*log)(void* user, const char* message) = nullptr;
    bool (*is_key_down)(void* user, const char* key) = nullptr;
    float (*get_axis)(void* user, const char* axis) = nullptr;

    uint32_t (*find_entity)(void* user, const char* name) = nullptr;
    bool (*entity_valid)(void* user, uint32_t entity) = nullptr;
    GaleshiScriptVec3 (*get_position)(void* user, uint32_t entity) = nullptr;
    void (*set_position)(void* user, uint32_t entity,
                         GaleshiScriptVec3 position) = nullptr;
    GaleshiScriptVec3 (*get_velocity)(void* user, uint32_t entity) = nullptr;
    void (*set_velocity)(void* user, uint32_t entity,
                         GaleshiScriptVec3 velocity) = nullptr;

    float (*f_interp)(float current, float target, float deltaTime,
                      float speed) = nullptr;
    GaleshiScriptVec3 (*v_interp)(GaleshiScriptVec3 current,
                                  GaleshiScriptVec3 target,
                                  float deltaTime, float speed) = nullptr;
};

inline float GaleshiInterpFactor(float deltaTime, float speed) {
    if (speed <= 0.0f) return 1.0f;
    if (deltaTime <= 0.0f) return 0.0f;
    // Frame-rate independent exponential approach, matching FInterpTo.
    const float factor = 1.0f - std::exp(-speed * deltaTime);
    return factor < 0.0f ? 0.0f : (factor > 1.0f ? 1.0f : factor);
}

inline float FInterp(float current, float target, float deltaTime,
                     float speed) {
    return current + (target - current) *
                         GaleshiInterpFactor(deltaTime, speed);
}

inline GaleshiScriptVec3 VInterp(GaleshiScriptVec3 current,
                                 GaleshiScriptVec3 target,
                                 float deltaTime, float speed) {
    const float t = GaleshiInterpFactor(deltaTime, speed);
    return {
        current.x + (target.x - current.x) * t,
        current.y + (target.y - current.y) * t,
        current.z + (target.z - current.z) * t,
    };
}

// A script module exports these two C functions:
//
//   extern "C" void GaleshiScriptRegister(const GaleshiScriptApi* api);
//   extern "C" void GaleshiScriptUpdate(float dt);
extern "C" void GaleshiScriptRegister(const GaleshiScriptApi* api);
extern "C" void GaleshiScriptUpdate(float dt);

using GaleshiScriptRegisterFn = void (*)(const GaleshiScriptApi*);
using GaleshiScriptUpdateFn = void (*)(float);

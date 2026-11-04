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

struct GaleshiScriptVec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct GaleshiScriptQuat {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

enum GaleshiScriptCameraMode : int {
    GALESHI_CAMERA_THIRD_PERSON = 0,
    GALESHI_CAMERA_FIRST_PERSON = 1,
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

    // Switches the gameplay camera for the current player/character. Returns
    // false when the host has no gameplay camera or the mode is invalid.
    bool (*set_camera_mode)(void* user, int mode) = nullptr;
    int (*get_camera_mode)(void* user) = nullptr;

    // Horizontal camera basis for camera-relative player movement.
    GaleshiScriptVec3 (*get_camera_forward)(void* user) = nullptr;
    GaleshiScriptVec3 (*get_camera_right)(void* user) = nullptr;
    GaleshiScriptVec3 (*get_camera_movement)(void* user,
                                              float horizontal,
                                              float vertical) = nullptr;

    bool (*get_player_strafing)(void* user) = nullptr;
    void (*set_player_strafing)(void* user, bool enabled) = nullptr;

    bool (*compile_shader)(void* user, const char* path) = nullptr;
    const char* (*get_shader_error)(void* user) = nullptr;

    // Appended for forward-compatible capability checks. Older scripts
    // compiled against a previous layout simply never read these fields.
    uint32_t abi_version = 0;
    uint32_t struct_size = 0;

    // World / ECS. Appended after the original ABI fields.
    uint32_t (*create_entity)(void* user, const char* name) = nullptr;
    bool (*destroy_entity)(void* user, uint32_t entity) = nullptr;
    const char* (*get_name)(void* user, uint32_t entity) = nullptr;
    bool (*set_name)(void* user, uint32_t entity, const char* name) = nullptr;
    GaleshiScriptQuat (*get_rotation)(void* user, uint32_t entity) = nullptr;
    void (*set_rotation)(void* user, uint32_t entity,
                         GaleshiScriptQuat rotation) = nullptr;
    GaleshiScriptVec3 (*get_scale)(void* user, uint32_t entity) = nullptr;
    void (*set_scale)(void* user, uint32_t entity,
                      GaleshiScriptVec3 scale) = nullptr;
    uint32_t (*get_parent)(void* user, uint32_t entity) = nullptr;
    bool (*set_parent)(void* user, uint32_t entity,
                       uint32_t parent) = nullptr;
    bool (*has_component)(void* user, uint32_t entity,
                          const char* component) = nullptr;

    // Input and camera helpers.
    float (*get_axis_raw)(void* user, const char* axis) = nullptr;
    bool (*is_mouse_button_down)(void* user, const char* button) = nullptr;
    GaleshiScriptVec2 (*get_mouse_position)(void* user) = nullptr;
    GaleshiScriptVec2 (*get_mouse_delta)(void* user) = nullptr;
    float (*get_mouse_wheel)(void* user) = nullptr;

    // Physics. Shape attachment uses the entity's current local transform and
    // replaces any existing body/components on that entity.
    bool (*attach_box)(void* user, uint32_t entity,
                       GaleshiScriptVec3 halfExtents,
                       bool isStatic) = nullptr;
    bool (*attach_sphere)(void* user, uint32_t entity, float radius,
                          bool isStatic) = nullptr;
    bool (*attach_capsule)(void* user, uint32_t entity, float halfHeight,
                           float radius, bool isStatic) = nullptr;
    bool (*destroy_body)(void* user, uint32_t entity) = nullptr;
    bool (*apply_impulse)(void* user, uint32_t entity,
                          GaleshiScriptVec3 impulse) = nullptr;

    // Navigation. FindPath stores the result in the host; query it with
    // PathPointCount/PathPoint before issuing another navigation request.
    bool (*build_navmesh)(void* user) = nullptr;
    bool (*find_path)(void* user, GaleshiScriptVec3 start,
                      GaleshiScriptVec3 end) = nullptr;
    uint32_t (*path_point_count)(void* user) = nullptr;
    GaleshiScriptVec3 (*path_point)(void* user, uint32_t index) = nullptr;

    // Audio. Source playback uses AudioSourceComponent attached to an entity;
    // the host owns listener selection and mixer bus application.
    bool (*play_sound)(void* user, const char* path, bool loop) = nullptr;
    bool (*stop_sound)(void* user, const char* path) = nullptr;
    bool (*is_sound_playing)(void* user, const char* path) = nullptr;
    void (*stop_all_sounds)(void* user) = nullptr;
    bool (*attach_audio_source)(void* user, uint32_t entity,
                                const char* path, bool loop,
                                bool spatial, int bus) = nullptr;
    bool (*play_source)(void* user, uint32_t entity) = nullptr;
    bool (*stop_source)(void* user, uint32_t entity) = nullptr;
    bool (*is_source_playing)(void* user, uint32_t entity) = nullptr;

    // Scene-wide post-process settings. Names are stable snake_case keys such
    // as "bloom", "exposure", "ssao_radius" and "fog_color".
    bool (*set_post_process_flag)(void* user, const char* name,
                                  bool value) = nullptr;
    bool (*get_post_process_flag)(void* user, const char* name,
                                  bool* value) = nullptr;
    bool (*set_post_process_float)(void* user, const char* name,
                                   float value) = nullptr;
    bool (*get_post_process_float)(void* user, const char* name,
                                   float* value) = nullptr;
    bool (*set_post_process_int)(void* user, const char* name,
                                 int value) = nullptr;
    bool (*get_post_process_int)(void* user, const char* name,
                                 int* value) = nullptr;
    bool (*set_post_process_vec3)(void* user, const char* name,
                                  GaleshiScriptVec3 value) = nullptr;
    bool (*get_post_process_vec3)(void* user, const char* name,
                                  GaleshiScriptVec3* value) = nullptr;
    bool (*set_post_process_enabled)(void* user, bool value) = nullptr;
    bool (*get_post_process_enabled)(void* user) = nullptr;
};

inline constexpr uint32_t kGaleshiScriptApiVersion = 7;

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

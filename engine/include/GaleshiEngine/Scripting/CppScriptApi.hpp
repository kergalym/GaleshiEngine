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

struct GaleshiScriptVec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
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

enum GaleshiScriptUIEventType : int {
    GALESHI_UI_EVENT_CLICKED = 0,
    GALESHI_UI_EVENT_VALUE_CHANGED = 1,
    GALESHI_UI_EVENT_TOGGLED = 2,
    GALESHI_UI_EVENT_SHOWN = 3,
    GALESHI_UI_EVENT_HOVER = 4,
};

struct GaleshiScriptUIEvent {
    uint32_t canvas_entity = UINT32_MAX;
    int type = GALESHI_UI_EVENT_CLICKED;
    float value = 0.0f;
    bool checked = false;
    char canvas_id[64] = {};
    char widget_id[128] = {};
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

    // UI canvases and widgets. Canvas ids and widget ids are authored strings;
    // canvas entities are regular ECS entities with UICanvasComponent.
    uint32_t (*find_ui_canvas)(void* user, const char* canvas_id) = nullptr;
    uint32_t (*create_ui_canvas)(void* user, const char* canvas_id,
                                 const char* title) = nullptr;
    bool (*remove_ui_canvas)(void* user, uint32_t canvas_entity) = nullptr;
    bool (*set_ui_canvas_flag)(void* user, uint32_t canvas_entity,
                               const char* flag, bool value) = nullptr;
    bool (*get_ui_canvas_flag)(void* user, uint32_t canvas_entity,
                               const char* flag, bool* value) = nullptr;
    bool (*set_ui_canvas_float)(void* user, uint32_t canvas_entity,
                                const char* property, float value) = nullptr;
    bool (*get_ui_canvas_float)(void* user, uint32_t canvas_entity,
                                const char* property, float* value) = nullptr;
    bool (*set_ui_canvas_string)(void* user, uint32_t canvas_entity,
                                 const char* property,
                                 const char* value) = nullptr;
    const char* (*get_ui_canvas_string)(void* user,
                                        uint32_t canvas_entity,
                                        const char* property) = nullptr;
    bool (*set_ui_canvas_vec2)(void* user, uint32_t canvas_entity,
                               const char* property,
                               GaleshiScriptVec2 value) = nullptr;
    bool (*get_ui_canvas_vec2)(void* user, uint32_t canvas_entity,
                               const char* property,
                               GaleshiScriptVec2* value) = nullptr;

    int32_t (*find_ui_widget)(void* user, uint32_t canvas_entity,
                              const char* widget_id) = nullptr;
    int32_t (*add_ui_widget)(void* user, uint32_t canvas_entity,
                             const char* widget_id,
                             const char* type) = nullptr;
    bool (*remove_ui_widget)(void* user, uint32_t canvas_entity,
                             const char* widget_id) = nullptr;

    bool (*set_ui_widget_flag)(void* user, uint32_t canvas_entity,
                               const char* widget_id,
                               const char* property, bool value) = nullptr;
    bool (*get_ui_widget_flag)(void* user, uint32_t canvas_entity,
                               const char* widget_id,
                               const char* property, bool* value) = nullptr;
    bool (*set_ui_widget_float)(void* user, uint32_t canvas_entity,
                                const char* widget_id,
                                const char* property, float value) = nullptr;
    bool (*get_ui_widget_float)(void* user, uint32_t canvas_entity,
                                const char* widget_id,
                                const char* property,
                                float* value) = nullptr;
    bool (*set_ui_widget_int)(void* user, uint32_t canvas_entity,
                              const char* widget_id,
                              const char* property, int value) = nullptr;
    bool (*get_ui_widget_int)(void* user, uint32_t canvas_entity,
                              const char* widget_id,
                              const char* property, int* value) = nullptr;
    bool (*set_ui_widget_string)(void* user, uint32_t canvas_entity,
                                 const char* widget_id,
                                 const char* property,
                                 const char* value) = nullptr;
    const char* (*get_ui_widget_string)(void* user,
                                        uint32_t canvas_entity,
                                        const char* widget_id,
                                        const char* property) = nullptr;
    bool (*set_ui_widget_vec2)(void* user, uint32_t canvas_entity,
                               const char* widget_id,
                               const char* property,
                               GaleshiScriptVec2 value) = nullptr;
    bool (*get_ui_widget_vec2)(void* user, uint32_t canvas_entity,
                               const char* widget_id,
                               const char* property,
                               GaleshiScriptVec2* value) = nullptr;
    bool (*set_ui_widget_vec4)(void* user, uint32_t canvas_entity,
                               const char* widget_id,
                               const char* property,
                               GaleshiScriptVec4 value) = nullptr;
    bool (*get_ui_widget_vec4)(void* user, uint32_t canvas_entity,
                               const char* widget_id,
                               const char* property,
                               GaleshiScriptVec4* value) = nullptr;

    // Pops one event from the host queue. Returns false when the queue is
    // empty. Events are queued by the editor's UIRuntime dispatch loop.
    bool (*poll_ui_event)(void* user, GaleshiScriptUIEvent* event) = nullptr;
    bool (*is_pointer_over_ui)(void* user) = nullptr;
    const char* (*get_ui_text)(void* user, uint32_t canvas_entity,
                               const char* canvas_id,
                               const char* widget_id) = nullptr;

    // Landscape and instanced vegetation. Landscape coordinates are local to
    // the landscape entity. Vegetation regeneration requires the entity to
    // carry both LandscapeComponent and VegetationComponent.
    uint32_t (*find_landscape)(void* user) = nullptr;
    float (*landscape_sample_height)(void* user, float localX,
                                     float localZ) = nullptr;
    bool (*landscape_set_height)(void* user, float localX, float localZ,
                                 float height) = nullptr;
    // tool: 0 raise, 1 lower, 2 smooth, 3 flatten, 4 noise, 5 terrace,
    // 6 sharpen.
    bool (*landscape_apply_brush)(void* user, int tool, float localX,
                                  float localZ, float radius,
                                  float strength, float falloff,
                                  float dt) = nullptr;
    bool (*landscape_reset_heights)(void* user) = nullptr;

    uint32_t (*find_vegetation)(void* user) = nullptr;
    bool (*vegetation_regenerate)(void* user, uint32_t entity) = nullptr;
    bool (*vegetation_clear)(void* user, uint32_t entity) = nullptr;
    bool (*vegetation_set_bool)(void* user, uint32_t entity,
                                const char* property, bool value) = nullptr;
    bool (*vegetation_get_bool)(void* user, uint32_t entity,
                                const char* property, bool* value) = nullptr;
    bool (*vegetation_set_int)(void* user, uint32_t entity,
                               const char* property, int value) = nullptr;
    bool (*vegetation_get_int)(void* user, uint32_t entity,
                               const char* property, int* value) = nullptr;
    bool (*vegetation_set_float)(void* user, uint32_t entity,
                                 const char* property, float value) = nullptr;
    bool (*vegetation_get_float)(void* user, uint32_t entity,
                                 const char* property, float* value) = nullptr;
    int32_t (*vegetation_instance_count)(void* user, uint32_t entity,
                                         bool grass, bool painted) = nullptr;

    // Animation state machines, skeletal playback and asset lookup.
    bool (*set_animation_state)(void* user, uint32_t entity,
                                const char* state) = nullptr;
    bool (*set_animation_parameter)(void* user, uint32_t entity,
                                    const char* name, float value) = nullptr;
    float (*get_animation_parameter)(void* user, uint32_t entity,
                                     const char* name) = nullptr;
    bool (*trigger_animation_parameter)(void* user, uint32_t entity,
                                        const char* name) = nullptr;
    bool (*play_animation_clip)(void* user, uint32_t entity,
                                int clip) = nullptr;
    bool (*stop_animation)(void* user, uint32_t entity) = nullptr;
    bool (*set_animation_speed)(void* user, uint32_t entity,
                                float speed) = nullptr;
    float (*get_animation_speed)(void* user, uint32_t entity) = nullptr;
    uint32_t (*animation_clip_count)(void* user, uint32_t entity) = nullptr;
    const char* (*animation_clip_name)(void* user, uint32_t entity,
                                       uint32_t index) = nullptr;
    float (*animation_clip_duration)(void* user, uint32_t entity,
                                     uint32_t index) = nullptr;
    bool (*asset_exists)(void* user, const char* path) = nullptr;
    const char* (*asset_resolve_path)(void* user,
                                      const char* path) = nullptr;
    bool (*asset_load_model)(void* user, const char* path) = nullptr;

    // Particle emitters and procedural groom.
    uint32_t (*find_particle_emitter)(void* user) = nullptr;
    bool (*set_particle_flag)(void* user, uint32_t entity,
                              const char* property, bool value) = nullptr;
    bool (*get_particle_flag)(void* user, uint32_t entity,
                              const char* property, bool* value) = nullptr;
    bool (*set_particle_float)(void* user, uint32_t entity,
                               const char* property, float value) = nullptr;
    bool (*get_particle_float)(void* user, uint32_t entity,
                               const char* property, float* value) = nullptr;
    bool (*set_particle_int)(void* user, uint32_t entity,
                             const char* property, int value) = nullptr;
    bool (*get_particle_int)(void* user, uint32_t entity,
                             const char* property, int* value) = nullptr;
    bool (*set_particle_vec3)(void* user, uint32_t entity,
                              const char* property,
                              GaleshiScriptVec3 value) = nullptr;
    bool (*get_particle_vec3)(void* user, uint32_t entity,
                              const char* property,
                              GaleshiScriptVec3* value) = nullptr;
    bool (*set_particle_vec4)(void* user, uint32_t entity,
                              const char* property,
                              GaleshiScriptVec4 value) = nullptr;
    bool (*get_particle_vec4)(void* user, uint32_t entity,
                              const char* property,
                              GaleshiScriptVec4* value) = nullptr;

    uint32_t (*find_groom)(void* user) = nullptr;
    bool (*set_groom_flag)(void* user, uint32_t entity,
                           const char* property, bool value) = nullptr;
    bool (*get_groom_flag)(void* user, uint32_t entity,
                           const char* property, bool* value) = nullptr;
    bool (*set_groom_float)(void* user, uint32_t entity,
                            const char* property, float value) = nullptr;
    bool (*get_groom_float)(void* user, uint32_t entity,
                            const char* property, float* value) = nullptr;
    bool (*set_groom_int)(void* user, uint32_t entity,
                          const char* property, int value) = nullptr;
    bool (*get_groom_int)(void* user, uint32_t entity,
                          const char* property, int* value) = nullptr;
    bool (*set_groom_vec3)(void* user, uint32_t entity,
                           const char* property,
                           GaleshiScriptVec3 value) = nullptr;
    bool (*get_groom_vec3)(void* user, uint32_t entity,
                           const char* property,
                           GaleshiScriptVec3* value) = nullptr;
    bool (*set_groom_string)(void* user, uint32_t entity,
                             const char* property,
                             const char* value) = nullptr;
    const char* (*get_groom_string)(void* user, uint32_t entity,
                                    const char* property) = nullptr;

    // AI components: nav agents, perception, behavior trees, controllers.
    uint32_t (*find_nav_agent)(void* user) = nullptr;
    uint32_t (*find_perception)(void* user) = nullptr;
    uint32_t (*find_behavior_tree)(void* user) = nullptr;
    uint32_t (*find_ai_controller)(void* user) = nullptr;
    bool (*set_ai_flag)(void* user, uint32_t entity,
                        const char* component, const char* property,
                        bool value) = nullptr;
    bool (*get_ai_flag)(void* user, uint32_t entity,
                        const char* component, const char* property,
                        bool* value) = nullptr;
    bool (*set_ai_float)(void* user, uint32_t entity,
                         const char* component, const char* property,
                         float value) = nullptr;
    bool (*get_ai_float)(void* user, uint32_t entity,
                         const char* component, const char* property,
                         float* value) = nullptr;
    bool (*set_ai_int)(void* user, uint32_t entity,
                       const char* component, const char* property,
                       int value) = nullptr;
    bool (*get_ai_int)(void* user, uint32_t entity,
                       const char* component, const char* property,
                       int* value) = nullptr;
    bool (*set_ai_vec3)(void* user, uint32_t entity,
                        const char* component, const char* property,
                        GaleshiScriptVec3 value) = nullptr;
    bool (*get_ai_vec3)(void* user, uint32_t entity,
                        const char* component, const char* property,
                        GaleshiScriptVec3* value) = nullptr;
    bool (*set_ai_string)(void* user, uint32_t entity,
                          const char* component, const char* property,
                          const char* value) = nullptr;
    const char* (*get_ai_string)(void* user, uint32_t entity,
                                 const char* component,
                                 const char* property) = nullptr;

    bool (*behavior_tree_create_default)(void* user,
                                         uint32_t entity) = nullptr;
    uint32_t (*behavior_tree_node_count)(void* user,
                                         uint32_t entity) = nullptr;
    int32_t (*behavior_tree_add_node)(void* user, uint32_t entity,
                                      const char* name, int type,
                                      int condition, int action) = nullptr;
    bool (*behavior_tree_add_child)(void* user, uint32_t entity,
                                    int parent, int child) = nullptr;
    bool (*behavior_tree_set_root)(void* user, uint32_t entity,
                                   int root) = nullptr;
    bool (*behavior_tree_set_node_float)(void* user, uint32_t entity,
                                         int index, const char* property,
                                         float value) = nullptr;
    bool (*behavior_tree_set_node_int)(void* user, uint32_t entity,
                                       int index, const char* property,
                                       int value) = nullptr;
    bool (*behavior_tree_set_node_string)(void* user, uint32_t entity,
                                          int index, const char* property,
                                          const char* value) = nullptr;
    const char* (*behavior_tree_status)(void* user,
                                        uint32_t entity) = nullptr;

    // Material component and renderer controls.
    bool (*set_material_float)(void* user, uint32_t entity,
                               const char* property, float value) = nullptr;
    bool (*get_material_float)(void* user, uint32_t entity,
                               const char* property, float* value) = nullptr;
    bool (*set_material_flag)(void* user, uint32_t entity,
                              const char* property, bool value) = nullptr;
    bool (*get_material_flag)(void* user, uint32_t entity,
                              const char* property, bool* value) = nullptr;
    bool (*set_material_color)(void* user, uint32_t entity,
                               const char* property,
                               GaleshiScriptVec4 value) = nullptr;
    bool (*get_material_color)(void* user, uint32_t entity,
                               const char* property,
                               GaleshiScriptVec4* value) = nullptr;
    bool (*set_material_texture)(void* user, uint32_t entity,
                                 const char* slot,
                                 const char* path) = nullptr;
    const char* (*get_material_texture)(void* user, uint32_t entity,
                                        const char* slot) = nullptr;

    uint32_t (*renderer_width)(void* user) = nullptr;
    uint32_t (*renderer_height)(void* user) = nullptr;
    bool (*renderer_deferred)(void* user) = nullptr;
    bool (*set_renderer_deferred)(void* user, bool value) = nullptr;
    bool (*renderer_lod)(void* user) = nullptr;
    bool (*set_renderer_lod)(void* user, bool value) = nullptr;
    bool (*renderer_occlusion)(void* user) = nullptr;
    bool (*set_renderer_occlusion)(void* user, bool value) = nullptr;

    // Landscape material layers and baking.
    uint32_t (*landscape_layer_count)(void* user) = nullptr;
    bool (*landscape_add_layer)(void* user) = nullptr;
    bool (*landscape_remove_layer)(void* user, int layer) = nullptr;
    int (*landscape_active_layer)(void* user) = nullptr;
    bool (*landscape_set_active_layer)(void* user, int layer) = nullptr;
    bool (*landscape_set_layer_texture)(void* user, int layer,
                                        const char* slot,
                                        const char* path) = nullptr;
    const char* (*landscape_get_layer_texture)(void* user, int layer,
                                               const char* slot) = nullptr;
    bool (*landscape_paint_layer)(void* user, int layer, float localX,
                                  float localZ, float radius,
                                  float strength, float falloff, float dt,
                                  bool erase) = nullptr;
    bool (*landscape_bake)(void* user) = nullptr;

    // Spawn/apply an imported skeletal model. Spawn returns the root entity.
    uint32_t (*spawn_skeletal_model)(void* user, const char* path,
                                     const char* name, float x, float y,
                                     float z) = nullptr;
    bool (*apply_skeletal_model)(void* user, uint32_t entity,
                                 const char* path) = nullptr;

    bool (*set_audio_bus)(void* user, const char* name, float volume,
                          bool muted) = nullptr;
    float (*get_audio_bus_volume)(void* user, const char* name) = nullptr;
    bool (*get_audio_bus_muted)(void* user, const char* name) = nullptr;
};

inline constexpr uint32_t kGaleshiScriptApiVersion = 17;

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

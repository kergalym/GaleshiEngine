#pragma once

#include "GaleshiEngine/Scripting/CppScriptApi.hpp"

#include <cstdint>

// Thin C++ class wrappers around the stable GaleshiScriptApi C ABI.
//
// Hot-reloaded script modules should use these classes instead of calling
// the function pointers directly. The wrappers are header-only and do not
// link the engine, preserving the worker ABI.
namespace Galeshi::Script {

using Vec2 = GaleshiScriptVec2;
using Vec3 = GaleshiScriptVec3;
using Vec4 = GaleshiScriptVec4;
using Quat = GaleshiScriptQuat;

class World {
public:
    explicit World(const GaleshiScriptApi* api = nullptr) : m_api(api) {}

    uint32_t Create(const char* name) const {
        return m_api && m_api->create_entity
            ? m_api->create_entity(m_api->user, name)
            : UINT32_MAX;
    }

    bool Destroy(uint32_t entity) const {
        return m_api && m_api->destroy_entity &&
               m_api->destroy_entity(m_api->user, entity);
    }

    const char* Name(uint32_t entity) const {
        return m_api && m_api->get_name
            ? m_api->get_name(m_api->user, entity)
            : "";
    }

    bool SetName(uint32_t entity, const char* name) const {
        return m_api && m_api->set_name &&
               m_api->set_name(m_api->user, entity, name);
    }

    uint32_t Find(const char* name) const {
        return m_api && m_api->find_entity
            ? m_api->find_entity(m_api->user, name)
            : UINT32_MAX;
    }

    bool Valid(uint32_t entity) const {
        return m_api && m_api->entity_valid &&
               m_api->entity_valid(m_api->user, entity);
    }

    Vec3 Position(uint32_t entity) const {
        return m_api && m_api->get_position
            ? m_api->get_position(m_api->user, entity)
            : Vec3{};
    }

    void SetPosition(uint32_t entity, Vec3 position) const {
        if (m_api && m_api->set_position) {
            m_api->set_position(m_api->user, entity, position);
        }
    }

    Quat Rotation(uint32_t entity) const {
        return m_api && m_api->get_rotation
            ? m_api->get_rotation(m_api->user, entity)
            : Quat{};
    }

    void SetRotation(uint32_t entity, Quat rotation) const {
        if (m_api && m_api->set_rotation) {
            m_api->set_rotation(m_api->user, entity, rotation);
        }
    }

    Vec3 Scale(uint32_t entity) const {
        return m_api && m_api->get_scale
            ? m_api->get_scale(m_api->user, entity)
            : Vec3{1.0f, 1.0f, 1.0f};
    }

    void SetScale(uint32_t entity, Vec3 scale) const {
        if (m_api && m_api->set_scale) {
            m_api->set_scale(m_api->user, entity, scale);
        }
    }

    uint32_t Parent(uint32_t entity) const {
        return m_api && m_api->get_parent
            ? m_api->get_parent(m_api->user, entity)
            : UINT32_MAX;
    }

    bool SetParent(uint32_t entity, uint32_t parent) const {
        return m_api && m_api->set_parent &&
               m_api->set_parent(m_api->user, entity, parent);
    }

    bool Detach(uint32_t entity) const {
        return SetParent(entity, UINT32_MAX);
    }

    bool AttachToBone(uint32_t entity, uint32_t character,
                      const char* bone) const {
        return m_api && m_api->attach_to_bone &&
               m_api->attach_to_bone(m_api->user, entity, character, bone);
    }

    bool DetachFromBone(uint32_t entity) const {
        return m_api && m_api->detach_from_bone &&
               m_api->detach_from_bone(m_api->user, entity);
    }

    bool HasComponent(uint32_t entity, const char* component) const {
        return m_api && m_api->has_component &&
               m_api->has_component(m_api->user, entity, component);
    }

    bool AddComponent(uint32_t entity, const char* component) const {
        return m_api && m_api->add_component &&
               m_api->add_component(m_api->user, entity, component);
    }

    bool SetVariable(uint32_t entity, const char* name, float value) const {
        return m_api && m_api->set_variable &&
               m_api->set_variable(m_api->user, entity, name, value);
    }

    float GetVariable(uint32_t entity, const char* name,
                      float fallback = 0.0f) const {
        return m_api && m_api->get_variable
            ? m_api->get_variable(m_api->user, entity, name, fallback)
            : fallback;
    }

    uint32_t FindAnimationEntity(uint32_t root) const {
        return m_api && m_api->find_animation_entity
            ? m_api->find_animation_entity(m_api->user, root)
            : UINT32_MAX;
    }

    Vec3 Velocity(uint32_t entity) const {
        return m_api && m_api->get_velocity
            ? m_api->get_velocity(m_api->user, entity)
            : Vec3{};
    }

    void SetVelocity(uint32_t entity, Vec3 velocity) const {
        if (m_api && m_api->set_velocity) {
            m_api->set_velocity(m_api->user, entity, velocity);
        }
    }

private:
    const GaleshiScriptApi* m_api = nullptr;
};

class Input {
public:
    explicit Input(const GaleshiScriptApi* api = nullptr) : m_api(api) {}

    bool KeyDown(const char* key) const {
        return m_api && m_api->is_key_down &&
               m_api->is_key_down(m_api->user, key);
    }

    float Axis(const char* axis) const {
        return m_api && m_api->get_axis
            ? m_api->get_axis(m_api->user, axis)
            : 0.0f;
    }

    float AxisRaw(const char* axis) const {
        return m_api && m_api->get_axis_raw
            ? m_api->get_axis_raw(m_api->user, axis)
            : 0.0f;
    }

    bool MouseDown(const char* button) const {
        return m_api && m_api->is_mouse_button_down &&
               m_api->is_mouse_button_down(m_api->user, button);
    }

    Vec2 MousePosition() const {
        return m_api && m_api->get_mouse_position
            ? m_api->get_mouse_position(m_api->user)
            : Vec2{};
    }

    Vec2 MouseDelta() const {
        return m_api && m_api->get_mouse_delta
            ? m_api->get_mouse_delta(m_api->user)
            : Vec2{};
    }

    float MouseWheel() const {
        return m_api && m_api->get_mouse_wheel
            ? m_api->get_mouse_wheel(m_api->user)
            : 0.0f;
    }

private:
    const GaleshiScriptApi* m_api = nullptr;
};

class Physics {
public:
    explicit Physics(const GaleshiScriptApi* api = nullptr) : m_api(api) {}

    Vec3 Velocity(uint32_t entity) const {
        return m_api && m_api->get_velocity
            ? m_api->get_velocity(m_api->user, entity)
            : Vec3{};
    }

    void SetVelocity(uint32_t entity, Vec3 velocity) const {
        if (m_api && m_api->set_velocity) {
            m_api->set_velocity(m_api->user, entity, velocity);
        }
    }

    bool AttachBox(uint32_t entity, Vec3 halfExtents,
                   bool isStatic = false) const {
        return m_api && m_api->attach_box &&
               m_api->attach_box(m_api->user, entity, halfExtents,
                                 isStatic);
    }

    bool AttachSphere(uint32_t entity, float radius,
                      bool isStatic = false) const {
        return m_api && m_api->attach_sphere &&
               m_api->attach_sphere(m_api->user, entity, radius,
                                    isStatic);
    }

    bool AttachCapsule(uint32_t entity, float halfHeight, float radius,
                       bool isStatic = false) const {
        return m_api && m_api->attach_capsule &&
               m_api->attach_capsule(m_api->user, entity, halfHeight,
                                     radius, isStatic);
    }

    bool DestroyBody(uint32_t entity) const {
        return m_api && m_api->destroy_body &&
               m_api->destroy_body(m_api->user, entity);
    }

    bool ApplyImpulse(uint32_t entity, Vec3 impulse) const {
        return m_api && m_api->apply_impulse &&
               m_api->apply_impulse(m_api->user, entity, impulse);
    }

    bool CreateRagdoll(uint32_t entity) const {
        return m_api && m_api->create_ragdoll &&
               m_api->create_ragdoll(m_api->user, entity);
    }

    bool DestroyRagdoll(uint32_t entity) const {
        return m_api && m_api->destroy_ragdoll &&
               m_api->destroy_ragdoll(m_api->user, entity);
    }

    bool RagdollActive(uint32_t entity) const {
        return m_api && m_api->ragdoll_active &&
               m_api->ragdoll_active(m_api->user, entity);
    }

    bool ApplyRagdollImpulse(uint32_t entity, Vec3 impulse) const {
        return m_api && m_api->apply_ragdoll_impulse &&
               m_api->apply_ragdoll_impulse(m_api->user, entity, impulse);
    }

private:
    const GaleshiScriptApi* m_api = nullptr;
};

class Navigation {
public:
    explicit Navigation(const GaleshiScriptApi* api = nullptr)
        : m_api(api) {}

    bool BuildNavMesh() const {
        return m_api && m_api->build_navmesh &&
               m_api->build_navmesh(m_api->user);
    }

    bool FindPath(Vec3 start, Vec3 end) const {
        return m_api && m_api->find_path &&
               m_api->find_path(m_api->user, start, end);
    }

    uint32_t PathPointCount() const {
        return m_api && m_api->path_point_count
            ? m_api->path_point_count(m_api->user)
            : 0;
    }

    Vec3 PathPoint(uint32_t index) const {
        return m_api && m_api->path_point
            ? m_api->path_point(m_api->user, index)
            : Vec3{};
    }

private:
    const GaleshiScriptApi* m_api = nullptr;
};

enum class AudioBus : int {
    Master = 0,
    Music = 1,
    SFX = 2,
    Voice = 3,
    Ambient = 4,
};

class Audio {
public:
    explicit Audio(const GaleshiScriptApi* api = nullptr) : m_api(api) {}

    bool PlaySound(const char* path, bool loop = false) const {
        return m_api && m_api->play_sound &&
               m_api->play_sound(m_api->user, path, loop);
    }

    bool StopSound(const char* path) const {
        return m_api && m_api->stop_sound &&
               m_api->stop_sound(m_api->user, path);
    }

    bool IsSoundPlaying(const char* path) const {
        return m_api && m_api->is_sound_playing &&
               m_api->is_sound_playing(m_api->user, path);
    }

    void StopAll() const {
        if (m_api && m_api->stop_all_sounds) {
            m_api->stop_all_sounds(m_api->user);
        }
    }

    bool SetBus(const char* name, float volume, bool muted = false) const {
        return m_api && m_api->set_audio_bus &&
               m_api->set_audio_bus(m_api->user, name, volume, muted);
    }
    float BusVolume(const char* name) const {
        return m_api && m_api->get_audio_bus_volume
            ? m_api->get_audio_bus_volume(m_api->user, name)
            : 0.0f;
    }
    bool BusMuted(const char* name) const {
        return m_api && m_api->get_audio_bus_muted &&
               m_api->get_audio_bus_muted(m_api->user, name);
    }

    bool AttachSource(uint32_t entity, const char* path,
                      bool loop = false, bool spatial = true,
                      AudioBus bus = AudioBus::SFX) const {
        return m_api && m_api->attach_audio_source &&
               m_api->attach_audio_source(
                   m_api->user, entity, path, loop, spatial,
                   static_cast<int>(bus));
    }

    bool PlaySource(uint32_t entity) const {
        return m_api && m_api->play_source &&
               m_api->play_source(m_api->user, entity);
    }

    bool StopSource(uint32_t entity) const {
        return m_api && m_api->stop_source &&
               m_api->stop_source(m_api->user, entity);
    }

    bool IsSourcePlaying(uint32_t entity) const {
        return m_api && m_api->is_source_playing &&
               m_api->is_source_playing(m_api->user, entity);
    }

    bool PauseSound(const char* path) const {
        return m_api && m_api->pause_sound &&
               m_api->pause_sound(m_api->user, path);
    }

    bool ResumeSound(const char* path) const {
        return m_api && m_api->resume_sound &&
               m_api->resume_sound(m_api->user, path);
    }

    bool SetSoundVolume(const char* path, float volume) const {
        return m_api && m_api->set_sound_volume &&
               m_api->set_sound_volume(m_api->user, path, volume);
    }

    bool SetSoundPitch(const char* path, float pitch) const {
        return m_api && m_api->set_sound_pitch &&
               m_api->set_sound_pitch(m_api->user, path, pitch);
    }

    bool SetSoundLooping(const char* path, bool loop) const {
        return m_api && m_api->set_sound_looping &&
               m_api->set_sound_looping(m_api->user, path, loop);
    }

    bool SeekSound(const char* path, float seconds) const {
        return m_api && m_api->seek_sound &&
               m_api->seek_sound(m_api->user, path, seconds);
    }

    float SoundDuration(const char* path) const {
        return m_api && m_api->get_sound_duration
            ? m_api->get_sound_duration(m_api->user, path)
            : 0.0f;
    }

    float SoundPosition(const char* path) const {
        return m_api && m_api->get_sound_position
            ? m_api->get_sound_position(m_api->user, path)
            : 0.0f;
    }

private:
    const GaleshiScriptApi* m_api = nullptr;
};

class PostProcess {
public:
    explicit PostProcess(const GaleshiScriptApi* api = nullptr)
        : m_api(api) {}

    bool SetFlag(const char* name, bool value) const {
        return m_api && m_api->set_post_process_flag &&
               m_api->set_post_process_flag(m_api->user, name, value);
    }

    bool GetFlag(const char* name, bool& value) const {
        return m_api && m_api->get_post_process_flag &&
               m_api->get_post_process_flag(m_api->user, name, &value);
    }

    bool SetFloat(const char* name, float value) const {
        return m_api && m_api->set_post_process_float &&
               m_api->set_post_process_float(m_api->user, name, value);
    }

    bool GetFloat(const char* name, float& value) const {
        return m_api && m_api->get_post_process_float &&
               m_api->get_post_process_float(m_api->user, name, &value);
    }

    bool SetInt(const char* name, int value) const {
        return m_api && m_api->set_post_process_int &&
               m_api->set_post_process_int(m_api->user, name, value);
    }

    bool GetInt(const char* name, int& value) const {
        return m_api && m_api->get_post_process_int &&
               m_api->get_post_process_int(m_api->user, name, &value);
    }

    bool SetVec3(const char* name, Vec3 value) const {
        return m_api && m_api->set_post_process_vec3 &&
               m_api->set_post_process_vec3(m_api->user, name, value);
    }

    bool GetVec3(const char* name, Vec3& value) const {
        return m_api && m_api->get_post_process_vec3 &&
               m_api->get_post_process_vec3(m_api->user, name, &value);
    }

    bool SetEnabled(bool value) const {
        return m_api && m_api->set_post_process_enabled &&
               m_api->set_post_process_enabled(m_api->user, value);
    }

    bool Enabled() const {
        return m_api && m_api->get_post_process_enabled &&
               m_api->get_post_process_enabled(m_api->user);
    }

private:
    const GaleshiScriptApi* m_api = nullptr;
};

class UI {
public:
    explicit UI(const GaleshiScriptApi* api = nullptr) : m_api(api) {}

    uint32_t FindCanvas(const char* canvasId) const {
        return m_api && m_api->find_ui_canvas
            ? m_api->find_ui_canvas(m_api->user, canvasId)
            : UINT32_MAX;
    }

    uint32_t CreateCanvas(const char* canvasId,
                          const char* title = "UI") const {
        return m_api && m_api->create_ui_canvas
            ? m_api->create_ui_canvas(m_api->user, canvasId, title)
            : UINT32_MAX;
    }

    bool RemoveCanvas(uint32_t canvasEntity) const {
        return m_api && m_api->remove_ui_canvas &&
               m_api->remove_ui_canvas(m_api->user, canvasEntity);
    }

    bool SetCanvasFlag(uint32_t canvasEntity, const char* flag,
                       bool value) const {
        return m_api && m_api->set_ui_canvas_flag &&
               m_api->set_ui_canvas_flag(m_api->user, canvasEntity,
                                         flag, value);
    }

    bool GetCanvasFlag(uint32_t canvasEntity, const char* flag,
                       bool& value) const {
        return m_api && m_api->get_ui_canvas_flag &&
               m_api->get_ui_canvas_flag(m_api->user, canvasEntity,
                                         flag, &value);
    }

    bool SetCanvasFloat(uint32_t canvasEntity, const char* property,
                        float value) const {
        return m_api && m_api->set_ui_canvas_float &&
               m_api->set_ui_canvas_float(m_api->user, canvasEntity,
                                          property, value);
    }

    bool GetCanvasFloat(uint32_t canvasEntity, const char* property,
                        float& value) const {
        return m_api && m_api->get_ui_canvas_float &&
               m_api->get_ui_canvas_float(m_api->user, canvasEntity,
                                          property, &value);
    }

    bool SetCanvasString(uint32_t canvasEntity, const char* property,
                         const char* value) const {
        return m_api && m_api->set_ui_canvas_string &&
               m_api->set_ui_canvas_string(m_api->user, canvasEntity,
                                           property, value);
    }

    const char* GetCanvasString(uint32_t canvasEntity,
                                const char* property) const {
        return m_api && m_api->get_ui_canvas_string
            ? m_api->get_ui_canvas_string(m_api->user, canvasEntity,
                                          property)
            : "";
    }

    bool SetCanvasVec2(uint32_t canvasEntity, const char* property,
                       Vec2 value) const {
        return m_api && m_api->set_ui_canvas_vec2 &&
               m_api->set_ui_canvas_vec2(m_api->user, canvasEntity,
                                         property, value);
    }

    bool GetCanvasVec2(uint32_t canvasEntity, const char* property,
                       Vec2& value) const {
        return m_api && m_api->get_ui_canvas_vec2 &&
               m_api->get_ui_canvas_vec2(m_api->user, canvasEntity,
                                         property, &value);
    }

    int32_t FindWidget(uint32_t canvasEntity,
                       const char* widgetId) const {
        return m_api && m_api->find_ui_widget
            ? m_api->find_ui_widget(m_api->user, canvasEntity,
                                    widgetId)
            : -1;
    }

    int32_t AddWidget(uint32_t canvasEntity, const char* widgetId,
                      const char* type = "panel") const {
        return m_api && m_api->add_ui_widget
            ? m_api->add_ui_widget(m_api->user, canvasEntity,
                                   widgetId, type)
            : -1;
    }

    bool RemoveWidget(uint32_t canvasEntity,
                      const char* widgetId) const {
        return m_api && m_api->remove_ui_widget &&
               m_api->remove_ui_widget(m_api->user, canvasEntity,
                                       widgetId);
    }

    bool SetWidgetFlag(uint32_t canvasEntity, const char* widgetId,
                       const char* property, bool value) const {
        return m_api && m_api->set_ui_widget_flag &&
               m_api->set_ui_widget_flag(m_api->user, canvasEntity,
                                         widgetId, property, value);
    }

    bool GetWidgetFlag(uint32_t canvasEntity, const char* widgetId,
                       const char* property, bool& value) const {
        return m_api && m_api->get_ui_widget_flag &&
               m_api->get_ui_widget_flag(m_api->user, canvasEntity,
                                         widgetId, property, &value);
    }

    bool SetWidgetFloat(uint32_t canvasEntity, const char* widgetId,
                        const char* property, float value) const {
        return m_api && m_api->set_ui_widget_float &&
               m_api->set_ui_widget_float(m_api->user, canvasEntity,
                                          widgetId, property, value);
    }

    bool GetWidgetFloat(uint32_t canvasEntity, const char* widgetId,
                        const char* property, float& value) const {
        return m_api && m_api->get_ui_widget_float &&
               m_api->get_ui_widget_float(m_api->user, canvasEntity,
                                          widgetId, property, &value);
    }

    bool SetWidgetInt(uint32_t canvasEntity, const char* widgetId,
                      const char* property, int value) const {
        return m_api && m_api->set_ui_widget_int &&
               m_api->set_ui_widget_int(m_api->user, canvasEntity,
                                        widgetId, property, value);
    }

    bool GetWidgetInt(uint32_t canvasEntity, const char* widgetId,
                      const char* property, int& value) const {
        return m_api && m_api->get_ui_widget_int &&
               m_api->get_ui_widget_int(m_api->user, canvasEntity,
                                        widgetId, property, &value);
    }

    bool SetWidgetString(uint32_t canvasEntity, const char* widgetId,
                         const char* property, const char* value) const {
        return m_api && m_api->set_ui_widget_string &&
               m_api->set_ui_widget_string(m_api->user, canvasEntity,
                                           widgetId, property, value);
    }

    const char* GetWidgetString(uint32_t canvasEntity,
                                const char* widgetId,
                                const char* property) const {
        return m_api && m_api->get_ui_widget_string
            ? m_api->get_ui_widget_string(m_api->user, canvasEntity,
                                          widgetId, property)
            : "";
    }

    bool SetWidgetVec2(uint32_t canvasEntity, const char* widgetId,
                       const char* property, Vec2 value) const {
        return m_api && m_api->set_ui_widget_vec2 &&
               m_api->set_ui_widget_vec2(m_api->user, canvasEntity,
                                         widgetId, property, value);
    }

    bool GetWidgetVec2(uint32_t canvasEntity, const char* widgetId,
                       const char* property, Vec2& value) const {
        return m_api && m_api->get_ui_widget_vec2 &&
               m_api->get_ui_widget_vec2(m_api->user, canvasEntity,
                                         widgetId, property, &value);
    }

    bool SetWidgetVec4(uint32_t canvasEntity, const char* widgetId,
                       const char* property, Vec4 value) const {
        return m_api && m_api->set_ui_widget_vec4 &&
               m_api->set_ui_widget_vec4(m_api->user, canvasEntity,
                                         widgetId, property, value);
    }

    bool GetWidgetVec4(uint32_t canvasEntity, const char* widgetId,
                       const char* property, Vec4& value) const {
        return m_api && m_api->get_ui_widget_vec4 &&
               m_api->get_ui_widget_vec4(m_api->user, canvasEntity,
                                         widgetId, property, &value);
    }

    // Common convenience aliases.
    bool SetWidgetVisible(uint32_t canvasEntity, const char* widgetId,
                          bool value) const {
        return SetWidgetFlag(canvasEntity, widgetId, "visible", value);
    }

    bool SetWidgetLabel(uint32_t canvasEntity, const char* widgetId,
                        const char* value) const {
        return SetWidgetString(canvasEntity, widgetId, "label", value);
    }

    bool SetWidgetValue(uint32_t canvasEntity, const char* widgetId,
                        float value) const {
        return SetWidgetFloat(canvasEntity, widgetId, "value", value);
    }

    bool PollEvent(GaleshiScriptUIEvent& event) const {
        return m_api && m_api->poll_ui_event &&
               m_api->poll_ui_event(m_api->user, &event);
    }

    bool PointerOverUI() const {
        return m_api && m_api->is_pointer_over_ui &&
               m_api->is_pointer_over_ui(m_api->user);
    }

    const char* TextValue(uint32_t canvasEntity, const char* canvasId,
                          const char* widgetId) const {
        return m_api && m_api->get_ui_text
            ? m_api->get_ui_text(m_api->user, canvasEntity, canvasId,
                                 widgetId)
            : "";
    }

private:
    const GaleshiScriptApi* m_api = nullptr;
};

enum class LandscapeTool : int {
    Raise = 0,
    Lower = 1,
    Smooth = 2,
    Flatten = 3,
    Noise = 4,
    Terrace = 5,
    Sharpen = 6,
};

class Landscape {
public:
    explicit Landscape(const GaleshiScriptApi* api = nullptr)
        : m_api(api) {}

    uint32_t Find() const {
        return m_api && m_api->find_landscape
            ? m_api->find_landscape(m_api->user)
            : UINT32_MAX;
    }

    float SampleHeight(float localX, float localZ) const {
        return m_api && m_api->landscape_sample_height
            ? m_api->landscape_sample_height(
                  m_api->user, localX, localZ)
            : 0.0f;
    }

    bool SetHeight(float localX, float localZ, float height) const {
        return m_api && m_api->landscape_set_height &&
               m_api->landscape_set_height(
                   m_api->user, localX, localZ, height);
    }

    bool ApplyBrush(LandscapeTool tool, float localX, float localZ,
                    float radius, float strength = 3.0f,
                    float falloff = 0.65f, float dt = 1.0f) const {
        return m_api && m_api->landscape_apply_brush &&
               m_api->landscape_apply_brush(
                   m_api->user, static_cast<int>(tool), localX,
                   localZ, radius, strength, falloff, dt);
    }

    bool ResetHeights() const {
        return m_api && m_api->landscape_reset_heights &&
               m_api->landscape_reset_heights(m_api->user);
    }

    uint32_t LayerCount() const {
        return m_api && m_api->landscape_layer_count
            ? m_api->landscape_layer_count(m_api->user) : 0;
    }
    bool AddLayer() const {
        return m_api && m_api->landscape_add_layer &&
               m_api->landscape_add_layer(m_api->user);
    }
    bool RemoveLayer(int layer) const {
        return m_api && m_api->landscape_remove_layer &&
               m_api->landscape_remove_layer(m_api->user, layer);
    }
    int ActiveLayer() const {
        return m_api && m_api->landscape_active_layer
            ? m_api->landscape_active_layer(m_api->user) : -1;
    }
    bool SetActiveLayer(int layer) const {
        return m_api && m_api->landscape_set_active_layer &&
               m_api->landscape_set_active_layer(m_api->user, layer);
    }
    bool SetLayerTexture(int layer, const char* slot,
                         const char* path) const {
        return m_api && m_api->landscape_set_layer_texture &&
               m_api->landscape_set_layer_texture(
                   m_api->user, layer, slot, path);
    }
    const char* GetLayerTexture(int layer, const char* slot) const {
        return m_api && m_api->landscape_get_layer_texture
            ? m_api->landscape_get_layer_texture(
                  m_api->user, layer, slot)
            : "";
    }
    bool PaintLayer(int layer, float localX, float localZ,
                    float radius, float strength = 3.0f,
                    float falloff = 0.65f, float dt = 0.016f,
                    bool erase = false) const {
        return m_api && m_api->landscape_paint_layer &&
               m_api->landscape_paint_layer(
                   m_api->user, layer, localX, localZ, radius, strength,
                   falloff, dt, erase);
    }
    bool Bake() const {
        return m_api && m_api->landscape_bake &&
               m_api->landscape_bake(m_api->user);
    }

private:
    const GaleshiScriptApi* m_api = nullptr;
};

class Vegetation {
public:
    explicit Vegetation(const GaleshiScriptApi* api = nullptr)
        : m_api(api) {}

    uint32_t Find() const {
        return m_api && m_api->find_vegetation
            ? m_api->find_vegetation(m_api->user)
            : UINT32_MAX;
    }

    bool Regenerate(uint32_t entity) const {
        return m_api && m_api->vegetation_regenerate &&
               m_api->vegetation_regenerate(m_api->user, entity);
    }

    bool Clear(uint32_t entity) const {
        return m_api && m_api->vegetation_clear &&
               m_api->vegetation_clear(m_api->user, entity);
    }

    bool SetBool(uint32_t entity, const char* property,
                 bool value) const {
        return m_api && m_api->vegetation_set_bool &&
               m_api->vegetation_set_bool(
                   m_api->user, entity, property, value);
    }

    bool GetBool(uint32_t entity, const char* property,
                 bool& value) const {
        return m_api && m_api->vegetation_get_bool &&
               m_api->vegetation_get_bool(
                   m_api->user, entity, property, &value);
    }

    bool SetInt(uint32_t entity, const char* property,
                int value) const {
        return m_api && m_api->vegetation_set_int &&
               m_api->vegetation_set_int(
                   m_api->user, entity, property, value);
    }

    bool GetInt(uint32_t entity, const char* property,
                int& value) const {
        return m_api && m_api->vegetation_get_int &&
               m_api->vegetation_get_int(
                   m_api->user, entity, property, &value);
    }

    bool SetFloat(uint32_t entity, const char* property,
                  float value) const {
        return m_api && m_api->vegetation_set_float &&
               m_api->vegetation_set_float(
                   m_api->user, entity, property, value);
    }

    bool GetFloat(uint32_t entity, const char* property,
                  float& value) const {
        return m_api && m_api->vegetation_get_float &&
               m_api->vegetation_get_float(
                   m_api->user, entity, property, &value);
    }

    int32_t Count(uint32_t entity, bool grass = false,
                  bool painted = false) const {
        return m_api && m_api->vegetation_instance_count
            ? m_api->vegetation_instance_count(
                  m_api->user, entity, grass, painted)
            : 0;
    }

    bool SetEnabled(uint32_t entity, bool value) const {
        return SetBool(entity, "enabled", value);
    }

    bool SetSeed(uint32_t entity, int value) const {
        return SetInt(entity, "seed", value);
    }

private:
    const GaleshiScriptApi* m_api = nullptr;
};

class AI {
public:
    explicit AI(const GaleshiScriptApi* api = nullptr) : m_api(api) {}

    uint32_t FindNavAgent() const {
        return m_api && m_api->find_nav_agent
            ? m_api->find_nav_agent(m_api->user) : UINT32_MAX;
    }
    uint32_t FindPerception() const {
        return m_api && m_api->find_perception
            ? m_api->find_perception(m_api->user) : UINT32_MAX;
    }
    uint32_t FindBehaviorTree() const {
        return m_api && m_api->find_behavior_tree
            ? m_api->find_behavior_tree(m_api->user) : UINT32_MAX;
    }
    uint32_t FindController() const {
        return m_api && m_api->find_ai_controller
            ? m_api->find_ai_controller(m_api->user) : UINT32_MAX;
    }

    bool SetFlag(uint32_t entity, const char* component,
                 const char* property, bool value) const {
        return m_api && m_api->set_ai_flag &&
               m_api->set_ai_flag(m_api->user, entity, component,
                                  property, value);
    }
    bool GetFlag(uint32_t entity, const char* component,
                 const char* property, bool& value) const {
        return m_api && m_api->get_ai_flag &&
               m_api->get_ai_flag(m_api->user, entity, component,
                                  property, &value);
    }
    bool SetFloat(uint32_t entity, const char* component,
                  const char* property, float value) const {
        return m_api && m_api->set_ai_float &&
               m_api->set_ai_float(m_api->user, entity, component,
                                   property, value);
    }
    bool GetFloat(uint32_t entity, const char* component,
                  const char* property, float& value) const {
        return m_api && m_api->get_ai_float &&
               m_api->get_ai_float(m_api->user, entity, component,
                                   property, &value);
    }
    bool SetInt(uint32_t entity, const char* component,
                const char* property, int value) const {
        return m_api && m_api->set_ai_int &&
               m_api->set_ai_int(m_api->user, entity, component,
                                 property, value);
    }
    bool GetInt(uint32_t entity, const char* component,
                const char* property, int& value) const {
        return m_api && m_api->get_ai_int &&
               m_api->get_ai_int(m_api->user, entity, component,
                                 property, &value);
    }
    bool SetVec3(uint32_t entity, const char* component,
                 const char* property, Vec3 value) const {
        return m_api && m_api->set_ai_vec3 &&
               m_api->set_ai_vec3(m_api->user, entity, component,
                                  property, value);
    }
    bool GetVec3(uint32_t entity, const char* component,
                 const char* property, Vec3& value) const {
        return m_api && m_api->get_ai_vec3 &&
               m_api->get_ai_vec3(m_api->user, entity, component,
                                  property, &value);
    }
    bool SetString(uint32_t entity, const char* component,
                   const char* property, const char* value) const {
        return m_api && m_api->set_ai_string &&
               m_api->set_ai_string(m_api->user, entity, component,
                                    property, value);
    }
    const char* GetString(uint32_t entity, const char* component,
                          const char* property) const {
        return m_api && m_api->get_ai_string
            ? m_api->get_ai_string(m_api->user, entity, component,
                                   property)
            : "";
    }

    bool CreateDefaultBehaviorTree(uint32_t entity) const {
        return m_api && m_api->behavior_tree_create_default &&
               m_api->behavior_tree_create_default(m_api->user, entity);
    }
    uint32_t NodeCount(uint32_t entity) const {
        return m_api && m_api->behavior_tree_node_count
            ? m_api->behavior_tree_node_count(m_api->user, entity) : 0;
    }
    int32_t AddNode(uint32_t entity, const char* name, int type,
                    int condition, int action) const {
        return m_api && m_api->behavior_tree_add_node
            ? m_api->behavior_tree_add_node(
                  m_api->user, entity, name, type, condition, action)
            : -1;
    }
    bool AddChild(uint32_t entity, int parent, int child) const {
        return m_api && m_api->behavior_tree_add_child &&
               m_api->behavior_tree_add_child(
                   m_api->user, entity, parent, child);
    }
    bool SetRoot(uint32_t entity, int root) const {
        return m_api && m_api->behavior_tree_set_root &&
               m_api->behavior_tree_set_root(m_api->user, entity, root);
    }
    bool SetNodeFloat(uint32_t entity, int index, const char* property,
                      float value) const {
        return m_api && m_api->behavior_tree_set_node_float &&
               m_api->behavior_tree_set_node_float(
                   m_api->user, entity, index, property, value);
    }
    bool SetNodeInt(uint32_t entity, int index, const char* property,
                    int value) const {
        return m_api && m_api->behavior_tree_set_node_int &&
               m_api->behavior_tree_set_node_int(
                   m_api->user, entity, index, property, value);
    }
    bool SetNodeString(uint32_t entity, int index,
                       const char* property, const char* value) const {
        return m_api && m_api->behavior_tree_set_node_string &&
               m_api->behavior_tree_set_node_string(
                   m_api->user, entity, index, property, value);
    }
    const char* Status(uint32_t entity) const {
        return m_api && m_api->behavior_tree_status
            ? m_api->behavior_tree_status(m_api->user, entity) : "";
    }

private:
    const GaleshiScriptApi* m_api = nullptr;
};

class MaterialAccess {
public:
    explicit MaterialAccess(const GaleshiScriptApi* api = nullptr)
        : m_api(api) {}

    bool SetFloat(uint32_t entity, const char* property,
                  float value) const {
        return m_api && m_api->set_material_float &&
               m_api->set_material_float(m_api->user, entity, property,
                                         value);
    }
    bool GetFloat(uint32_t entity, const char* property,
                  float& value) const {
        return m_api && m_api->get_material_float &&
               m_api->get_material_float(m_api->user, entity, property,
                                         &value);
    }
    bool SetFlag(uint32_t entity, const char* property,
                 bool value) const {
        return m_api && m_api->set_material_flag &&
               m_api->set_material_flag(m_api->user, entity, property,
                                        value);
    }
    bool GetFlag(uint32_t entity, const char* property,
                 bool& value) const {
        return m_api && m_api->get_material_flag &&
               m_api->get_material_flag(m_api->user, entity, property,
                                        &value);
    }
    bool SetColor(uint32_t entity, const char* property,
                  Vec4 value) const {
        return m_api && m_api->set_material_color &&
               m_api->set_material_color(m_api->user, entity, property,
                                         value);
    }
    bool GetColor(uint32_t entity, const char* property,
                  Vec4& value) const {
        return m_api && m_api->get_material_color &&
               m_api->get_material_color(m_api->user, entity, property,
                                         &value);
    }
    bool SetTexture(uint32_t entity, const char* slot,
                    const char* path) const {
        return m_api && m_api->set_material_texture &&
               m_api->set_material_texture(m_api->user, entity, slot,
                                           path);
    }
    const char* GetTexture(uint32_t entity, const char* slot) const {
        return m_api && m_api->get_material_texture
            ? m_api->get_material_texture(m_api->user, entity, slot)
            : "";
    }

private:
    const GaleshiScriptApi* m_api = nullptr;
};

class Rendering {
public:
    explicit Rendering(const GaleshiScriptApi* api = nullptr)
        : m_api(api) {}

    uint32_t Width() const {
        return m_api && m_api->renderer_width
            ? m_api->renderer_width(m_api->user) : 0;
    }
    uint32_t Height() const {
        return m_api && m_api->renderer_height
            ? m_api->renderer_height(m_api->user) : 0;
    }
    bool Deferred() const {
        return m_api && m_api->renderer_deferred &&
               m_api->renderer_deferred(m_api->user);
    }
    bool SetDeferred(bool value) const {
        return m_api && m_api->set_renderer_deferred &&
               m_api->set_renderer_deferred(m_api->user, value);
    }
    bool Lod() const {
        return m_api && m_api->renderer_lod &&
               m_api->renderer_lod(m_api->user);
    }
    bool SetLod(bool value) const {
        return m_api && m_api->set_renderer_lod &&
               m_api->set_renderer_lod(m_api->user, value);
    }
    bool Occlusion() const {
        return m_api && m_api->renderer_occlusion &&
               m_api->renderer_occlusion(m_api->user);
    }
    bool SetOcclusion(bool value) const {
        return m_api && m_api->set_renderer_occlusion &&
               m_api->set_renderer_occlusion(m_api->user, value);
    }

private:
    const GaleshiScriptApi* m_api = nullptr;
};

class Animation {
public:
    explicit Animation(const GaleshiScriptApi* api = nullptr)
        : m_api(api) {}

    bool SetState(uint32_t entity, const char* state) const {
        return m_api && m_api->set_animation_state &&
               m_api->set_animation_state(m_api->user, entity, state);
    }

    bool SetParameter(uint32_t entity, const char* name,
                      float value) const {
        return m_api && m_api->set_animation_parameter &&
               m_api->set_animation_parameter(
                   m_api->user, entity, name, value);
    }

    float GetParameter(uint32_t entity, const char* name) const {
        return m_api && m_api->get_animation_parameter
            ? m_api->get_animation_parameter(m_api->user, entity, name)
            : 0.0f;
    }

    bool Trigger(uint32_t entity, const char* name) const {
        return m_api && m_api->trigger_animation_parameter &&
               m_api->trigger_animation_parameter(
                   m_api->user, entity, name);
    }

    bool PlayClip(uint32_t entity, int clip) const {
        return m_api && m_api->play_animation_clip &&
               m_api->play_animation_clip(m_api->user, entity, clip);
    }

    bool Stop(uint32_t entity) const {
        return m_api && m_api->stop_animation &&
               m_api->stop_animation(m_api->user, entity);
    }

    bool SetSpeed(uint32_t entity, float speed) const {
        return m_api && m_api->set_animation_speed &&
               m_api->set_animation_speed(m_api->user, entity, speed);
    }

    float GetSpeed(uint32_t entity) const {
        return m_api && m_api->get_animation_speed
            ? m_api->get_animation_speed(m_api->user, entity)
            : 1.0f;
    }

    uint32_t ClipCount(uint32_t entity) const {
        return m_api && m_api->animation_clip_count
            ? m_api->animation_clip_count(m_api->user, entity)
            : 0;
    }

    const char* ClipName(uint32_t entity, uint32_t index) const {
        return m_api && m_api->animation_clip_name
            ? m_api->animation_clip_name(m_api->user, entity, index)
            : "";
    }

    float ClipDuration(uint32_t entity, uint32_t index) const {
        return m_api && m_api->animation_clip_duration
            ? m_api->animation_clip_duration(m_api->user, entity, index)
            : 0.0f;
    }

    uint32_t SpawnModel(const char* path, const char* name,
                        Vec3 position = {}) const {
        return m_api && m_api->spawn_skeletal_model
            ? m_api->spawn_skeletal_model(
                  m_api->user, path, name, position.x, position.y,
                  position.z)
            : UINT32_MAX;
    }

    bool ApplyModel(uint32_t entity, const char* path) const {
        return m_api && m_api->apply_skeletal_model &&
               m_api->apply_skeletal_model(m_api->user, entity, path);
    }

private:
    const GaleshiScriptApi* m_api = nullptr;
};

class Assets {
public:
    explicit Assets(const GaleshiScriptApi* api = nullptr) : m_api(api) {}

    bool Exists(const char* path) const {
        return m_api && m_api->asset_exists &&
               m_api->asset_exists(m_api->user, path);
    }

    const char* ResolvePath(const char* path) const {
        return m_api && m_api->asset_resolve_path
            ? m_api->asset_resolve_path(m_api->user, path)
            : "";
    }

    bool LoadModel(const char* path) const {
        return m_api && m_api->asset_load_model &&
               m_api->asset_load_model(m_api->user, path);
    }

private:
    const GaleshiScriptApi* m_api = nullptr;
};

class Particles {
public:
    explicit Particles(const GaleshiScriptApi* api = nullptr)
        : m_api(api) {}

    uint32_t Find() const {
        return m_api && m_api->find_particle_emitter
            ? m_api->find_particle_emitter(m_api->user)
            : UINT32_MAX;
    }

    bool SetFlag(uint32_t entity, const char* property,
                 bool value) const {
        return m_api && m_api->set_particle_flag &&
               m_api->set_particle_flag(m_api->user, entity, property,
                                        value);
    }
    bool GetFlag(uint32_t entity, const char* property,
                 bool& value) const {
        return m_api && m_api->get_particle_flag &&
               m_api->get_particle_flag(m_api->user, entity, property,
                                        &value);
    }
    bool SetFloat(uint32_t entity, const char* property,
                  float value) const {
        return m_api && m_api->set_particle_float &&
               m_api->set_particle_float(m_api->user, entity, property,
                                         value);
    }
    bool GetFloat(uint32_t entity, const char* property,
                  float& value) const {
        return m_api && m_api->get_particle_float &&
               m_api->get_particle_float(m_api->user, entity, property,
                                         &value);
    }
    bool SetInt(uint32_t entity, const char* property,
                int value) const {
        return m_api && m_api->set_particle_int &&
               m_api->set_particle_int(m_api->user, entity, property,
                                       value);
    }
    bool GetInt(uint32_t entity, const char* property,
                int& value) const {
        return m_api && m_api->get_particle_int &&
               m_api->get_particle_int(m_api->user, entity, property,
                                       &value);
    }
    bool SetVec3(uint32_t entity, const char* property,
                 Vec3 value) const {
        return m_api && m_api->set_particle_vec3 &&
               m_api->set_particle_vec3(m_api->user, entity, property,
                                        value);
    }
    bool GetVec3(uint32_t entity, const char* property,
                 Vec3& value) const {
        return m_api && m_api->get_particle_vec3 &&
               m_api->get_particle_vec3(m_api->user, entity, property,
                                        &value);
    }
    bool SetVec4(uint32_t entity, const char* property,
                 Vec4 value) const {
        return m_api && m_api->set_particle_vec4 &&
               m_api->set_particle_vec4(m_api->user, entity, property,
                                        value);
    }
    bool GetVec4(uint32_t entity, const char* property,
                 Vec4& value) const {
        return m_api && m_api->get_particle_vec4 &&
               m_api->get_particle_vec4(m_api->user, entity, property,
                                        &value);
    }

private:
    const GaleshiScriptApi* m_api = nullptr;
};

class Groom {
public:
    explicit Groom(const GaleshiScriptApi* api = nullptr) : m_api(api) {}

    uint32_t Find() const {
        return m_api && m_api->find_groom
            ? m_api->find_groom(m_api->user)
            : UINT32_MAX;
    }

    bool SetFlag(uint32_t entity, const char* property,
                 bool value) const {
        return m_api && m_api->set_groom_flag &&
               m_api->set_groom_flag(m_api->user, entity, property,
                                     value);
    }
    bool GetFlag(uint32_t entity, const char* property,
                 bool& value) const {
        return m_api && m_api->get_groom_flag &&
               m_api->get_groom_flag(m_api->user, entity, property,
                                     &value);
    }
    bool SetFloat(uint32_t entity, const char* property,
                  float value) const {
        return m_api && m_api->set_groom_float &&
               m_api->set_groom_float(m_api->user, entity, property,
                                      value);
    }
    bool GetFloat(uint32_t entity, const char* property,
                  float& value) const {
        return m_api && m_api->get_groom_float &&
               m_api->get_groom_float(m_api->user, entity, property,
                                      &value);
    }
    bool SetInt(uint32_t entity, const char* property,
                int value) const {
        return m_api && m_api->set_groom_int &&
               m_api->set_groom_int(m_api->user, entity, property,
                                    value);
    }
    bool GetInt(uint32_t entity, const char* property,
                int& value) const {
        return m_api && m_api->get_groom_int &&
               m_api->get_groom_int(m_api->user, entity, property,
                                    &value);
    }
    bool SetVec3(uint32_t entity, const char* property,
                 Vec3 value) const {
        return m_api && m_api->set_groom_vec3 &&
               m_api->set_groom_vec3(m_api->user, entity, property,
                                     value);
    }
    bool GetVec3(uint32_t entity, const char* property,
                 Vec3& value) const {
        return m_api && m_api->get_groom_vec3 &&
               m_api->get_groom_vec3(m_api->user, entity, property,
                                     &value);
    }
    bool SetString(uint32_t entity, const char* property,
                   const char* value) const {
        return m_api && m_api->set_groom_string &&
               m_api->set_groom_string(m_api->user, entity, property,
                                       value);
    }
    const char* GetString(uint32_t entity, const char* property) const {
        return m_api && m_api->get_groom_string
            ? m_api->get_groom_string(m_api->user, entity, property)
            : "";
    }

private:
    const GaleshiScriptApi* m_api = nullptr;
};

class Camera {
public:
    explicit Camera(const GaleshiScriptApi* api = nullptr) : m_api(api) {}

    bool SetMode(int mode) const {
        return m_api && m_api->set_camera_mode &&
               m_api->set_camera_mode(m_api->user, mode);
    }

    int Mode() const {
        return m_api && m_api->get_camera_mode
            ? m_api->get_camera_mode(m_api->user)
            : GALESHI_CAMERA_THIRD_PERSON;
    }

    Vec3 Forward() const {
        return m_api && m_api->get_camera_forward
            ? m_api->get_camera_forward(m_api->user)
            : Vec3{0.0f, 0.0f, -1.0f};
    }

    Vec3 Right() const {
        return m_api && m_api->get_camera_right
            ? m_api->get_camera_right(m_api->user)
            : Vec3{1.0f, 0.0f, 0.0f};
    }

    Vec3 Movement(float horizontal, float vertical) const {
        return m_api && m_api->get_camera_movement
            ? m_api->get_camera_movement(m_api->user, horizontal, vertical)
            : Vec3{};
    }

private:
    const GaleshiScriptApi* m_api = nullptr;
};

class Player {
public:
    explicit Player(const GaleshiScriptApi* api = nullptr) : m_api(api) {}

    bool Strafing() const {
        return m_api && m_api->get_player_strafing &&
               m_api->get_player_strafing(m_api->user);
    }

    void SetStrafing(bool enabled) const {
        if (m_api && m_api->set_player_strafing) {
            m_api->set_player_strafing(m_api->user, enabled);
        }
    }

private:
    const GaleshiScriptApi* m_api = nullptr;
};

class Shader {
public:
    explicit Shader(const GaleshiScriptApi* api = nullptr) : m_api(api) {}

    bool Compile(const char* path) const {
        return m_api && m_api->compile_shader &&
               m_api->compile_shader(m_api->user, path);
    }

    const char* Error() const {
        return m_api && m_api->get_shader_error
            ? m_api->get_shader_error(m_api->user)
            : "";
    }

private:
    const GaleshiScriptApi* m_api = nullptr;
};

class Api {
public:
    explicit Api(const GaleshiScriptApi* api = nullptr)
        : m_api(api),
          world(api),
          input(api),
          physics(api),
          navigation(api),
          audio(api),
          postProcess(api),
          ui(api),
          landscape(api),
          vegetation(api),
          ai(api),
          materials(api),
          rendering(api),
          animation(api),
          assets(api),
          particles(api),
          groom(api),
          camera(api),
          player(api),
          shader(api) {}

    bool Valid() const { return m_api != nullptr; }

    uint32_t AbiVersion() const {
        return m_api ? m_api->abi_version : 0;
    }

    bool Compatible() const {
        return m_api && m_api->abi_version >= 1 &&
               m_api->struct_size >= 1;
    }

    void Log(const char* message) const {
        if (m_api && m_api->log) {
            m_api->log(m_api->user, message);
        }
    }

    const GaleshiScriptApi* Raw() const { return m_api; }
    void* User() const { return m_api ? m_api->user : nullptr; }

    World world;
    Input input;
    Physics physics;
    Navigation navigation;
    Audio audio;
    PostProcess postProcess;
    UI ui;
    Landscape landscape;
    Vegetation vegetation;
    AI ai;
    MaterialAccess materials;
    Rendering rendering;
    Animation animation;
    Assets assets;
    Particles particles;
    Groom groom;
    Camera camera;
    Player player;
    Shader shader;

private:
    const GaleshiScriptApi* m_api = nullptr;
};

} // namespace Galeshi::Script

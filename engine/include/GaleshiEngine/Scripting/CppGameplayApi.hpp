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

    bool HasComponent(uint32_t entity, const char* component) const {
        return m_api && m_api->has_component &&
               m_api->has_component(m_api->user, entity, component);
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
    Camera camera;
    Player player;
    Shader shader;

private:
    const GaleshiScriptApi* m_api = nullptr;
};

} // namespace Galeshi::Script

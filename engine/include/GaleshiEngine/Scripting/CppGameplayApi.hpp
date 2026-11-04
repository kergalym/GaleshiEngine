#pragma once

#include "GaleshiEngine/Scripting/CppScriptApi.hpp"

#include <cstdint>

// Thin C++ class wrappers around the stable GaleshiScriptApi C ABI.
//
// Hot-reloaded script modules should use these classes instead of calling
// the function pointers directly. The wrappers are header-only and do not
// link the engine, preserving the worker ABI.
namespace Galeshi::Script {

using Vec3 = GaleshiScriptVec3;

class World {
public:
    explicit World(const GaleshiScriptApi* api = nullptr) : m_api(api) {}

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
    Camera camera;
    Player player;
    Shader shader;

private:
    const GaleshiScriptApi* m_api = nullptr;
};

} // namespace Galeshi::Script

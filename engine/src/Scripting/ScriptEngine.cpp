#include "GaleshiEngine/Scripting/ScriptEngine.hpp"
#include "ScriptEngineInternal.hpp"
#include "GaleshiEngine/Core/ProjectManager.hpp"
#include "GaleshiEngine/Scene/Scene.hpp"
#include "GaleshiEngine/Scene/Components.hpp"
#include "GaleshiEngine/AI/BehaviorTree.hpp"
#include "GaleshiEngine/Animation/AnimationSystem.hpp"
#include "GaleshiEngine/Assets/AssetManager.hpp"
#include "GaleshiEngine/Assets/LandscapeMesh.hpp"
#include "GaleshiEngine/Assets/Material.hpp"
#include "GaleshiEngine/Assets/Mesh.hpp"
#include "GaleshiEngine/Assets/VegetationGenerator.hpp"
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <utility>

#if defined(__unix__) || defined(__APPLE__)
#include <poll.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/prctl.h>
#endif
#endif

namespace Galeshi {

using namespace ScriptEngineInternal;


void ScriptEngine::BuildApi() {
    if (m_apiBuilt) return;
    m_apiBuilt = true;

    m_api = m_lua.create_table();
    m_lua["Galeshi"] = m_api;
    m_api["version"] = "0.3.0";

    // ------------------------------------------------------------------
    // Small conversion helpers shared by all modules.
    // ------------------------------------------------------------------
    auto readNumber = [](const sol::object& value, float& out) {
        if (value.get_type() != sol::type::number) return false;
        out = static_cast<float>(value.as<double>());
        return true;
    };

    auto readBool = [](const sol::object& value, bool& out) {
        if (value.get_type() != sol::type::boolean) return false;
        out = value.as<bool>();
        return true;
    };

    auto readString = [](const sol::object& value, std::string& out) {
        if (value.get_type() != sol::type::string) return false;
        out = value.as<std::string>();
        return true;
    };

    auto readVec2 = [readNumber](const sol::object& value, glm::vec2& out) {
        if (value.get_type() != sol::type::table) return false;
        sol::table table = value.as<sol::table>();
        float x = 0.0f;
        float y = 0.0f;
        const sol::object xo = table.get<sol::object>("x");
        const sol::object yo = table.get<sol::object>("y");
        if (!readNumber(xo, x)) {
            const sol::object xi = table.get<sol::object>(1);
            if (!readNumber(xi, x)) return false;
        }
        if (!readNumber(yo, y)) {
            const sol::object yi = table.get<sol::object>(2);
            if (!readNumber(yi, y)) return false;
        }
        out = {x, y};
        return true;
    };

    auto readVec3 = [readNumber](const sol::object& value, glm::vec3& out) {
        if (value.get_type() != sol::type::table) return false;
        sol::table table = value.as<sol::table>();
        float components[3] = {0.0f, 0.0f, 0.0f};
        const char* names[3] = {"x", "y", "z"};
        for (int i = 0; i < 3; ++i) {
            const sol::object named = table.get<sol::object>(names[i]);
            if (!readNumber(named, components[i])) {
                const sol::object indexed = table.get<sol::object>(i + 1);
                if (!readNumber(indexed, components[i])) return false;
            }
        }
        out = {components[0], components[1], components[2]};
        return true;
    };

    auto readVec4 = [readNumber](const sol::object& value, glm::vec4& out) {
        if (value.get_type() != sol::type::table) return false;
        sol::table table = value.as<sol::table>();
        float components[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        const char* names[4] = {"x", "y", "z", "w"};
        for (int i = 0; i < 4; ++i) {
            const sol::object named = table.get<sol::object>(names[i]);
            if (!readNumber(named, components[i])) {
                const sol::object indexed = table.get<sol::object>(i + 1);
                if (!readNumber(indexed, components[i])) {
                    // RGB color helpers may omit alpha; default it to opaque.
                    if (i == 3) {
                        components[3] = 1.0f;
                        continue;
                    }
                    return false;
                }
            }
        }
        out = {components[0], components[1], components[2], components[3]};
        return true;
    };

    auto readQuat = [readNumber](const sol::object& value, glm::quat& out) {
        if (value.get_type() != sol::type::table) return false;
        sol::table table = value.as<sol::table>();
        float components[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        const char* names[4] = {"x", "y", "z", "w"};
        for (int i = 0; i < 4; ++i) {
            const sol::object named = table.get<sol::object>(names[i]);
            if (!readNumber(named, components[i])) {
                const sol::object indexed = table.get<sol::object>(i + 1);
                if (!readNumber(indexed, components[i])) return false;
            }
        }
        out = glm::quat(components[3], components[0], components[1], components[2]);
        return true;
    };

    auto readVec3Args = [readVec3, readNumber](sol::variadic_args args,
                                               glm::vec3& out) {
        if (args.size() == 1) {
            return readVec3(args[0], out);
        }
        if (args.size() < 3) return false;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        if (!readNumber(args[0], x) || !readNumber(args[1], y) ||
            !readNumber(args[2], z)) {
            return false;
        }
        out = {x, y, z};
        return true;
    };

    auto makeVec2 = [this](const glm::vec2& value) {
        sol::table table = m_lua.create_table();
        table[1] = value.x;
        table[2] = value.y;
        table["x"] = value.x;
        table["y"] = value.y;
        return table;
    };

    auto makeVec3 = [this](const glm::vec3& value) {
        sol::table table = m_lua.create_table();
        table[1] = value.x;
        table[2] = value.y;
        table[3] = value.z;
        table["x"] = value.x;
        table["y"] = value.y;
        table["z"] = value.z;
        return table;
    };

    auto makeVec4 = [this](const glm::vec4& value) {
        sol::table table = m_lua.create_table();
        table[1] = value.x;
        table[2] = value.y;
        table[3] = value.z;
        table[4] = value.w;
        table["x"] = value.x;
        table["y"] = value.y;
        table["z"] = value.z;
        table["w"] = value.w;
        return table;
    };

    auto makeQuat = [this](const glm::quat& value) {
        sol::table table = m_lua.create_table();
        table[1] = value.x;
        table[2] = value.y;
        table[3] = value.z;
        table[4] = value.w;
        table["x"] = value.x;
        table["y"] = value.y;
        table["z"] = value.z;
        table["w"] = value.w;
        return table;
    };

    // ------------------------------------------------------------------
    // Logging and math helpers.
    // ------------------------------------------------------------------
    auto printArgs = [this](sol::variadic_args args) {
        std::string out;
        for (auto value : args) {
            if (!out.empty()) out += ' ';
            out += LuaValueToString(value);
        }
        AddLog(out);
    };

    const auto logFn = [this](const std::string& message) { AddLog(message); };
    const auto warnFn = [this](const std::string& message) {
        AddLog(std::string("WARN: ") + message);
        spdlog::warn("[Lua] {}", message);
    };
    const auto errorFn = [this](const std::string& message) {
        AddLog(std::string("ERROR: ") + message);
        spdlog::error("[Lua] {}", message);
    };

    m_api["log"] = logFn;
    m_api["warn"] = warnFn;
    m_api["error"] = errorFn;
    m_api["print"] = printArgs;
    m_lua.set_function("log", logFn);
    m_lua.set_function("print", printArgs);

    m_api["vec2"] = [makeVec2](float x, float y) {
        return makeVec2(glm::vec2{x, y});
    };
    m_api["vec3"] = [makeVec3](float x, float y, float z) {
        return makeVec3(glm::vec3{x, y, z});
    };
    m_api["color"] = [makeVec4](float r, float g, float b,
                                sol::optional<float> a) {
        return makeVec4(glm::vec4{r, g, b, a.value_or(1.0f)});
    };
    m_api["quat"] = [makeQuat](float x, float y, float z,
                               sol::optional<float> w) {
        return makeQuat(glm::quat(w.value_or(1.0f), x, y, z));
    };

    auto fInterp = [](float current, float target, float deltaTime,
                      float speed) {
        return current + (target - current) *
                             InterpFactor(deltaTime, speed);
    };
    auto vInterp = [readVec3, makeVec3](
                       const sol::object& current,
                       const sol::object& target,
                       float deltaTime,
                       float speed) {
        glm::vec3 from{0.0f};
        glm::vec3 to{0.0f};
        if (!readVec3(current, from) || !readVec3(target, to)) {
            return makeVec3(glm::vec3{0.0f});
        }
        const float t = InterpFactor(deltaTime, speed);
        return makeVec3(from + (to - from) * t);
    };

    sol::table math = m_lua.create_table();
    math["FInterp"] = fInterp;
    math["f_interp"] = fInterp;
    math["VInterp"] = vInterp;
    math["v_interp"] = vInterp;
    m_api["math"] = math;
    m_api["FInterp"] = fInterp;
    m_api["f_interp"] = fInterp;
    m_api["VInterp"] = vInterp;
    m_api["v_interp"] = vInterp;
    m_lua.set_function("FInterp", fInterp);
    m_lua.set_function("f_interp", fInterp);
    m_lua.set_function("VInterp", vInterp);
    m_lua.set_function("v_interp", vInterp);

    sol::table world = m_lua.create_table();
    sol::table input = m_lua.create_table();
    sol::table physics = m_lua.create_table();
    sol::table audio = m_lua.create_table();
    sol::table animation = m_lua.create_table();
    sol::table ai = m_lua.create_table();
    sol::table navigation = m_lua.create_table();
    sol::table ui = m_lua.create_table();
    sol::table particles = m_lua.create_table();
    sol::table engine = m_lua.create_table();
    sol::table camera = m_lua.create_table();
    sol::table player = m_lua.create_table();
    sol::table shader = m_lua.create_table();
    m_api["world"] = world;
    m_api["input"] = input;
    m_api["physics"] = physics;
    m_api["audio"] = audio;
    m_api["animation"] = animation;
    m_api["ai"] = ai;
    m_api["navigation"] = navigation;
    m_api["ui"] = ui;
    m_api["particles"] = particles;
    m_api["engine"] = engine;
    m_api["camera"] = camera;
    m_api["player"] = player;
    m_api["shader"] = shader;

    // ------------------------------------------------------------------
    // Entity handle helpers.  Handles are lightweight Lua tables with an
    // `id` field and a shared metatable containing colon-callable methods.
    // ------------------------------------------------------------------
    auto validId = [this](uint32_t id) {
        if (!m_scene) return false;
        return m_scene->Registry().valid(static_cast<entt::entity>(id));
    };

    auto idFromValue = [](const sol::object& value) -> uint32_t {
        if (value.get_type() == sol::type::number) {
            const double number = value.as<double>();
            if (number < 0.0 || number > 4294967295.0) {
                return static_cast<uint32_t>(entt::null);
            }
            return static_cast<uint32_t>(number);
        }
        if (value.get_type() == sol::type::table) {
            sol::table table = value.as<sol::table>();
            const sol::object id = table.get<sol::object>("id");
            if (id.get_type() == sol::type::number) {
                const double number = id.as<double>();
                if (number < 0.0 || number > 4294967295.0) {
                    return static_cast<uint32_t>(entt::null);
                }
                return static_cast<uint32_t>(number);
            }
        }
        return static_cast<uint32_t>(entt::null);
    };

    auto resolve = [this, idFromValue, validId](const sol::object& value) {
        const uint32_t id = idFromValue(value);
        if (!validId(id)) return entt::entity{entt::null};
        return static_cast<entt::entity>(id);
    };

    auto idFromTable = [idFromValue](const sol::table& table) {
        return idFromValue(sol::object(table));
    };

    auto resolveTable = [this, idFromTable, validId](const sol::table& table) {
        const uint32_t id = idFromTable(table);
        if (!validId(id)) return entt::entity{entt::null};
        return static_cast<entt::entity>(id);
    };

    auto makeEntityTable = [this](uint32_t id) {
        sol::table table = m_lua.create_table();
        table["id"] = id;
        table[sol::metatable_key] = m_entityMeta;
        return table;
    };

    auto makeEntity = [this, makeEntityTable, validId](uint32_t id) -> sol::object {
        if (!validId(id)) return sol::make_object(m_lua, sol::nil);
        return sol::make_object(m_lua, makeEntityTable(id));
    };

    auto getTransform = [this](entt::entity entity, bool create) -> TransformComponent* {
        if (!m_scene || !m_scene->Registry().valid(entity)) return nullptr;
        auto& registry = m_scene->Registry();
        if (registry.all_of<TransformComponent>(entity)) {
            return &registry.get<TransformComponent>(entity);
        }
        if (!create) return nullptr;
        return &registry.emplace<TransformComponent>(entity);
    };

    ScriptApiBuildContext context;
    context.readNumber = readNumber;
    context.readBool = readBool;
    context.readString = readString;
    context.readVec2 = readVec2;
    context.readVec3 = readVec3;
    context.readVec4 = readVec4;
    context.readQuat = readQuat;
    context.readVec3Args = readVec3Args;
    context.makeVec2 = makeVec2;
    context.makeVec3 = makeVec3;
    context.makeVec4 = makeVec4;
    context.makeQuat = makeQuat;
    context.logFn = logFn;
    context.warnFn = warnFn;
    context.errorFn = errorFn;
    context.validId = validId;
    context.idFromValue = idFromValue;
    context.resolve = resolve;
    context.idFromTable = idFromTable;
    context.resolveTable = resolveTable;
    context.makeEntityTable = makeEntityTable;
    context.makeEntity = makeEntity;
    context.getTransform = getTransform;
    context.world = world;
    context.input = input;
    context.physics = physics;
    context.audio = audio;
    context.animation = animation;
    context.ai = ai;
    context.navigation = navigation;
    context.ui = ui;
    context.particles = particles;
    context.engine = engine;
    context.camera = camera;
    context.player = player;
    context.shader = shader;

    BuildApiCore(context);
    BuildApiGameplay(context);
    BuildApiUI(context);
    BuildApiRuntime(context);
}

} // namespace Galeshi

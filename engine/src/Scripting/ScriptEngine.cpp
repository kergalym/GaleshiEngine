#include "GaleshiEngine/Scripting/ScriptEngine.hpp"
#include "GaleshiEngine/Scene/Scene.hpp"
#include "GaleshiEngine/Scene/Components.hpp"
#include "GaleshiEngine/Animation/AnimationSystem.hpp"
#include "GaleshiEngine/Assets/AssetManager.hpp"
#include "GaleshiEngine/Assets/Material.hpp"
#include "GaleshiEngine/Assets/Mesh.hpp"
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <limits>
#include <sstream>
#include <utility>

namespace Galeshi {

namespace {

std::string NormalizeKey(std::string value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        if (c == '-' || c == ' ') c = '_';
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

std::string LuaValueToString(const sol::object& value) {
    switch (value.get_type()) {
        case sol::type::nil:
        case sol::type::none:
            return "nil";
        case sol::type::boolean:
            return value.as<bool>() ? "true" : "false";
        case sol::type::number: {
            std::ostringstream out;
            out << value.as<double>();
            return out.str();
        }
        case sol::type::string:
            return value.as<std::string>();
        case sol::type::function:
            return "<function>";
        case sol::type::table:
            return "<table>";
        case sol::type::userdata:
            return "<userdata>";
        case sol::type::thread:
            return "<thread>";
        default:
            return "<value>";
    }
}

int AudioBusIndexFromName(const std::string& name) {
    const std::string key = NormalizeKey(name);
    if (key == "master" || key == "0") return static_cast<int>(AudioBus::Master);
    if (key == "music" || key == "1") return static_cast<int>(AudioBus::Music);
    if (key == "sfx" || key == "effects" || key == "2") return static_cast<int>(AudioBus::SFX);
    if (key == "voice" || key == "3") return static_cast<int>(AudioBus::Voice);
    if (key == "ambient" || key == "4") return static_cast<int>(AudioBus::Ambient);
    return -1;
}

const char* WidgetTypeName(UIWidgetType type) {
    switch (type) {
        case UIWidgetType::Panel: return "panel";
        case UIWidgetType::Text: return "text";
        case UIWidgetType::Button: return "button";
        case UIWidgetType::Image: return "image";
        case UIWidgetType::Slider: return "slider";
        case UIWidgetType::CheckBox: return "checkbox";
        case UIWidgetType::Progress: return "progress";
        case UIWidgetType::Input: return "input";
        case UIWidgetType::Dropdown: return "dropdown";
        case UIWidgetType::ScrollList: return "scroll_list";
        case UIWidgetType::Toggle: return "toggle";
        case UIWidgetType::LayoutGroup: return "layout_group";
        case UIWidgetType::None:
        default: return "none";
    }
}

} // namespace

ScriptEngine::ScriptEngine() {
    m_lua.open_libraries(sol::lib::base, sol::lib::math,
                         sol::lib::table, sol::lib::string);
    BindFunctions();
}

void ScriptEngine::AddLog(const std::string& msg) {
    m_logs.push_back(msg);
    if (m_logs.size() > 1000) m_logs.erase(m_logs.begin());
    spdlog::info("[Lua] {}", msg);
}

bool ScriptEngine::RunScript(const std::string& filepath) {
    try {
        m_lua.script_file(filepath);
        // A fresh script (including a hot reload) gets its own RunOnce pass.
        m_runOnceInvoked = false;
        AddLog("Ran script: " + filepath);
        return true;
    } catch (const sol::error& e) {
        AddLog(std::string("ERROR: ") + e.what());
        return false;
    }
}

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

    // Build the shared entity metatable before any handle can be returned.
    sol::table methods = m_lua.create_table();
    m_entityMeta = m_lua.create_table();

    methods["valid"] = [resolveTable](sol::table self) {
        return resolveTable(self) != entt::entity{entt::null};
    };
    methods["get_id"] = [idFromTable](sol::table self) {
        return idFromTable(self);
    };
    methods["name"] = [this, resolveTable](sol::table self) {
        const entt::entity entity = resolveTable(self);
        if (entity == entt::null) return std::string{};
        auto& registry = m_scene->Registry();
        if (!registry.all_of<NameComponent>(entity)) return std::string{};
        return registry.get<NameComponent>(entity).name;
    };
    methods["set_name"] = [this, resolveTable](sol::table self,
                                                const std::string& name) {
        const entt::entity entity = resolveTable(self);
        if (entity == entt::null) return self;
        auto& registry = m_scene->Registry();
        if (registry.all_of<NameComponent>(entity)) {
            registry.get<NameComponent>(entity).name = name;
        } else {
            registry.emplace<NameComponent>(entity, NameComponent{name});
        }
        return self;
    };
    methods["position"] = [this, resolveTable, getTransform, makeVec3](sol::table self) {
        const entt::entity entity = resolveTable(self);
        const TransformComponent* transform = getTransform(entity, false);
        return makeVec3(transform ? transform->position : glm::vec3(0.0f));
    };
    methods["set_position"] = [resolveTable, getTransform, readVec3Args](
                                  sol::table self, sol::variadic_args args) {
        const entt::entity entity = resolveTable(self);
        glm::vec3 position{0.0f};
        if (entity == entt::null || !readVec3Args(args, position)) return self;
        if (TransformComponent* transform = getTransform(entity, true)) {
            transform->position = position;
        }
        return self;
    };
    methods["translate"] = [resolveTable, getTransform, readVec3Args](
                               sol::table self, sol::variadic_args args) {
        const entt::entity entity = resolveTable(self);
        glm::vec3 delta{0.0f};
        if (entity == entt::null || !readVec3Args(args, delta)) return self;
        if (TransformComponent* transform = getTransform(entity, true)) {
            transform->position += delta;
        }
        return self;
    };
    methods["rotation"] = [this, resolveTable, getTransform, makeQuat](sol::table self) {
        const entt::entity entity = resolveTable(self);
        const TransformComponent* transform = getTransform(entity, false);
        return makeQuat(transform ? transform->rotation
                                  : glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    };
    methods["set_rotation"] = [resolveTable, getTransform, readQuat, readNumber](
                                  sol::table self, sol::variadic_args args) {
        const entt::entity entity = resolveTable(self);
        if (entity == entt::null) return self;
        glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
        bool ok = false;
        if (args.size() == 1) {
            ok = readQuat(args[0], rotation);
        } else if (args.size() >= 4) {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            float w = 1.0f;
            ok = readNumber(args[0], x) && readNumber(args[1], y) &&
                 readNumber(args[2], z) && readNumber(args[3], w);
            if (ok) rotation = glm::quat(w, x, y, z);
        } else if (args.size() >= 3) {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            ok = readNumber(args[0], x) && readNumber(args[1], y) &&
                 readNumber(args[2], z);
            if (ok) rotation = glm::quat(glm::radians(glm::vec3{x, y, z}));
        }
        if (!ok) return self;
        if (TransformComponent* transform = getTransform(entity, true)) {
            transform->rotation = glm::normalize(rotation);
        }
        return self;
    };
    methods["euler"] = [this, resolveTable, getTransform, makeVec3](sol::table self) {
        const entt::entity entity = resolveTable(self);
        const TransformComponent* transform = getTransform(entity, false);
        if (!transform) return makeVec3(glm::vec3(0.0f));
        return makeVec3(glm::degrees(glm::eulerAngles(transform->rotation)));
    };
    methods["set_euler"] = [resolveTable, getTransform, readVec3Args](
                               sol::table self, sol::variadic_args args) {
        const entt::entity entity = resolveTable(self);
        glm::vec3 euler{0.0f};
        if (entity == entt::null || !readVec3Args(args, euler)) return self;
        if (TransformComponent* transform = getTransform(entity, true)) {
            transform->rotation = glm::normalize(
                glm::quat(glm::radians(euler)));
        }
        return self;
    };
    methods["scale"] = [resolveTable, getTransform, makeVec3](sol::table self) {
        const entt::entity entity = resolveTable(self);
        const TransformComponent* transform = getTransform(entity, false);
        return makeVec3(transform ? transform->scale : glm::vec3(1.0f));
    };
    methods["set_scale"] = [resolveTable, getTransform, readVec3Args](
                               sol::table self, sol::variadic_args args) {
        const entt::entity entity = resolveTable(self);
        glm::vec3 scale{1.0f};
        if (entity == entt::null || !readVec3Args(args, scale)) return self;
        if (TransformComponent* transform = getTransform(entity, true)) {
            transform->scale = scale;
        }
        return self;
    };
    methods["scale_by"] = [resolveTable, getTransform, readVec3Args](
                              sol::table self, sol::variadic_args args) {
        const entt::entity entity = resolveTable(self);
        glm::vec3 scale{1.0f};
        if (entity == entt::null || !readVec3Args(args, scale)) return self;
        if (TransformComponent* transform = getTransform(entity, true)) {
            transform->scale *= scale;
        }
        return self;
    };
    methods["parent"] = [this, resolveTable, makeEntity](sol::table self) -> sol::object {
        const entt::entity entity = resolveTable(self);
        if (entity == entt::null) return sol::make_object(m_lua, sol::nil);
        auto& registry = m_scene->Registry();
        if (!registry.all_of<ParentComponent>(entity)) {
            return sol::make_object(m_lua, sol::nil);
        }
        const entt::entity parent = registry.get<ParentComponent>(entity).parent;
        if (parent == entt::null || !registry.valid(parent)) {
            return sol::make_object(m_lua, sol::nil);
        }
        return makeEntity(static_cast<uint32_t>(parent));
    };
    methods["set_parent"] = [this, resolveTable, resolve](sol::table self,
                                                          sol::object parentValue) {
        const entt::entity entity = resolveTable(self);
        if (entity == entt::null) return false;
        if (parentValue.get_type() == sol::type::nil ||
            parentValue.get_type() == sol::type::none) {
            auto& registry = m_scene->Registry();
            if (registry.all_of<ParentComponent>(entity)) {
                registry.remove<ParentComponent>(entity);
            }
            return true;
        }
        const entt::entity parent = resolve(parentValue);
        if (parent == entt::null || parent == entity) return false;
        auto& registry = m_scene->Registry();
        entt::entity cursor = parent;
        for (int depth = 0; depth < 256 && cursor != entt::null; ++depth) {
            if (cursor == entity) return false;
            if (!registry.all_of<ParentComponent>(cursor)) break;
            cursor = registry.get<ParentComponent>(cursor).parent;
        }
        registry.emplace_or_replace<ParentComponent>(entity,
                                                     ParentComponent{parent});
        return true;
    };
    methods["detach"] = [this, resolveTable](sol::table self) {
        const entt::entity entity = resolveTable(self);
        if (entity == entt::null) return false;
        auto& registry = m_scene->Registry();
        if (!registry.all_of<ParentComponent>(entity)) return false;
        registry.remove<ParentComponent>(entity);
        return true;
    };
    methods["children"] = [this, resolveTable, makeEntityTable](sol::table self) {
        sol::table result = m_lua.create_table();
        const entt::entity entity = resolveTable(self);
        if (entity == entt::null) return result;
        int index = 1;
        auto view = m_scene->Registry().view<ParentComponent>();
        for (entt::entity child : view) {
            if (view.get<ParentComponent>(child).parent == entity) {
                result[index++] = makeEntityTable(static_cast<uint32_t>(child));
            }
        }
        return result;
    };
    methods["destroy"] = [this, resolveTable](sol::table self) {
        const entt::entity entity = resolveTable(self);
        if (entity == entt::null) return false;
        DestroyEntity(entity);
        return true;
    };
    methods["has_component"] = [this, resolveTable](sol::table self,
                                                     const std::string& name) {
        const entt::entity entity = resolveTable(self);
        if (entity == entt::null || !m_scene) return false;
        auto& registry = m_scene->Registry();
        const std::string key = NormalizeKey(name);
        if (key == "name") return registry.all_of<NameComponent>(entity);
        if (key == "transform") return registry.all_of<TransformComponent>(entity);
        if (key == "parent") return registry.all_of<ParentComponent>(entity);
        if (key == "mesh") return registry.all_of<MeshComponent>(entity);
        if (key == "material") return registry.all_of<MaterialComponent>(entity);
        if (key == "skeletal") return registry.all_of<SkeletalComponent>(entity);
        if (key == "camera") return registry.all_of<CameraComponent>(entity);
        if (key == "directional_light") return registry.all_of<DirectionalLightComponent>(entity);
        if (key == "point_light") return registry.all_of<PointLightComponent>(entity);
        if (key == "spot_light") return registry.all_of<SpotLightComponent>(entity);
        if (key == "rigid_body" || key == "rigidbody") return registry.all_of<RigidBodyComponent>(entity);
        if (key == "collider") return registry.all_of<ColliderComponent>(entity);
        if (key == "landscape") return registry.all_of<LandscapeComponent>(entity);
        if (key == "vegetation") return registry.all_of<VegetationComponent>(entity);
        if (key == "nav_agent") return registry.all_of<NavAgentComponent>(entity);
        if (key == "soft_body") return registry.all_of<SoftBodyComponent>(entity);
        if (key == "ai_controller") return registry.all_of<AIControllerComponent>(entity);
        if (key == "player_tag") return registry.all_of<PlayerTagComponent>(entity);
        if (key == "perception") return registry.all_of<PerceptionComponent>(entity);
        if (key == "behavior_tree") return registry.all_of<BehaviorTreeComponent>(entity);
        if (key == "audio_source") return registry.all_of<AudioSourceComponent>(entity);
        if (key == "audio_listener") return registry.all_of<AudioListenerComponent>(entity);
        if (key == "post_process_volume") return registry.all_of<PostProcessVolumeComponent>(entity);
        if (key == "particle_emitter") return registry.all_of<ParticleEmitterComponent>(entity);
        if (key == "water") return registry.all_of<WaterComponent>(entity);
        if (key == "groom" || key == "groom_hair") return registry.all_of<GroomComponent>(entity);
        if (key == "ui_canvas") return registry.all_of<UICanvasComponent>(entity);
        if (key == "animation_state_machine") return registry.all_of<AnimationStateMachineComponent>(entity);
        if (key == "lua_entity") return registry.all_of<LuaEntityComponent>(entity);
        return false;
    };

    m_entityMeta[sol::meta_function::index] = methods;
    m_entityMeta[sol::meta_function::to_string] = [this, idFromTable](sol::table self) {
        const uint32_t id = idFromTable(self);
        if (!m_scene || !m_scene->Registry().valid(static_cast<entt::entity>(id))) {
            return std::string("Entity(invalid)");
        }
        auto& registry = m_scene->Registry();
        if (registry.all_of<NameComponent>(static_cast<entt::entity>(id))) {
            return std::string("Entity(") +
                   registry.get<NameComponent>(static_cast<entt::entity>(id)).name +
                   ", id=" + std::to_string(id) + ")";
        }
        return std::string("Entity(id=") + std::to_string(id) + ")";
    };
    m_entityMeta[sol::meta_function::equal_to] = [idFromTable](sol::table lhs,
                                                                sol::table rhs) {
        return idFromTable(lhs) == idFromTable(rhs);
    };

    auto createEntityId = [this](const std::string& name) -> uint32_t {
        if (!m_scene) return static_cast<uint32_t>(entt::null);
        Entity entity = m_scene->CreateEntity(name);
        entity.AddComponent<LuaEntityComponent>();
        return static_cast<uint32_t>(static_cast<entt::entity>(entity));
    };

    // ------------------------------------------------------------------
    // World / ECS
    // ------------------------------------------------------------------
    world["create"] = [createEntityId, makeEntity](const std::string& name) {
        return makeEntity(createEntityId(name));
    };
    world["entity"] = [makeEntity, idFromValue](sol::object value) {
        return makeEntity(idFromValue(value));
    };
    world["find"] = [this, makeEntity](const std::string& name) {
        if (!m_scene) return sol::make_object(m_lua, sol::nil);
        Entity entity = m_scene->FindEntityByName(name);
        return makeEntity(static_cast<uint32_t>(static_cast<entt::entity>(entity)));
    };
    world["find_all"] = [this, makeEntityTable](sol::optional<std::string> name) {
        sol::table result = m_lua.create_table();
        if (!m_scene) return result;
        int index = 1;
        auto view = m_scene->Registry().view<NameComponent>();
        for (entt::entity entity : view) {
            if (name && view.get<NameComponent>(entity).name != *name) continue;
            result[index++] = makeEntityTable(static_cast<uint32_t>(entity));
        }
        return result;
    };
    world["all"] = [this, makeEntityTable](sol::optional<std::string> name) {
        sol::table result = m_lua.create_table();
        if (!m_scene) return result;
        int index = 1;
        auto view = m_scene->Registry().view<NameComponent>();
        for (entt::entity entity : view) {
            if (name && view.get<NameComponent>(entity).name != *name) continue;
            result[index++] = makeEntityTable(static_cast<uint32_t>(entity));
        }
        return result;
    };
    world["exists"] = [resolve](sol::object value) {
        return resolve(value) != entt::entity{entt::null};
    };
    world["destroy"] = [this, resolve](sol::object value) {
        const entt::entity entity = resolve(value);
        if (entity == entt::null) return false;
        DestroyEntity(entity);
        return true;
    };
    world["count"] = [this]() -> size_t {
        if (!m_scene) return 0;
        return m_scene->Registry().view<NameComponent>().size();
    };
    world["clear_script_entities"] = [this]() {
        ClearLuaEntities();
    };

    // Legacy ECS aliases.
    m_lua.set_function("new_entity", [createEntityId](const std::string& name) {
        return createEntityId(name);
    });
    m_lua.set_function("set_position", [resolve, getTransform, readVec3Args](
                                          sol::object value, sol::variadic_args args) {
        const entt::entity entity = resolve(value);
        glm::vec3 position{0.0f};
        if (entity == entt::null || !readVec3Args(args, position)) return;
        if (TransformComponent* transform = getTransform(entity, true)) {
            transform->position = position;
        }
    });
    m_lua.set_function("get_position", [resolve, getTransform, makeVec3](
                                          sol::object value) {
        const entt::entity entity = resolve(value);
        const TransformComponent* transform = getTransform(entity, false);
        return makeVec3(transform ? transform->position : glm::vec3(0.0f));
    });
    m_lua.set_function("translate", [resolve, getTransform, readVec3Args](
                                        sol::object value, sol::variadic_args args) {
        const entt::entity entity = resolve(value);
        glm::vec3 delta{0.0f};
        if (entity == entt::null || !readVec3Args(args, delta)) return;
        if (TransformComponent* transform = getTransform(entity, true)) {
            transform->position += delta;
        }
    });
    m_lua.set_function("destroy_entity", [this, resolve](sol::object value) {
        const entt::entity entity = resolve(value);
        if (entity == entt::null) return false;
        DestroyEntity(entity);
        return true;
    });

    // ------------------------------------------------------------------
    // Input (SDL3-backed by the host application).
    // ------------------------------------------------------------------
    const auto keyDown = [this](const std::string& key) {
        return m_isKeyDown ? m_isKeyDown(key) : false;
    };
    const auto axis = [this](const std::string& name) {
        return m_getAxis ? m_getAxis(name) : 0.0f;
    };
    const auto axisRaw = [this](const std::string& name) {
        return m_getAxisRaw ? m_getAxisRaw(name) : 0.0f;
    };
    const auto mouseDown = [this](const std::string& button) {
        return m_isMouseButtonDown ? m_isMouseButtonDown(button) : false;
    };
    const auto mousePosition = [this, makeVec2]() {
        return makeVec2(m_getMousePosition ? m_getMousePosition() : glm::vec2(0.0f));
    };
    const auto mouseDelta = [this, makeVec2]() {
        return makeVec2(m_getMouseDelta ? m_getMouseDelta() : glm::vec2(0.0f));
    };
    const auto mouseWheel = [this]() {
        return m_getMouseWheel ? m_getMouseWheel() : 0.0f;
    };

    input["key_down"] = keyDown;
    input["axis"] = axis;
    input["axis_raw"] = axisRaw;
    input["mouse_down"] = mouseDown;
    input["mouse_position"] = mousePosition;
    input["mouse_delta"] = mouseDelta;
    input["mouse_wheel"] = mouseWheel;
    m_lua.set_function("is_key_down", keyDown);
    m_lua.set_function("get_axis", axis);
    m_lua.set_function("get_axis_raw", axisRaw);
    m_lua.set_function("is_mouse_button_down", mouseDown);
    m_lua.set_function("get_mouse_position", mousePosition);
    m_lua.set_function("get_mouse_delta", mouseDelta);
    m_lua.set_function("get_mouse_wheel", mouseWheel);

    // ------------------------------------------------------------------
    // Physics.  The script layer owns the ECS components; the host creates
    // the native Jolt body through the callbacks registered by BindPhysics().
    // ------------------------------------------------------------------
    auto spawnPhysicsEntity = [this, makeEntity](const std::string& name,
                                                  int shape,
                                                  const glm::vec3& position,
                                                  const glm::vec3& size,
                                                  float radius,
                                                  float halfHeight,
                                                  bool isStatic) -> sol::object {
        if (!m_scene) return sol::make_object(m_lua, sol::nil);
        Entity entity = m_scene->CreateEntity(name);
        entity.AddComponent<LuaEntityComponent>();
        if (name == "Player") entity.AddComponent<PlayerTagComponent>();

        auto& transform = entity.AddComponent<TransformComponent>();
        transform.position = position;

        auto& body = entity.AddComponent<RigidBodyComponent>();
        body.motionType = isStatic ? RigidBodyComponent::MotionType::Static
                                   : RigidBodyComponent::MotionType::Dynamic;
        body.mass = isStatic ? 0.0f : 1.0f;
        body.useGravity = !isStatic;

        auto& collider = entity.AddComponent<ColliderComponent>();
        collider.friction = 0.5f;

        uint32_t bodyId = 0xFFFFFFFFu;
        if (shape == 0) {
            transform.scale = size;
            collider.shapeType = ColliderShapeType::Box;
            collider.halfExtents = size * 0.5f;
            if (m_physics.createBox) {
                bodyId = m_physics.createBox(size * 0.5f, position,
                                             static_cast<entt::entity>(entity),
                                             isStatic);
            }
        } else if (shape == 1) {
            transform.scale = glm::vec3(radius * 2.0f);
            collider.shapeType = ColliderShapeType::Sphere;
            collider.radius = radius;
            if (m_physics.createSphere) {
                bodyId = m_physics.createSphere(radius, position,
                                                static_cast<entt::entity>(entity),
                                                isStatic);
            }
        } else {
            transform.scale = glm::vec3(radius * 2.0f,
                                        (halfHeight + radius) * 2.0f,
                                        radius * 2.0f);
            collider.shapeType = ColliderShapeType::Capsule;
            collider.radius = radius;
            collider.halfHeight = halfHeight;
            if (m_physics.createCapsule) {
                bodyId = m_physics.createCapsule(halfHeight, radius, position,
                                                 static_cast<entt::entity>(entity),
                                                 isStatic);
            }
        }
        body.bodyId = bodyId;
        return makeEntity(static_cast<uint32_t>(static_cast<entt::entity>(entity)));
    };

    physics["create_box"] = [this, spawnPhysicsEntity, readVec3](
                                 const std::string& name, float x, float y, float z,
                                 sol::object sizeValue,
                                 sol::optional<bool> isStatic) -> sol::object {
        glm::vec3 size{1.0f};
        if (sizeValue.get_type() == sol::type::table) {
            if (!readVec3(sizeValue, size)) {
                return sol::make_object(m_lua, sol::nil);
            }
        } else if (sizeValue.get_type() == sol::type::number) {
            size = glm::vec3(static_cast<float>(sizeValue.as<double>()));
        } else {
            return sol::make_object(m_lua, sol::nil);
        }
        return spawnPhysicsEntity(name, 0, {x, y, z}, size, 0.0f, 0.0f,
                                  isStatic.value_or(false));
    };
    physics["create_sphere"] = [spawnPhysicsEntity](
                                    const std::string& name, float x, float y,
                                    float z, float radius,
                                    sol::optional<bool> isStatic) {
        return spawnPhysicsEntity(name, 1, {x, y, z}, glm::vec3(radius * 2.0f),
                                  radius, 0.0f, isStatic.value_or(false));
    };
    physics["create_capsule"] = [spawnPhysicsEntity](
                                     const std::string& name, float x, float y,
                                     float z, float halfHeight, float radius,
                                     sol::optional<bool> isStatic) {
        return spawnPhysicsEntity(name, 2, {x, y, z}, glm::vec3(0.0f),
                                  radius, halfHeight, isStatic.value_or(false));
    };

    const auto applyImpulse = [this, resolve, readVec3Args](
                                  sol::object value, sol::variadic_args args) {
        const entt::entity entity = resolve(value);
        glm::vec3 impulse{0.0f};
        if (entity == entt::null || !readVec3Args(args, impulse)) return false;
        if (m_applyImpulse) m_applyImpulse(static_cast<uint32_t>(entity), impulse);
        return true;
    };
    const auto getVelocity = [this, resolve, makeVec3](sol::object value) {
        const entt::entity entity = resolve(value);
        const glm::vec3 velocity = (entity != entt::null && m_getVelocity)
            ? m_getVelocity(static_cast<uint32_t>(entity))
            : glm::vec3(0.0f);
        return makeVec3(velocity);
    };
    const auto setVelocity = [this, resolve, readVec3Args](
                                  sol::object value, sol::variadic_args args) {
        const entt::entity entity = resolve(value);
        glm::vec3 velocity{0.0f};
        if (entity == entt::null || !readVec3Args(args, velocity)) return false;
        if (m_setVelocity) m_setVelocity(static_cast<uint32_t>(entity), velocity);
        return true;
    };
    const auto destroyBody = [this, resolve](sol::object value) {
        const entt::entity entity = resolve(value);
        if (entity == entt::null) return false;
        auto& registry = m_scene->Registry();
        if (!registry.all_of<RigidBodyComponent>(entity)) return false;
        auto& body = registry.get<RigidBodyComponent>(entity);
        if (body.bodyId == 0xFFFFFFFFu) return false;
        if (m_physics.destroyBody) m_physics.destroyBody(body.bodyId);
        body.bodyId = 0xFFFFFFFFu;
        return true;
    };

    physics["add_impulse"] = applyImpulse;
    physics["get_velocity"] = getVelocity;
    physics["velocity"] = getVelocity;
    physics["set_velocity"] = setVelocity;
    physics["destroy_body"] = destroyBody;
    m_lua.set_function("apply_impulse", applyImpulse);
    m_lua.set_function("get_velocity", getVelocity);
    m_lua.set_function("set_velocity", setVelocity);
    m_lua.set_function("new_physics_cube", [spawnPhysicsEntity](
                                                const std::string& name,
                                                float x, float y, float z,
                                                float size) {
        return spawnPhysicsEntity(name, 0, {x, y, z}, glm::vec3(size),
                                  0.0f, 0.0f, false);
    });

    // ------------------------------------------------------------------
    // Audio (miniaudio-backed by the host application).  Mixer bus values
    // live in Scene and are applied by the audio system each frame.
    // ------------------------------------------------------------------
    const auto playSound = [this](const std::string& path,
                                  sol::optional<bool> loop) {
        if (!m_playSound) {
            AddLog("[Lua] audio.play: no audio backend is bound");
            return false;
        }
        m_playSound(path, loop.value_or(false));
        return true;
    };
    const auto stopSound = [this](const std::string& path) {
        if (m_stopSound) m_stopSound(path);
    };
    const auto isSoundPlaying = [this](const std::string& path) {
        return m_isSoundPlaying ? m_isSoundPlaying(path) : false;
    };
    const auto stopAllSounds = [this]() {
        if (m_stopAllSounds) m_stopAllSounds();
    };
    const auto setBus = [this](const std::string& name, float volume,
                               sol::optional<bool> muted) {
        if (!m_scene) return false;
        const int index = AudioBusIndexFromName(name);
        if (index < 0 || index >= kAudioBusCount) return false;
        auto& bus = m_scene->AudioBuses()[static_cast<size_t>(index)];
        bus.volume = std::clamp(volume, 0.0f, 1.0f);
        if (muted) bus.muted = *muted;
        return true;
    };
    const auto getBus = [this](const std::string& name) -> sol::object {
        if (!m_scene) return sol::make_object(m_lua, sol::nil);
        const int index = AudioBusIndexFromName(name);
        if (index < 0 || index >= kAudioBusCount) {
            return sol::make_object(m_lua, sol::nil);
        }
        const auto& bus = m_scene->AudioBuses()[static_cast<size_t>(index)];
        sol::table table = m_lua.create_table();
        table["name"] = AudioBusName(static_cast<AudioBus>(index));
        table["volume"] = bus.volume;
        table["muted"] = bus.muted;
        return sol::make_object(m_lua, table);
    };

    audio["play"] = playSound;
    audio["stop"] = stopSound;
    audio["is_playing"] = isSoundPlaying;
    audio["stop_all"] = stopAllSounds;
    audio["set_bus"] = setBus;
    audio["get_bus"] = getBus;
    m_lua.set_function("play_sound", [this](const std::string& path) {
        if (!m_playSound) {
            AddLog("[Lua] play_sound: no audio backend is bound");
            return false;
        }
        m_playSound(path, false);
        return true;
    });
    m_lua.set_function("stop_sound", stopSound);
    m_lua.set_function("is_sound_playing", isSoundPlaying);

    // ------------------------------------------------------------------
    // Animation state machines.
    // ------------------------------------------------------------------
    auto findAnimationMachine = [this](entt::entity entity)
        -> AnimationStateMachineComponent* {
        if (!m_scene) return nullptr;
        auto& registry = m_scene->Registry();
        int depth = 0;
        while (registry.valid(entity) && depth++ < 128) {
            if (registry.all_of<AnimationStateMachineComponent>(entity)) {
                return &registry.get<AnimationStateMachineComponent>(entity);
            }
            if (!registry.all_of<ParentComponent>(entity)) break;
            entity = registry.get<ParentComponent>(entity).parent;
        }
        return nullptr;
    };

    const auto setAnimationState = [resolve, findAnimationMachine](
                                       sol::object value,
                                       const std::string& state) {
        const entt::entity entity = resolve(value);
        if (auto* machine = findAnimationMachine(entity)) {
            AnimationSystem::SetState(*machine, state);
            return true;
        }
        return false;
    };
    const auto setAnimationParameter = [resolve, findAnimationMachine](
                                           sol::object value,
                                           const std::string& name,
                                           float parameter) {
        const entt::entity entity = resolve(value);
        if (auto* machine = findAnimationMachine(entity)) {
            AnimationSystem::SetParameter(*machine, name, parameter);
            return true;
        }
        return false;
    };
    const auto getAnimationParameter = [resolve, findAnimationMachine](
                                           sol::object value,
                                           const std::string& name,
                                           sol::optional<float> fallback) {
        const entt::entity entity = resolve(value);
        if (auto* machine = findAnimationMachine(entity)) {
            return AnimationSystem::GetParameter(*machine, name,
                                                 fallback.value_or(0.0f));
        }
        return fallback.value_or(0.0f);
    };
    const auto triggerAnimation = [resolve, findAnimationMachine](
                                      sol::object value,
                                      const std::string& name) {
        const entt::entity entity = resolve(value);
        if (auto* machine = findAnimationMachine(entity)) {
            AnimationSystem::Trigger(*machine, name);
            return true;
        }
        return false;
    };

    animation["set_state"] = setAnimationState;
    animation["set_parameter"] = setAnimationParameter;
    animation["get_parameter"] = getAnimationParameter;
    animation["trigger"] = triggerAnimation;
    m_lua.set_function("set_animation_state", setAnimationState);
    m_lua.set_function("set_animation_parameter", setAnimationParameter);
    m_lua.set_function("get_animation_parameter", getAnimationParameter);
    m_lua.set_function("trigger_animation", triggerAnimation);

    // ------------------------------------------------------------------
    // Skeletal model assets and playback.
    //
    // Galeshi.animation.model(path) returns a lightweight model object that
    // remembers the source file and clip list.  Spawn/apply attach the full
    // imported mesh hierarchy and SkeletalComponent to the scene; play passes
    // the model and a clip directly to the animation system.
    // ------------------------------------------------------------------
    auto skeletalPath = [](const sol::object& value) -> std::string {
        if (value.get_type() == sol::type::string) {
            return value.as<std::string>();
        }
        if (value.get_type() == sol::type::table) {
            sol::table table = value.as<sol::table>();
            const sol::object path = table.get<sol::object>("path");
            if (path.get_type() == sol::type::string) {
                return path.as<std::string>();
            }
        }
        return {};
    };

    auto makeSkeletalModel = [this](const std::string& path) -> sol::object {
        if (!m_assets) {
            AddLog("[Lua] animation.model: no AssetManager is bound");
            return sol::make_object(m_lua, sol::nil);
        }
        if (path.empty()) return sol::make_object(m_lua, sol::nil);

        auto loaded = m_assets->LoadModel(path);
        if (loaded.meshes.empty() && !loaded.hasAnimation) {
            AddLog("[Lua] animation.model: could not load '" + path + "'");
            return sol::make_object(m_lua, sol::nil);
        }

        const std::string relativePath = m_assets->MakeRelativePath(path);
        sol::table model = m_lua.create_table();
        model["path"] = relativePath;
        model["name"] = std::filesystem::path(path).stem().string();
        model["valid"] = true;
        model["has_animation"] = loaded.hasAnimation;
        model["mesh_count"] = static_cast<int>(loaded.meshes.size());
        model["bone_count"] = loaded.totalBones;
        model["num_clips"] = static_cast<int>(loaded.animationNames.size());

        sol::table clips = m_lua.create_table();
        sol::table durations = m_lua.create_table();
        for (size_t i = 0; i < loaded.animationNames.size(); ++i) {
            clips[i + 1] = loaded.animationNames[i];
        }
        for (size_t i = 0; i < loaded.animationDurations.size(); ++i) {
            durations[i + 1] = loaded.animationDurations[i];
        }
        model["clips"] = clips;
        model["durations"] = durations;
        model[sol::metatable_key] = m_skeletalMeta;
        return sol::make_object(m_lua, model);
    };

    auto spawnSkeletalModel = [this, makeEntity](
                                  const std::string& path,
                                  const std::string& requestedName,
                                  const glm::vec3& position) -> sol::object {
        if (!m_assets || !m_scene) {
            AddLog("[Lua] animation.spawn: no AssetManager or Scene is bound");
            return sol::make_object(m_lua, sol::nil);
        }
        auto loaded = m_assets->LoadModel(path);
        if (loaded.meshes.empty()) {
            AddLog("[Lua] animation.spawn: model has no meshes: " + path);
            return sol::make_object(m_lua, sol::nil);
        }

        const std::string source = m_assets->MakeRelativePath(path);
        const std::string stem = std::filesystem::path(path).stem().string();
        const std::string rootName = requestedName.empty()
            ? (stem.empty() ? std::string("SkeletalModel") : stem)
            : requestedName;

        Entity root = m_scene->CreateEntity(rootName);
        root.AddComponent<LuaEntityComponent>();
        auto& transform = root.GetComponent<TransformComponent>();
        transform.position = position;

        if (loaded.hasAnimation || loaded.totalBones > 0) {
            auto& skeleton = root.AddComponent<SkeletalComponent>();
            skeleton.isAnimated = loaded.hasAnimation;
            skeleton.sourcePath = source;
            skeleton.boneMatrices.assign(256, glm::mat4(1.0f));
            if (loaded.hasAnimation) {
                auto& machine =
                    root.AddComponent<AnimationStateMachineComponent>();
                machine = AnimationSystem::CreateDefaultStateMachine(
                    loaded.animationNames);
            }
        }

        for (size_t i = 0; i < loaded.meshes.size(); ++i) {
            Entity meshEntity = m_scene->CreateEntity(
                rootName + "_" + std::to_string(i));
            meshEntity.AddComponent<LuaEntityComponent>();
            meshEntity.AddComponent<ParentComponent>(
                ParentComponent{static_cast<entt::entity>(root)});
            meshEntity.AddComponent<MeshComponent>(MeshComponent{
                loaded.meshes[i], source, "", static_cast<int>(i)});

            std::shared_ptr<Material> material;
            if (i < loaded.meshMaterialIndex.size()) {
                const int materialIndex = loaded.meshMaterialIndex[i];
                if (materialIndex >= 0 &&
                    materialIndex < static_cast<int>(loaded.materials.size())) {
                    material = loaded.materials[static_cast<size_t>(materialIndex)];
                }
            }
            if (!material && !loaded.materials.empty()) {
                material = loaded.materials.front();
            }
            if (!material) material = std::make_shared<Material>();
            meshEntity.AddComponent<MaterialComponent>(
                MaterialComponent{material});
        }

        spdlog::info("[Lua] Spawned skeletal model '{}' ({} meshes, {} clips)",
                     source, loaded.meshes.size(), loaded.animationNames.size());
        return makeEntity(static_cast<uint32_t>(static_cast<entt::entity>(root)));
    };

    auto applySkeletalModel = [this](entt::entity entity,
                                     const std::string& path) -> bool {
        if (!m_assets || !m_scene || entity == entt::null) return false;
        auto loaded = m_assets->LoadModel(path);
        if (loaded.meshes.empty() && !loaded.hasAnimation) return false;

        auto& registry = m_scene->Registry();
        SkeletalComponent* skeleton = nullptr;
        if (registry.all_of<SkeletalComponent>(entity)) {
            skeleton = &registry.get<SkeletalComponent>(entity);
        } else {
            skeleton = &registry.emplace<SkeletalComponent>(entity);
        }
        skeleton->isAnimated = loaded.hasAnimation;
        skeleton->sourcePath = m_assets->MakeRelativePath(path);
        skeleton->currentTime = 0.0f;
        skeleton->activeAnimation = 0;
        skeleton->boneMatrices.assign(256, glm::mat4(1.0f));

        if (loaded.hasAnimation) {
            if (!registry.all_of<AnimationStateMachineComponent>(entity)) {
                auto& machine =
                    registry.emplace<AnimationStateMachineComponent>(entity);
                machine = AnimationSystem::CreateDefaultStateMachine(
                    loaded.animationNames);
            } else {
                auto& machine =
                    registry.get<AnimationStateMachineComponent>(entity);
                if (machine.states.empty()) {
                    machine = AnimationSystem::CreateDefaultStateMachine(
                        loaded.animationNames);
                }
            }
        }
        return true;
    };

    auto resolveSkeletalClip = [this](const std::string& path,
                                      const sol::object& value,
                                      int& outClip) -> bool {
        if (!m_assets) return false;
        auto loaded = m_assets->LoadModel(path);
        if (!loaded.hasAnimation || loaded.animationNames.empty()) return false;

        int index = 0; // Lua-facing clip indices are 1-based.
        if (value.get_type() == sol::type::number) {
            index = static_cast<int>(value.as<double>()) - 1;
        } else if (value.get_type() == sol::type::string) {
            const std::string name = value.as<std::string>();
            bool found = false;
            for (size_t i = 0; i < loaded.animationNames.size(); ++i) {
                if (loaded.animationNames[i] == name) {
                    index = static_cast<int>(i);
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }
        index = std::clamp(index, 0,
                           static_cast<int>(loaded.animationNames.size()) - 1);
        outClip = index;
        return true;
    };

    auto playSkeletalModel = [this, applySkeletalModel, resolveSkeletalClip](
                                 entt::entity entity,
                                 const std::string& path,
                                 const sol::object& clipValue) -> bool {
        if (!m_assets || !m_scene || entity == entt::null) return false;
        if (!applySkeletalModel(entity, path)) return false;

        int clip = 0;
        if (!resolveSkeletalClip(path, clipValue, clip)) return false;

        auto& registry = m_scene->Registry();
        auto* skeleton = registry.try_get<SkeletalComponent>(entity);
        if (!skeleton) return false;
        skeleton->isAnimated = true;
        skeleton->activeAnimation = clip;
        skeleton->currentTime = 0.0f;

        if (auto* machine =
                registry.try_get<AnimationStateMachineComponent>(entity)) {
            int stateIndex = -1;
            for (size_t i = 0; i < machine->states.size(); ++i) {
                if (machine->states[i].clip == clip) {
                    stateIndex = static_cast<int>(i);
                    break;
                }
            }
            if (stateIndex >= 0) {
                machine->enabled = true;
                AnimationSystem::SetState(*machine,
                                          machine->states[stateIndex].name);
            } else {
                // Custom state machines may not have a state for this clip.
                // Fall back to direct clip playback.
                machine->enabled = false;
            }
        }
        return true;
    };

    auto playSkeletalClip = [this, playSkeletalModel](entt::entity entity,
                                                      const sol::object& clipValue) {
        if (!m_assets || !m_scene || entity == entt::null) return false;
        auto* skeleton = m_scene->Registry().try_get<SkeletalComponent>(entity);
        if (!skeleton || skeleton->sourcePath.empty()) return false;
        return playSkeletalModel(entity, skeleton->sourcePath, clipValue);
    };

    // Shared metatable for model objects returned by animation.model().
    m_skeletalMeta = m_lua.create_table();
    sol::table modelMethods = m_lua.create_table();
    modelMethods["clip_count"] = [](sol::table self) -> int {
        const sol::object count = self.get<sol::object>("num_clips");
        return count.get_type() == sol::type::number
            ? static_cast<int>(count.as<double>())
            : 0;
    };
    modelMethods["clip_name"] = [](sol::table self, int index) -> std::string {
        const sol::object clipsValue = self.get<sol::object>("clips");
        if (clipsValue.get_type() != sol::type::table) return {};
        sol::table clips = clipsValue.as<sol::table>();
        const sol::object name = clips.get<sol::object>(index);
        return name.get_type() == sol::type::string
            ? name.as<std::string>()
            : std::string{};
    };
    modelMethods["duration"] = [](sol::table self, int index) -> float {
        const sol::object durationsValue = self.get<sol::object>("durations");
        if (durationsValue.get_type() != sol::type::table) return 0.0f;
        sol::table durations = durationsValue.as<sol::table>();
        const sol::object value = durations.get<sol::object>(index);
        return value.get_type() == sol::type::number
            ? static_cast<float>(value.as<double>())
            : 0.0f;
    };
    modelMethods["spawn"] = [this, skeletalPath, spawnSkeletalModel](
                                sol::table self,
                                sol::optional<std::string> name,
                                sol::optional<float> x,
                                sol::optional<float> y,
                                sol::optional<float> z) -> sol::object {
        return spawnSkeletalModel(skeletalPath(sol::object(self)),
                                  name.value_or(""),
                                  {x.value_or(0.0f), y.value_or(0.0f),
                                   z.value_or(0.0f)});
    };
    modelMethods["apply"] = [this, resolve, skeletalPath, applySkeletalModel](
                                sol::table self, sol::object entity) {
        return applySkeletalModel(resolve(entity),
                                  skeletalPath(sol::object(self)));
    };
    modelMethods["play"] = [this, resolve, skeletalPath, playSkeletalModel](
                               sol::table self, sol::object entity,
                               sol::variadic_args args) {
        sol::object clip;
        if (args.size() > 0) clip = args[0];
        return playSkeletalModel(resolve(entity), skeletalPath(sol::object(self)),
                                 clip);
    };
    m_skeletalMeta[sol::meta_function::index] = modelMethods;
    m_skeletalMeta[sol::meta_function::to_string] =
        [](sol::table self) -> std::string {
            const sol::object path = self.get<sol::object>("path");
            const sol::object count = self.get<sol::object>("num_clips");
            return std::string("SkeletalModel(") +
                   (path.get_type() == sol::type::string
                        ? path.as<std::string>()
                        : std::string("?")) +
                   ", clips=" +
                   std::to_string(count.get_type() == sol::type::number
                                      ? static_cast<int>(count.as<double>())
                                      : 0) + ")";
        };

    animation["model"] = [makeSkeletalModel](const std::string& path) {
        return makeSkeletalModel(path);
    };
    animation["load"] = [makeSkeletalModel](const std::string& path) {
        return makeSkeletalModel(path);
    };
    animation["spawn"] = [this, skeletalPath, spawnSkeletalModel](
                             sol::object modelValue,
                             sol::optional<std::string> name,
                             sol::optional<float> x,
                             sol::optional<float> y,
                             sol::optional<float> z) -> sol::object {
        const std::string path = skeletalPath(modelValue);
        if (path.empty()) return sol::make_object(m_lua, sol::nil);
        return spawnSkeletalModel(path, name.value_or(""),
                                  {x.value_or(0.0f), y.value_or(0.0f),
                                   z.value_or(0.0f)});
    };
    animation["apply"] = [this, resolve, skeletalPath, applySkeletalModel](
                             sol::object entity, sol::object modelValue) {
        return applySkeletalModel(resolve(entity), skeletalPath(modelValue));
    };
    animation["play"] = [this, resolve, skeletalPath, playSkeletalModel](
                            sol::object entity, sol::object modelValue,
                            sol::variadic_args args) {
        sol::object clip;
        if (args.size() > 0) clip = args[0];
        return playSkeletalModel(resolve(entity), skeletalPath(modelValue),
                                 clip);
    };
    animation["play_clip"] = [this, resolve, playSkeletalClip](
                                 sol::object entity,
                                 sol::variadic_args args) {
        sol::object clip;
        if (args.size() > 0) clip = args[0];
        return playSkeletalClip(resolve(entity), clip);
    };
    animation["stop"] = [this, resolve](sol::object value) {
        const entt::entity entity = resolve(value);
        if (entity == entt::null || !m_scene) return false;
        auto* skeleton = m_scene->Registry().try_get<SkeletalComponent>(entity);
        if (!skeleton) return false;
        skeleton->isAnimated = false;
        return true;
    };
    animation["is_playing"] = [this, resolve](sol::object value) {
        const entt::entity entity = resolve(value);
        if (entity == entt::null || !m_scene) return false;
        auto* skeleton = m_scene->Registry().try_get<SkeletalComponent>(entity);
        return skeleton && skeleton->isAnimated;
    };

    // Legacy aliases for simple scripts.
    m_lua.set_function("load_skeletal_model", [makeSkeletalModel](
                                                   const std::string& path) {
        return makeSkeletalModel(path);
    });

    // ------------------------------------------------------------------
    // AI and navigation (Detour/Recast through host callbacks).
    // ------------------------------------------------------------------
    auto navAgent = [this, resolve](sol::object value) -> NavAgentComponent* {
        const entt::entity entity = resolve(value);
        if (entity == entt::null) return nullptr;
        auto& registry = m_scene->Registry();
        if (!registry.all_of<NavAgentComponent>(entity)) return nullptr;
        return &registry.get<NavAgentComponent>(entity);
    };

    const auto setAITarget = [navAgent, readVec3Args](
                                 sol::object value, sol::variadic_args args) {
        NavAgentComponent* agent = navAgent(value);
        glm::vec3 target{0.0f};
        if (!agent || !readVec3Args(args, target)) return false;
        agent->targetPosition = target;
        agent->hasTarget = true;
        return true;
    };
    const auto isPathDone = [navAgent](sol::object value) {
        NavAgentComponent* agent = navAgent(value);
        return !agent || agent->path.empty();
    };
    const auto setAISpeed = [navAgent](sol::object value, float speed) {
        NavAgentComponent* agent = navAgent(value);
        if (!agent) return false;
        agent->speed = speed;
        return true;
    };
    const auto getAISpeed = [navAgent](sol::object value) {
        NavAgentComponent* agent = navAgent(value);
        return agent ? agent->speed : 0.0f;
    };
    const auto clearAITarget = [navAgent](sol::object value) {
        NavAgentComponent* agent = navAgent(value);
        if (!agent) return false;
        agent->hasTarget = false;
        agent->path.clear();
        agent->currentWaypoint = 0;
        return true;
    };
    const auto setNavStoppingDistance = [navAgent](sol::object value,
                                                    float distance) {
        NavAgentComponent* agent = navAgent(value);
        if (!agent) return false;
        agent->stoppingDistance = std::max(distance, 0.0f);
        return true;
    };

    ai["set_target"] = setAITarget;
    ai["path_done"] = isPathDone;
    ai["is_path_done"] = isPathDone;
    ai["set_speed"] = setAISpeed;
    ai["speed"] = getAISpeed;
    ai["clear_target"] = clearAITarget;
    ai["set_stopping_distance"] = setNavStoppingDistance;
    m_lua.set_function("set_ai_target", setAITarget);
    m_lua.set_function("is_path_done", isPathDone);
    m_lua.set_function("set_ai_speed", setAISpeed);

    navigation["find_path"] = [this, readVec3, makeVec3](
                                  sol::object startValue,
                                  sol::object endValue) -> sol::object {
        glm::vec3 start{0.0f};
        glm::vec3 end{0.0f};
        if (!m_findPath || !readVec3(startValue, start) ||
            !readVec3(endValue, end)) {
            return sol::make_object(m_lua, sol::nil);
        }
        std::vector<glm::vec3> path;
        if (!m_findPath(start, end, path)) {
            return sol::make_object(m_lua, sol::nil);
        }
        sol::table result = m_lua.create_table();
        int index = 1;
        for (const glm::vec3& point : path) {
            result[index++] = makeVec3(point);
        }
        return sol::make_object(m_lua, result);
    };
    navigation["build"] = [this]() {
        return m_buildNavMesh ? m_buildNavMesh() : false;
    };

    // ------------------------------------------------------------------
    // UI runtime access and event handler registration.
    // ------------------------------------------------------------------
    auto findCanvas = [this](const std::string& id) -> std::pair<UICanvas*, entt::entity> {
        if (!m_scene) return {nullptr, entt::null};
        auto view = m_scene->Registry().view<UICanvasComponent>();
        for (entt::entity entity : view) {
            UICanvas& canvas = view.get<UICanvasComponent>(entity).canvas;
            if (id.empty() || canvas.canvasId == id) return {&canvas, entity};
        }
        return {nullptr, entt::null};
    };

    auto findWidget = [](UICanvas& canvas, const std::string& id) -> UIWidget* {
        for (UIWidget& widget : canvas.widgets) {
            if (widget.id == id) return &widget;
        }
        return nullptr;
    };

    auto widgetToTable = [this, makeVec2, makeVec4](const UIWidget& widget) {
        sol::table table = m_lua.create_table();
        table["id"] = widget.id;
        table["label"] = widget.label;
        table["type"] = WidgetTypeName(widget.type);
        table["visible"] = widget.visible;
        table["enabled"] = widget.enabled;
        table["interactable"] = widget.interactable;
        table["opacity"] = widget.opacity;
        table["position"] = makeVec2(widget.position);
        table["size"] = makeVec2(widget.size);
        table["value"] = widget.sliderValue;
        table["checked"] = widget.checked;
        table["color"] = makeVec4(widget.color);
        table["image"] = widget.imagePath;
        table["on_click"] = widget.onClick;
        table["on_hover"] = widget.onHover;
        table["on_show"] = widget.onShow;
        return table;
    };

    auto applyWidgetProps = [readNumber, readBool, readString, readVec2,
                             readVec4](UIWidget& widget,
                                       const sol::table& props) {
        std::string text;
        bool boolean = false;
        float number = 0.0f;
        glm::vec2 vector2{0.0f};
        glm::vec4 color{1.0f};

        if (readString(props.get<sol::object>("label"), text)) widget.label = text;
        if (readBool(props.get<sol::object>("visible"), boolean)) widget.visible = boolean;
        if (readBool(props.get<sol::object>("enabled"), boolean)) widget.enabled = boolean;
        if (readBool(props.get<sol::object>("interactable"), boolean)) {
            widget.interactable = boolean;
        }
        if (readNumber(props.get<sol::object>("opacity"), number)) {
            widget.opacity = std::clamp(number, 0.0f, 1.0f);
        }
        if (readNumber(props.get<sol::object>("value"), number)) widget.sliderValue = number;
        if (readBool(props.get<sol::object>("checked"), boolean)) widget.checked = boolean;
        if (readString(props.get<sol::object>("image"), text)) widget.imagePath = text;
        if (readString(props.get<sol::object>("on_click"), text)) widget.onClick = text;
        if (readString(props.get<sol::object>("on_hover"), text)) widget.onHover = text;
        if (readString(props.get<sol::object>("on_show"), text)) widget.onShow = text;
        if (readNumber(props.get<sol::object>("z_order"), number)) {
            widget.zOrder = static_cast<int>(number);
        }
        if (readNumber(props.get<sol::object>("parent"), number)) {
            widget.parent = static_cast<int>(number);
        }
        if (readVec2(props.get<sol::object>("position"), vector2)) widget.position = vector2;
        if (readVec2(props.get<sol::object>("size"), vector2)) widget.size = vector2;
        if (readVec4(props.get<sol::object>("color"), color)) widget.color = color;
    };

    ui["find_canvas"] = [this, findCanvas, makeEntity](const std::string& id) {
        const auto [canvas, entity] = findCanvas(id);
        if (!canvas) return sol::make_object(m_lua, sol::nil);
        return makeEntity(static_cast<uint32_t>(entity));
    };
    ui["canvases"] = [this, findCanvas]() {
        sol::table result = m_lua.create_table();
        if (!m_scene) return result;
        int index = 1;
        auto view = m_scene->Registry().view<UICanvasComponent>();
        for (entt::entity entity : view) {
            const UICanvas& canvas = view.get<UICanvasComponent>(entity).canvas;
            sol::table entry = m_lua.create_table();
            entry["entity"] = static_cast<uint32_t>(entity);
            entry["id"] = canvas.canvasId;
            entry["title"] = canvas.title;
            entry["visible"] = canvas.visible;
            entry["widgets"] = static_cast<int>(canvas.widgets.size());
            result[index++] = entry;
        }
        return result;
    };
    ui["canvas"] = [this, findCanvas](const std::string& id) -> sol::object {
        const auto [canvas, entity] = findCanvas(id);
        if (!canvas) return sol::make_object(m_lua, sol::nil);
        sol::table table = m_lua.create_table();
        table["entity"] = static_cast<uint32_t>(entity);
        table["id"] = canvas->canvasId;
        table["title"] = canvas->title;
        table["visible"] = canvas->visible;
        table["widgets"] = static_cast<int>(canvas->widgets.size());
        return sol::make_object(m_lua, table);
    };
    ui["widget"] = [this, findCanvas, findWidget, widgetToTable](
                       const std::string& canvasId,
                       const std::string& widgetId) -> sol::object {
        const auto [canvas, entity] = findCanvas(canvasId);
        (void)entity;
        if (!canvas) return sol::make_object(m_lua, sol::nil);
        UIWidget* widget = findWidget(*canvas, widgetId);
        if (!widget) return sol::make_object(m_lua, sol::nil);
        return sol::make_object(m_lua, widgetToTable(*widget));
    };
    ui["set_widget"] = [findCanvas, findWidget, applyWidgetProps](
                           const std::string& canvasId,
                           const std::string& widgetId,
                           sol::table props) {
        const auto [canvas, entity] = findCanvas(canvasId);
        (void)entity;
        if (!canvas) return false;
        UIWidget* widget = findWidget(*canvas, widgetId);
        if (!widget) return false;
        applyWidgetProps(*widget, props);
        return true;
    };
    ui["set_value"] = [findCanvas, findWidget](const std::string& canvasId,
                                               const std::string& widgetId,
                                               float value) {
        const auto [canvas, entity] = findCanvas(canvasId);
        (void)entity;
        if (!canvas) return false;
        UIWidget* widget = findWidget(*canvas, widgetId);
        if (!widget) return false;
        widget->sliderValue = value;
        return true;
    };
    ui["value"] = [findCanvas, findWidget](const std::string& canvasId,
                                           const std::string& widgetId) {
        const auto [canvas, entity] = findCanvas(canvasId);
        (void)entity;
        if (!canvas) return 0.0f;
        UIWidget* widget = findWidget(*canvas, widgetId);
        return widget ? widget->sliderValue : 0.0f;
    };
    ui["show_canvas"] = [findCanvas](const std::string& canvasId, bool visible) {
        const auto [canvas, entity] = findCanvas(canvasId);
        (void)entity;
        if (!canvas) return false;
        canvas->visible = visible;
        return true;
    };
    ui["on"] = [this](const std::string& name, sol::protected_function function) {
        if (!function.valid()) return false;
        m_uiHandlers[name] = function;
        return true;
    };
    ui["off"] = [this](const std::string& name) {
        return m_uiHandlers.erase(name) > 0;
    };
    ui["call"] = [this](const std::string& name,
                        sol::optional<std::string> widgetId,
                        sol::optional<float> value,
                        sol::optional<bool> checked) {
        return CallUIHandler(name, widgetId.value_or(""), value.value_or(0.0f),
                             checked.value_or(false));
    };

    // ------------------------------------------------------------------
    // Particles.
    // ------------------------------------------------------------------
    auto applyParticleProps = [readNumber, readBool, readVec3, readVec4](
                                  ParticleEmitterComponent& emitter,
                                  const sol::table& props) {
        bool boolean = false;
        float number = 0.0f;
        glm::vec3 vector3{0.0f};
        glm::vec4 color{1.0f};

        if (readBool(props.get<sol::object>("enabled"), boolean)) emitter.enabled = boolean;
        if (readNumber(props.get<sol::object>("emit_rate"), number)) emitter.emitRate = number;
        if (readNumber(props.get<sol::object>("lifetime"), number)) emitter.lifetime = number;
        if (readNumber(props.get<sol::object>("lifetime_variance"), number)) {
            emitter.lifetimeVariance = number;
        }
        if (readNumber(props.get<sol::object>("start_size"), number)) emitter.startSize = number;
        if (readNumber(props.get<sol::object>("end_size"), number)) emitter.endSize = number;
        if (readNumber(props.get<sol::object>("velocity_variance"), number)) {
            emitter.velocityVariance = number;
        }
        if (readNumber(props.get<sol::object>("spawn_radius"), number)) emitter.spawnRadius = number;
        if (readNumber(props.get<sol::object>("drag"), number)) emitter.drag = number;
        if (readNumber(props.get<sol::object>("max_particles"), number)) {
            emitter.maxParticles = static_cast<uint32_t>(
                std::max(0.0f, number));
        }
        if (readVec3(props.get<sol::object>("velocity"), vector3)) emitter.velocity = vector3;
        if (readVec3(props.get<sol::object>("spawn_box"), vector3)) emitter.spawnBoxSize = vector3;
        if (readVec3(props.get<sol::object>("gravity"), vector3)) emitter.gravity = vector3;
        if (readVec4(props.get<sol::object>("start_color"), color)) emitter.startColor = color;
        if (readVec4(props.get<sol::object>("end_color"), color)) emitter.endColor = color;
    };

    auto particleToTable = [this, makeVec3, makeVec4](const ParticleEmitterComponent& emitter) {
        sol::table table = m_lua.create_table();
        table["enabled"] = emitter.enabled;
        table["emit_rate"] = emitter.emitRate;
        table["lifetime"] = emitter.lifetime;
        table["lifetime_variance"] = emitter.lifetimeVariance;
        table["start_size"] = emitter.startSize;
        table["end_size"] = emitter.endSize;
        table["velocity"] = makeVec3(emitter.velocity);
        table["velocity_variance"] = emitter.velocityVariance;
        table["spawn_radius"] = emitter.spawnRadius;
        table["spawn_box"] = makeVec3(emitter.spawnBoxSize);
        table["gravity"] = makeVec3(emitter.gravity);
        table["drag"] = emitter.drag;
        table["max_particles"] = static_cast<double>(emitter.maxParticles);
        table["start_color"] = makeVec4(emitter.startColor);
        table["end_color"] = makeVec4(emitter.endColor);
        return table;
    };

    particles["create"] = [this, makeEntity, applyParticleProps](
                              const std::string& name, float x, float y, float z,
                              sol::optional<sol::table> props) -> sol::object {
        if (!m_scene) return sol::make_object(m_lua, sol::nil);
        Entity entity = m_scene->CreateEntity(name);
        entity.AddComponent<LuaEntityComponent>();
        auto& transform = entity.AddComponent<TransformComponent>();
        transform.position = {x, y, z};
        auto& emitter = entity.AddComponent<ParticleEmitterComponent>();
        if (props) applyParticleProps(emitter, *props);
        return makeEntity(static_cast<uint32_t>(static_cast<entt::entity>(entity)));
    };
    particles["set"] = [this, resolve, applyParticleProps](sol::object value,
                                                           sol::table props) {
        const entt::entity entity = resolve(value);
        if (entity == entt::null) return false;
        auto& registry = m_scene->Registry();
        if (!registry.all_of<ParticleEmitterComponent>(entity)) return false;
        applyParticleProps(registry.get<ParticleEmitterComponent>(entity), props);
        return true;
    };
    particles["get"] = [this, resolve, particleToTable](sol::object value) -> sol::object {
        const entt::entity entity = resolve(value);
        if (entity == entt::null) return sol::make_object(m_lua, sol::nil);
        auto& registry = m_scene->Registry();
        if (!registry.all_of<ParticleEmitterComponent>(entity)) {
            return sol::make_object(m_lua, sol::nil);
        }
        return sol::make_object(m_lua,
                                particleToTable(registry.get<ParticleEmitterComponent>(entity)));
    };

    // ------------------------------------------------------------------
    // Engine / lifecycle.
    // ------------------------------------------------------------------
    engine["version"] = "0.3.0";
    engine["time"] = [this]() { return m_time; };
    engine["delta_time"] = [this]() { return static_cast<double>(m_deltaTime); };
    engine["frame"] = [this]() { return static_cast<double>(m_frame); };
    engine["quit"] = [this]() {
        if (m_requestExit) {
            m_requestExit();
            return true;
        }
        return false;
    };
    engine["log"] = logFn;
    engine["warn"] = warnFn;
    engine["error"] = errorFn;

    // Explicitly release the temporary tables; all bound functions are owned
    // by the Lua state / m_api now.
    (void)world;
    (void)input;
    (void)physics;
    (void)audio;
    (void)animation;
    (void)ai;
    (void)navigation;
    (void)ui;
    (void)particles;
    (void)engine;
}

void ScriptEngine::BindFunctions() {
    BuildApi();
}

void ScriptEngine::DestroyEntity(entt::entity entity) {
    if (!m_scene || !m_scene->Registry().valid(entity)) return;
    auto& registry = m_scene->Registry();

    if (registry.all_of<RigidBodyComponent>(entity)) {
        auto& body = registry.get<RigidBodyComponent>(entity);
        if (body.bodyId != 0xFFFFFFFFu && m_physics.destroyBody) {
            m_physics.destroyBody(body.bodyId);
        }
    }
    if (registry.all_of<SoftBodyComponent>(entity)) {
        auto& softBody = registry.get<SoftBodyComponent>(entity);
        if (softBody.softBodyId != 0xFFFFFFFFu && m_physics.destroyBody) {
            m_physics.destroyBody(softBody.softBodyId);
        }
    }

    registry.destroy(entity);
}

void ScriptEngine::BindECS(Scene& scene) {
    BuildApi();
    m_scene = &scene;
}

void ScriptEngine::BindAssets(AssetManager* assets) {
    BuildApi();
    m_assets = assets;
}

void ScriptEngine::BindInput(std::function<bool(const std::string&)> isKeyDown) {
    BuildApi();
    m_isKeyDown = std::move(isKeyDown);
}

void ScriptEngine::BindInputAxes(
    std::function<float(const std::string&)> getAxis,
    std::function<float(const std::string&)> getAxisRaw,
    std::function<glm::vec2()> getMousePosition,
    std::function<glm::vec2()> getMouseDelta,
    std::function<float()> getMouseWheel,
    std::function<bool(const std::string&)> isMouseButtonDown) {
    BuildApi();
    m_getAxis = std::move(getAxis);
    m_getAxisRaw = std::move(getAxisRaw);
    m_getMousePosition = std::move(getMousePosition);
    m_getMouseDelta = std::move(getMouseDelta);
    m_getMouseWheel = std::move(getMouseWheel);
    m_isMouseButtonDown = std::move(isMouseButtonDown);
}

void ScriptEngine::BindVelocity(
    std::function<glm::vec3(uint32_t)> getVel,
    std::function<void(uint32_t, const glm::vec3&)> setVel) {
    BuildApi();
    m_getVelocity = std::move(getVel);
    m_setVelocity = std::move(setVel);
}

void ScriptEngine::BindAudio(
    std::function<void(const std::string&, bool)> playSound,
    std::function<void(const std::string&)> stopSound,
    std::function<bool(const std::string&)> isSoundPlaying,
    std::function<void()> stopAllSounds) {
    BuildApi();
    m_playSound = std::move(playSound);
    m_stopSound = std::move(stopSound);
    m_isSoundPlaying = std::move(isSoundPlaying);
    m_stopAllSounds = std::move(stopAllSounds);
}

void ScriptEngine::BindAudio(std::function<void(const std::string&)> playSound) {
    BuildApi();
    m_playSound = [play = std::move(playSound)](const std::string& path, bool) {
        if (play) play(path);
    };
}

void ScriptEngine::BindPhysics(const PhysicsBindings& bindings) {
    BuildApi();
    m_physics = bindings;
}

void ScriptEngine::BindPhysics(
    std::function<uint32_t(const glm::vec3&, const glm::vec3&, entt::entity)>
        createBox) {
    BuildApi();
    PhysicsBindings bindings;
    if (createBox) {
        bindings.createBox = [createBox = std::move(createBox)](
                                 const glm::vec3& halfExtents,
                                 const glm::vec3& position,
                                 entt::entity entity,
                                 bool) {
            return createBox(halfExtents, position, entity);
        };
    }
    m_physics = std::move(bindings);
}

void ScriptEngine::BindImpulse(
    std::function<void(uint32_t, const glm::vec3&)> applyImpulse) {
    BuildApi();
    m_applyImpulse = std::move(applyImpulse);
}

void ScriptEngine::BindAI() {
    BuildApi();
}

void ScriptEngine::BindNavigation(
    std::function<bool(const glm::vec3&, const glm::vec3&,
                       std::vector<glm::vec3>&)> findPath,
    std::function<bool()> buildNavMesh) {
    BuildApi();
    m_findPath = std::move(findPath);
    m_buildNavMesh = std::move(buildNavMesh);
}

void ScriptEngine::BindEngine(std::function<void()> requestExit) {
    BuildApi();
    m_requestExit = std::move(requestExit);
}

void ScriptEngine::RunAIScripts(Scene& scene, float dt) {
    auto& reg = scene.Registry();
    auto view = reg.view<AIControllerComponent>();
    for (auto e : view) {
        auto& aic = view.get<AIControllerComponent>(e);
        if (aic.scriptPath.empty()) continue;
        uint32_t eid = (uint32_t)e;
        auto& instance = m_aiControllers[eid];
        if (instance.loadedPath != aic.scriptPath) {
            try {
                instance.env = sol::environment(m_lua, sol::create, m_lua.globals());
                m_lua.script_file(aic.scriptPath, instance.env);
                instance.updateFunc = instance.env["update_ai"];
                instance.loadedPath = aic.scriptPath;
                spdlog::info("[AI] Loaded script for entity {}: {}", eid, aic.scriptPath);
            } catch (const sol::error& err) {
                AddLog(std::string("[AI ERROR] ") + err.what());
                instance.loadedPath = "ERROR_" + aic.scriptPath;
            }
        }
        if (instance.updateFunc.valid()) {
            sol::protected_function_result result = instance.updateFunc(eid, dt);
            if (!result.valid()) {
                sol::error err = result;
                AddLog(std::string("[AI ERROR] ") + err.what());
            }
        }
    }
}

void ScriptEngine::ClearLuaEntities() {
    if (!m_scene) return;
    auto view = m_scene->Registry().view<LuaEntityComponent>();
    std::vector<entt::entity> to_destroy(view.begin(), view.end());
    for (auto entity : to_destroy) DestroyEntity(entity);
    if (!to_destroy.empty()) AddLog("Cleared " + std::to_string(to_destroy.size()) +
                                   " script-spawned entities");
}

bool ScriptEngine::CallUIHandler(const std::string& handler,
                                 const std::string& widgetId,
                                 float value,
                                 bool checked) {
    if (handler.empty()) return false;

    sol::protected_function function;
    const auto it = m_uiHandlers.find(handler);
    if (it != m_uiHandlers.end()) {
        function = it->second;
    } else {
        sol::object object = m_lua[handler];
        if (!object.valid() || object.get_type() != sol::type::function) {
            spdlog::warn("[UI] Lua handler '{}' is not defined", handler);
            return false;
        }
        function = object;
    }

    sol::protected_function_result result = function(widgetId, value, checked);
    if (!result.valid()) {
        sol::error error = result;
        AddLog(std::string("[UI ERROR] ") + handler + ": " + error.what());
        return false;
    }
    return true;
}

bool ScriptEngine::HotReload(const std::string& path) {
    ClearLuaEntities();
    m_aiControllers.clear();
    m_uiHandlers.clear();
    return RunScript(path);
}

void ScriptEngine::Update(float dt) {
    m_deltaTime = dt;
    m_time += static_cast<double>(dt);
    ++m_frame;

    if (!m_runOnceInvoked) {
        // Mark as invoked before the call so a failing RunOnce is not retried
        // every frame. It is reset by the next successful script load/reload.
        m_runOnceInvoked = true;

        auto runOnceFunc = m_lua["RunOnce"];
        if (runOnceFunc.valid() && runOnceFunc.get_type() != sol::type::nil) {
            if (runOnceFunc.get_type() == sol::type::function) {
                try {
                    runOnceFunc();
                } catch (const sol::error& e) {
                    AddLog(std::string("[RUNONCE ERROR] ") + e.what());
                }
            } else {
                AddLog("[RUNONCE ERROR] RunOnce is not a function");
            }
        }
    }

    // RunAlways is the new per-frame entry point. Keep update(dt) working for
    // scripts written before the lifecycle callbacks were introduced.
    auto runAlwaysFunc = m_lua["RunAlways"];
    if (runAlwaysFunc.valid() && runAlwaysFunc.get_type() == sol::type::function) {
        try {
            runAlwaysFunc(dt);
        } catch (const sol::error& e) {
            AddLog(std::string("[RUNALWAYS ERROR] ") + e.what());
        }
        return;
    }

    auto updateFunc = m_lua["update"];
    if (updateFunc.valid() && updateFunc.get_type() == sol::type::function) {
        try {
            updateFunc(dt);
        } catch (const sol::error& e) {
            AddLog(std::string("[UPDATE ERROR] ") + e.what());
        }
    }
}

} // namespace Galeshi

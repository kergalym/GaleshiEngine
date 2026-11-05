#pragma once

#include "GaleshiEngine/Scripting/ScriptEngine.hpp"
#include "GaleshiEngine/Scene/Scene.hpp"
#include "GaleshiEngine/UI/UIWidgets.hpp"
#include "ScriptEngineFields.hpp"

#include <filesystem>
#include <glm/glm.hpp>
#include <string>

namespace Galeshi {

struct ScriptApiBuildContext {
    sol::table world;
    sol::table input;
    sol::table physics;
    sol::table audio;
    sol::table animation;
    sol::table ai;
    sol::table navigation;
    sol::table ui;
    sol::table particles;
    sol::table engine;
    sol::table camera;
    sol::table player;
    sol::table shader;

    std::function<bool(const sol::object&, float&)> readNumber;
    std::function<bool(const sol::object&, bool&)> readBool;
    std::function<bool(const sol::object&, std::string&)> readString;
    std::function<bool(const sol::object&, glm::vec2&)> readVec2;
    std::function<bool(const sol::object&, glm::vec3&)> readVec3;
    std::function<bool(const sol::object&, glm::vec4&)> readVec4;
    std::function<bool(const sol::object&, glm::quat&)> readQuat;
    std::function<bool(sol::variadic_args, glm::vec3&)> readVec3Args;
    std::function<sol::table(const glm::vec2&)> makeVec2;
    std::function<sol::table(const glm::vec3&)> makeVec3;
    std::function<sol::table(const glm::vec4&)> makeVec4;
    std::function<sol::table(const glm::quat&)> makeQuat;
    std::function<void(const std::string&)> logFn;
    std::function<void(const std::string&)> warnFn;
    std::function<void(const std::string&)> errorFn;

    std::function<bool(uint32_t)> validId;
    std::function<uint32_t(const sol::object&)> idFromValue;
    std::function<entt::entity(const sol::object&)> resolve;
    std::function<uint32_t(const sol::table&)> idFromTable;
    std::function<entt::entity(const sol::table&)> resolveTable;
    std::function<sol::table(uint32_t)> makeEntityTable;
    std::function<sol::object(uint32_t)> makeEntity;
    std::function<TransformComponent*(entt::entity, bool)> getTransform;
};

namespace ScriptEngineInternal {

std::string NormalizeKey(std::string value);

UIWidgetType UIWidgetTypeFromName(const std::string& value);
UICanvas* FindCanvasById(Scene& scene, const std::string& id);
UIWidget* FindWidgetById(UICanvas& canvas, const std::string& id);

std::string LuaValueToString(const sol::object& value);
int AudioBusIndexFromName(const std::string& name);
const char* WidgetTypeName(UIWidgetType type);
float InterpFactor(float deltaTime, float speed);
GaleshiScriptVec3 ToScriptVec3(const glm::vec3& value);
glm::vec3 FromScriptVec3(const GaleshiScriptVec3& value);
bool IsCppScriptPath(const std::filesystem::path& path);

bool ReadWorkerPacket(int fd, CppWorkerPacket& packet);
bool WriteWorkerPacket(int fd, const CppWorkerPacket& packet);

bool CompileCppScript(const std::filesystem::path& source,
                      const std::filesystem::path& output,
                      const std::string& includeRoot,
                      const std::string& projectRoot,
                      std::string& compilerOutput,
                      const std::string& extraInclude = {},
                      bool syntaxOnly = false);

} // namespace ScriptEngineInternal
} // namespace Galeshi

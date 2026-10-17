#pragma once
#include <sol/sol.hpp>
#include <entt/entt.hpp>
#include <string>
#include <iostream>
#include <glm/glm.hpp>

namespace Galeshi {
class ScriptEngine {
public:
    ScriptEngine() {
        m_lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table);
        BindFunctions();
    }
    bool RunScript(const std::string& filepath) {
        try { m_lua.script_file(filepath); return true; }
        catch (const sol::error& e) { std::cerr << "Lua Error: " << e.what() << std::endl; return false; }
    }
    void BindFunctions() {
        m_lua.set_function("log", [](const std::string& msg) { std::cout << "[Lua] " << msg << std::endl; });
    }
    // Заглушка для ECS биндинга
    void BindECS(entt::registry& reg) {
        // Пример: m_lua.set_function("translate", ...)
    }
    sol::state& GetState() { return m_lua; }
private:
    sol::state m_lua;
};
}

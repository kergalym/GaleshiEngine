#include "GaleshiEngine/Editor/ContentBrowser.hpp"
#include "GaleshiEngine/Assets/AssetManager.hpp"
#include "GaleshiEngine/Assets/Texture.hpp"

#include <imgui.h>
#include <spdlog/spdlog.h>
#include <bgfx/bgfx.h>
#include <cctype>
#include <cstdio>
#include <fstream>

#include <algorithm>

namespace fs = std::filesystem;

namespace Galeshi {

namespace {

struct ScriptTemplate {
    const char* id;
    const char* label;
    const char* description;
    const char* defaultName;
    const char* source;
};

const ScriptTemplate kScriptTemplates[] = {
    {
        "empty_header_cpp",
        "Empty Header (C++)",
        "Empty C++ header for gameplay code.",
        "empty_header.h",
        R"CPP(#pragma once

// Gameplay header.
)CPP"
    },
    {
        "empty_script_cpp",
        "Empty Script (C++)",
        "Empty hot-reloaded C++ script.",
        "empty_script.cpp",
        R"CPP(#include "GaleshiEngine/Scripting/CppScriptApi.hpp"

static const GaleshiScriptApi* g_api = nullptr;

extern "C" void GaleshiScriptRegister(const GaleshiScriptApi* api) {
    g_api = api;
}

extern "C" void GaleshiScriptUpdate(float dt) {
    (void)dt;
    (void)g_api;
}
)CPP"
    },
    {
        "empty",
        "Empty Script",
        "Lifecycle callbacks only.",
        "empty_script.lua",
        R"LUA(-- Empty Galeshi script.
-- RunOnce() is called once after this file is loaded or hot-reloaded.
-- RunAlways(dt) is called once per frame.

function RunOnce()
end

function RunAlways(dt)
end
)LUA"
    },
    {
        "shader_glsl",
        "Fragment Shader (GLSL)",
        "GLSL fragment shader with a live Code Editor preview.",
        "new_shader.glsl",
        R"GLSL($input v_texcoord0
#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);
uniform vec4 u_shaderTime;
uniform vec4 u_shaderResolution;

void main()
{
    vec2 uv = v_texcoord0;
    vec4 color = texture2D(s_texColor, uv);
    float pulse = 0.5 + 0.5 * sin(u_shaderTime.x * 2.0);
    color.rgb *= mix(1.0, pulse, 0.3);
    gl_FragColor = color;
}
)GLSL"
    },
    {
        "shader_hlsl",
        "Fragment Shader (HLSL)",
        "HLSL fragment shader with a live Code Editor preview.",
        "new_shader.hlsl",
        R"HLSL($input v_texcoord0
#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);
uniform vec4 u_shaderTime;
uniform vec4 u_shaderResolution;

void main()
{
    vec2 uv = v_texcoord0;
    vec4 color = texture2D(s_texColor, uv);
    float pulse = 0.5 + 0.5 * sin(u_shaderTime.x * 2.0);
    color.rgb *= mix(1.0, pulse, 0.3);
    gl_FragColor = color;
}
)HLSL"
    },
    {
        "shader_vertex_glsl",
        "Vertex Shader (GLSL)",
        "GLSL vertex shader with a live Code Editor preview.",
        "new_vertex_shader.glsl",
        R"GLSL($input a_position
$output v_texcoord0
#include <bgfx_shader.sh>

uniform vec4 u_shaderTime;

void main()
{
    vec3 position = a_position;
    float wave = sin(position.y * 8.0 + u_shaderTime.x * 3.0);
    position.x += wave * 0.08;
    gl_Position = vec4(position.xy, 0.0, 1.0);
    v_texcoord0 = vec2(position.x * 0.5 + 0.5,
                       1.0 - (position.y * 0.5 + 0.5));
}
)GLSL"
    },
    {
        "shader_vertex_hlsl",
        "Vertex Shader (HLSL)",
        "HLSL vertex shader with a live Code Editor preview.",
        "new_vertex_shader.hlsl",
        R"HLSL($input a_position
$output v_texcoord0
#include <bgfx_shader.sh>

uniform vec4 u_shaderTime;

void main()
{
    vec3 position = a_position;
    float wave = sin(position.y * 8.0 + u_shaderTime.x * 3.0);
    position.x += wave * 0.08;
    gl_Position = vec4(position.x, position.y, 0.0, 1.0);
    v_texcoord0 = vec2(position.x * 0.5 + 0.5,
                       1.0 - (position.y * 0.5 + 0.5));
}
)HLSL"
    },
    {
        "player_controller",
        "Player Controller",
        "Player movement, jumping and ASME Speed for an entity named Player.",
        "player_controller.lua",
        R"LUA(-- Player Controller
-- Assign this file in Edit -> Scripts, or use it as the main script.
local playerName = "Player"
local moveSpeed = 8.0
local turnSpeed = 120.0
local jumpImpulse = 8.0
local player = nil
local animationEntity = nil

local function findAnimationEntity(entity)
    if not entity or not entity:valid() then return nil end
    if entity:has_component("animation_state_machine") then
        return entity
    end
    local children = entity:children()
    for i = 1, #children do
        local found = findAnimationEntity(children[i])
        if found then return found end
    end
    return nil
end

local function acquirePlayer()
    player = Galeshi.world.find(playerName)
    animationEntity = findAnimationEntity(player)
end

function RunOnce()
    acquirePlayer()
end

function RunAlways(dt)
    if not player or not player:valid() then
        acquirePlayer()
        if not player then return end
    end
    if not animationEntity or not animationEntity:valid() then
        animationEntity = findAnimationEntity(player)
    end

    local velocity = Galeshi.physics.get_velocity(player)
    local horizontal = Galeshi.input.axis("Horizontal")
    local vertical = Galeshi.input.axis("Vertical")
    local strafing = Galeshi.player.strafing()
    local forward = Galeshi.camera.forward()
    local vx, vz = 0.0, 0.0

    if math.abs(vertical) > 0.001 then
        -- W/S face the character along the camera direction.
        player:set_euler(
            0.0, math.deg(math.atan(forward.x, forward.z)), 0.0)
        vx = forward.x * vertical * moveSpeed
        vz = forward.z * vertical * moveSpeed
    elseif not strafing and math.abs(horizontal) > 0.001 then
        local rotation = player:rotation()
        local yaw = math.deg(2.0 * math.atan(rotation.y, rotation.w))
        player:set_euler(
            0.0, yaw - horizontal * turnSpeed * dt, 0.0)
    end

    if strafing then
        -- A/D move sideways instead of turning.
        local move = Galeshi.camera.movement(horizontal, vertical)
        vx = move.x * moveSpeed
        vz = move.z * moveSpeed
    end

    Galeshi.physics.set_velocity(player, vx, velocity.y, vz)

    if Galeshi.input.axis("Jump") > 0.5 and math.abs(velocity.y) < 0.5 then
        Galeshi.physics.add_impulse(player, 0.0, jumpImpulse, 0.0)
    end

    if animationEntity then
        local speed = math.sqrt(vx * vx + vz * vz) / moveSpeed
        Galeshi.animation.set_parameter(
            animationEntity, "Speed", math.min(speed, 1.0))
    end
end
)LUA"
    },
    {
        "player_controller_cpp",
        "Player Controller (C++)",
        "Hot-reloaded C++ counterpart of the player controller. Updates "
        "ASME Speed.",
        "player_controller.cpp",
        R"CPP(// Player Controller (C++ hot-reload)
#include "GaleshiEngine/Scripting/CppGameplayApi.hpp"

#include <algorithm>
#include <cmath>

namespace {

using Galeshi::Script::Api;
using Galeshi::Script::Quat;
using Galeshi::Script::Vec3;

constexpr const char* kPlayerName = "Player";
constexpr const char* kPlayerVisualName = "PlayerVisual";
constexpr float kMoveSpeed = 8.0f;
constexpr float kTurnSpeed = 120.0f;
constexpr float kJumpImpulse = 8.0f;

Api* g_api = nullptr;
uint32_t g_player = UINT32_MAX;
uint32_t g_animation = UINT32_MAX;

Quat YawQuat(float yaw) {
    return {0.0f, std::sin(yaw * 0.5f), 0.0f, std::cos(yaw * 0.5f)};
}

float YawFromRotation(Quat rotation) {
    return 2.0f * std::atan2(rotation.y, rotation.w);
}

uint32_t FindAnimationEntity(Api& api, uint32_t player) {
    if (api.world.HasComponent(player, "animation_state_machine")) {
        return player;
    }
    const uint32_t visual = api.world.Find(kPlayerVisualName);
    if (api.world.Valid(visual) &&
        api.world.HasComponent(visual, "animation_state_machine")) {
        return visual;
    }
    return player;
}

void AcquirePlayer(Api& api) {
    g_player = api.world.Find(kPlayerName);
    g_animation = g_player == UINT32_MAX
        ? UINT32_MAX : FindAnimationEntity(api, g_player);
}

} // namespace

extern "C" void GaleshiScriptRegister(const GaleshiScriptApi* api) {
    static Galeshi::Script::Api wrapper(api);
    g_api = &wrapper;
    g_player = UINT32_MAX;
    g_animation = UINT32_MAX;
}

extern "C" void GaleshiScriptUpdate(float dt) {
    if (!g_api || !g_api->Compatible() || dt <= 0.0f) return;
    Api& api = *g_api;

    if (g_player == UINT32_MAX || !api.world.Valid(g_player)) {
        AcquirePlayer(api);
        if (g_player == UINT32_MAX) return;
    }
    if (g_animation == UINT32_MAX || !api.world.Valid(g_animation)) {
        g_animation = FindAnimationEntity(api, g_player);
    }

    const float horizontal = api.input.Axis("Horizontal");
    const float vertical = api.input.Axis("Vertical");
    const bool strafing = api.player.Strafing();
    const Vec3 forward = api.camera.Forward();
    const Vec3 velocity = api.physics.Velocity(g_player);
    float vx = 0.0f;
    float vz = 0.0f;

    if (std::abs(vertical) > 0.001f) {
        api.world.SetRotation(
            g_player, YawQuat(std::atan2(forward.x, forward.z)));
        vx = forward.x * vertical * kMoveSpeed;
        vz = forward.z * vertical * kMoveSpeed;
    } else if (!strafing && std::abs(horizontal) > 0.001f) {
        const float yaw = YawFromRotation(api.world.Rotation(g_player)) -
                          horizontal * kTurnSpeed * dt;
        api.world.SetRotation(g_player, YawQuat(yaw));
    }

    if (strafing) {
        const Vec3 move = api.camera.Movement(horizontal, vertical);
        vx = move.x * kMoveSpeed;
        vz = move.z * kMoveSpeed;
    }

    if (api.world.HasComponent(g_player, "rigid_body")) {
        api.physics.SetVelocity(g_player, {vx, velocity.y, vz});
        if (api.input.Axis("Jump") > 0.5f &&
            std::abs(velocity.y) < 0.5f) {
            api.physics.ApplyImpulse(
                g_player, {0.0f, kJumpImpulse, 0.0f});
        }
    } else {
        Vec3 position = api.world.Position(g_player);
        position.x += vx * dt;
        position.z += vz * dt;
        api.world.SetPosition(g_player, position);
    }

    if (g_animation != UINT32_MAX) {
        const float speed = std::min(
            1.0f, std::sqrt(vx * vx + vz * vz) / kMoveSpeed);
        api.animation.SetParameter(g_animation, "Speed", speed);
    }
}
)CPP"
    },
    {
        "ai_controller",
        "AI Controller",
        "Chase an entity named Player and update ASME Speed.",
        "ai_controller.lua",
        R"LUA(-- AI Controller
-- Attach this file to an entity with an AI Controller component.
local targetName = "Player"
local moveSpeed = 3.5
local chaseRadius = 20.0
local stopRadius = 1.0
local animationEntity = nil
local lastPosition = nil

local function findAnimationEntity(entity)
    if not entity or not entity:valid() then return nil end
    if entity:has_component("animation_state_machine") then
        return entity
    end
    local children = entity:children()
    for i = 1, #children do
        local found = findAnimationEntity(children[i])
        if found then return found end
    end
    return nil
end

function update_ai(eid, dt)
    local agent = Galeshi.world.entity(eid)
    if not agent or not agent:valid() then return end
    if not animationEntity or not animationEntity:valid() then
        animationEntity = findAnimationEntity(agent)
    end

    local agentPosition = agent:position()
    local actualSpeed = 0.0
    if lastPosition and dt > 0.0 then
        local dx = agentPosition.x - lastPosition.x
        local dz = agentPosition.z - lastPosition.z
        actualSpeed = math.sqrt(dx * dx + dz * dz) / dt
    end
    lastPosition = {
        x = agentPosition.x,
        y = agentPosition.y,
        z = agentPosition.z,
    }

    Galeshi.ai.set_speed(agent, moveSpeed)
    local target = Galeshi.world.find(targetName)
    if target and target:valid() then
        local position = target:position()
        local dx = position.x - agentPosition.x
        local dz = position.z - agentPosition.z
        local distanceSq = dx * dx + dz * dz
        if distanceSq <= chaseRadius * chaseRadius and
           distanceSq > stopRadius * stopRadius then
            Galeshi.ai.set_target(
                agent, position.x, position.y, position.z)
        else
            Galeshi.ai.clear_target(agent)
        end
    else
        Galeshi.ai.clear_target(agent)
    end

    if animationEntity then
        local speed = math.min(actualSpeed / moveSpeed, 1.0)
        Galeshi.animation.set_parameter(animationEntity, "Speed", speed)
    end
end
)LUA"
    },
    {
        "ai_controller_cpp",
        "AI Controller (C++)",
        "Hot-reloaded C++ counterpart of the AI controller. Updates ASME "
        "Speed.",
        "ai_controller.cpp",
        R"CPP(// AI Controller (C++ hot-reload)
//
// C++ scripts update globally, so this drives the first NavAgent in the
// scene. Rename kAgentVisualName if the ASME lives on a child model.
#include "GaleshiEngine/Scripting/CppGameplayApi.hpp"

#include <algorithm>
#include <cmath>

namespace {

using Galeshi::Script::Api;
using Galeshi::Script::Vec3;

constexpr const char* kTargetName = "Player";
constexpr const char* kAgentVisualName = "AIVisual";
constexpr float kMoveSpeed = 3.5f;
constexpr float kChaseRadius = 20.0f;
constexpr float kStopRadius = 1.0f;

Api* g_api = nullptr;
uint32_t g_agent = UINT32_MAX;
uint32_t g_animation = UINT32_MAX;
Vec3 g_lastPosition{};
bool g_hasLastPosition = false;

uint32_t FindAnimationEntity(Api& api, uint32_t agent) {
    if (api.world.HasComponent(agent, "animation_state_machine")) {
        return agent;
    }
    const uint32_t visual = api.world.Find(kAgentVisualName);
    if (api.world.Valid(visual) &&
        api.world.HasComponent(visual, "animation_state_machine")) {
        return visual;
    }
    return agent;
}

void AcquireAgent(Api& api) {
    g_agent = api.ai.FindNavAgent();
    if (g_agent == UINT32_MAX) {
        g_animation = UINT32_MAX;
        g_hasLastPosition = false;
        return;
    }
    g_animation = FindAnimationEntity(api, g_agent);
    g_lastPosition = api.world.Position(g_agent);
    g_hasLastPosition = true;
}

} // namespace

extern "C" void GaleshiScriptRegister(const GaleshiScriptApi* api) {
    static Galeshi::Script::Api wrapper(api);
    g_api = &wrapper;
    g_agent = UINT32_MAX;
    g_animation = UINT32_MAX;
    g_hasLastPosition = false;
}

extern "C" void GaleshiScriptUpdate(float dt) {
    if (!g_api || !g_api->Compatible() || dt <= 0.0f) return;
    Api& api = *g_api;

    if (g_agent == UINT32_MAX || !api.world.Valid(g_agent)) {
        AcquireAgent(api);
        if (g_agent == UINT32_MAX) return;
    }
    if (g_animation == UINT32_MAX || !api.world.Valid(g_animation)) {
        g_animation = FindAnimationEntity(api, g_agent);
    }

    const Vec3 agentPosition = api.world.Position(g_agent);
    float actualSpeed = 0.0f;
    if (g_hasLastPosition) {
        const float dx = agentPosition.x - g_lastPosition.x;
        const float dz = agentPosition.z - g_lastPosition.z;
        actualSpeed = std::sqrt(dx * dx + dz * dz) / dt;
    }
    g_lastPosition = agentPosition;
    g_hasLastPosition = true;

    const uint32_t target = api.world.Find(kTargetName);
    if (api.world.Valid(target)) {
        const Vec3 targetPosition = api.world.Position(target);
        const float dx = targetPosition.x - agentPosition.x;
        const float dz = targetPosition.z - agentPosition.z;
        const float distanceSq = dx * dx + dz * dz;
        if (distanceSq <= kChaseRadius * kChaseRadius &&
            distanceSq > kStopRadius * kStopRadius) {
            api.ai.SetFloat(g_agent, "nav_agent", "speed", kMoveSpeed);
            api.ai.SetVec3(g_agent, "nav_agent", "target_position",
                           targetPosition);
            api.ai.SetFlag(g_agent, "nav_agent", "has_target", true);
        } else {
            api.ai.SetFlag(g_agent, "nav_agent", "has_target", false);
        }
    }

    if (g_animation != UINT32_MAX) {
        const float speed = std::min(
            1.0f, actualSpeed / kMoveSpeed);
        api.animation.SetParameter(g_animation, "Speed", speed);
    }
}
)CPP"
    },
    {
        "ai_patrol",
        "AI Patrol",
        "Patrol between four waypoints.",
        "ai_patrol.lua",
        R"LUA(-- AI Patrol
-- Attach this file to an entity with an AI Controller component.
local waypoints = {
    { x =  5, y = 0, z =  5 },
    { x = -5, y = 0, z =  5 },
    { x = -5, y = 0, z = -5 },
    { x =  5, y = 0, z = -5 },
}
local currentWaypoint = 1
local waitTimer = 0.0

function update_ai(eid, dt)
    local agent = Galeshi.world.entity(eid)
    if not agent or not agent:valid() then return end

    if Galeshi.ai.path_done(agent) then
        waitTimer = waitTimer + dt
        if waitTimer >= 1.5 then
            waitTimer = 0.0
            currentWaypoint = (currentWaypoint % #waypoints) + 1
            local waypoint = waypoints[currentWaypoint]
            Galeshi.ai.set_target(agent, waypoint.x, waypoint.y, waypoint.z)
        end
    end
end
)LUA"
    },
    {
        "ui_menu",
        "UI Menu",
        "Register handlers for UI Designer widgets.",
        "ui_menu.lua",
        R"LUA(-- UI Menu
-- Set these handler names on canvas/widgets in the UI Designer.
Galeshi.ui.on("on_menu_open", function()
    Galeshi.log("UI: menu opened")
end)

Galeshi.ui.on("on_play_clicked", function(widgetId)
    Galeshi.log("UI: " .. tostring(widgetId) .. " clicked")
end)

Galeshi.ui.on("on_volume_changed", function(widgetId, value)
    Galeshi.audio.set_bus("SFX", value)
end)

Galeshi.ui.on("on_fullscreen_toggled", function(widgetId, value, checked)
    Galeshi.log("UI: fullscreen=" .. tostring(checked))
end)
)LUA"
    },
    {
        "rotator",
        "Object Rotator",
        "Rotate an entity named Rotator.",
        "object_rotator.lua",
        R"LUA(-- Object Rotator
local targetName = "Rotator"
local degreesPerSecond = 45.0
local target = nil

local function acquireTarget()
    target = Galeshi.world.find(targetName)
end

function RunOnce()
    acquireTarget()
end

function RunAlways(dt)
    if not target or not target:valid() then
        acquireTarget()
        return
    end

    local euler = target:euler()
    target:set_euler(euler.x, euler.y + degreesPerSecond * dt, euler.z)
end
)LUA"
    },
    {
        "physics_demo",
        "Physics Demo",
        "Spawn and push a dynamic box.",
        "physics_demo.lua",
        R"LUA(-- Physics Demo
local cube = nil

function RunOnce()
    cube = Galeshi.physics.create_box("ScriptCube", 0.0, 6.0, 0.0, 1.0)
    if cube then
        Galeshi.physics.set_velocity(cube, 2.0, 0.0, 1.0)
    end
end

function RunAlways(dt)
end
)LUA"
    },
};

const ScriptTemplate* FindScriptTemplate(const std::string& id) {
    for (const ScriptTemplate& scriptTemplate : kScriptTemplates) {
        if (id == scriptTemplate.id) return &scriptTemplate;
    }
    return nullptr;
}

const char* kNewLuaSource =
    "-- New Galeshi script.\n"
    "\n"
    "function RunOnce()\n"
    "end\n"
    "\n"
    "function RunAlways(dt)\n"
    "end\n";

bool IsSubPath(const fs::path& child, const fs::path& parent) {
    std::error_code ec;
    const fs::path canonicalChild = fs::weakly_canonical(child, ec);
    if (ec) return false;
    ec.clear();
    const fs::path canonicalParent = fs::weakly_canonical(parent, ec);
    if (ec) return false;
    if (canonicalChild == canonicalParent) return true;
    const fs::path relative = canonicalChild.lexically_relative(canonicalParent);
    return !relative.empty() && *relative.begin() != "..";
}

} // namespace

ContentBrowser::ContentBrowser(std::string rootDir, AssetManager* assets)
    : m_root(std::move(rootDir)), m_assets(assets), m_current(m_root) {
    Refresh();
}

void ContentBrowser::Refresh() {
    m_entries.clear();
    std::error_code ec;
    if (!fs::exists(m_current, ec)) return;

    for (auto& p : fs::directory_iterator(m_current, ec)) {
        Entry e;
        e.path = p.path();
        e.isDirectory = p.is_directory();
        e.name = p.path().filename().string();
        if (!e.isDirectory) {
            std::error_code sizeEc;
            e.size = p.file_size(sizeEc);
            if (sizeEc) e.size = 0;
        }
        m_entries.push_back(e);
    }
    std::sort(m_entries.begin(), m_entries.end(),
        [](const Entry& a, const Entry& b) {
            if (a.isDirectory != b.isDirectory) return a.isDirectory > b.isDirectory;
            return a.name < b.name;
        });

    // Remember the directory timestamp so UpdateAutoRefresh() can detect
    // external changes (new/deleted/renamed files).
    std::error_code timeEc;
    const auto writeTime = fs::last_write_time(m_current, timeEc);
    if (!timeEc) {
        m_lastWriteTime = writeTime;
    }
}

bool ContentBrowser::ImportFiles(const std::vector<std::string>& files,
                                 const std::filesystem::path& destDir) {
    if (files.empty() || destDir.empty()) return false;

    bool imported = false;
    for (const std::string& sourcePath : files) {
        if (sourcePath.empty()) continue;
        const fs::path source(sourcePath);
        std::error_code ec;
        if (!fs::is_regular_file(source, ec)) {
            spdlog::warn("[ContentBrowser] Skipping non-file drop '{}'",
                         sourcePath);
            continue;
        }

        const fs::path destination = destDir / source.filename();
        fs::create_directories(destination.parent_path(), ec);
        if (ec) {
            spdlog::error("[ContentBrowser] Cannot create directory '{}': {}",
                          destination.parent_path().string(), ec.message());
            continue;
        }

        fs::copy_file(source, destination,
                      fs::copy_options::overwrite_existing, ec);
        if (ec) {
            spdlog::error("[ContentBrowser] Failed to import '{}' to '{}': {}",
                          sourcePath, destination.string(), ec.message());
            continue;
        }

        spdlog::info("[ContentBrowser] Imported '{}' -> '{}'",
                     sourcePath, destination.string());
        imported = true;
    }

    if (imported) Refresh();
    return imported;
}

bool ContentBrowser::ContainsPoint(float x, float y) const {
    return x >= m_windowMinX && x <= m_windowMaxX &&
           y >= m_windowMinY && y <= m_windowMaxY;
}

std::string ContentBrowser::CreateScript(const std::string& templateId) {
    const ScriptTemplate* scriptTemplate = FindScriptTemplate(templateId);
    if (!scriptTemplate) {
        spdlog::warn("[ContentBrowser] Unknown script template '{}'", templateId);
        return {};
    }

    const fs::path scriptsDirectory = fs::path(m_root) / "scripts";
    std::error_code ec;
    fs::create_directories(scriptsDirectory, ec);
    if (ec) {
        spdlog::error("[ContentBrowser] Cannot create scripts directory '{}': {}",
                      scriptsDirectory.string(), ec.message());
        return {};
    }

    fs::path target = scriptsDirectory / scriptTemplate->defaultName;
    if (fs::exists(target, ec)) {
        const std::string stem = target.stem().string();
        const std::string extension = target.extension().string();
        bool foundFreeName = false;
        for (int suffix = 1; suffix < 10000; ++suffix) {
            target = scriptsDirectory /
                     (stem + "_" + std::to_string(suffix) + extension);
            if (!fs::exists(target, ec)) {
                foundFreeName = true;
                break;
            }
        }
        if (!foundFreeName) {
            spdlog::error("[ContentBrowser] No free filename for template '{}'",
                          scriptTemplate->label);
            return {};
        }
    }

    std::ofstream out(target, std::ios::binary);
    if (!out.is_open()) {
        spdlog::error("[ContentBrowser] Cannot create script '{}'",
                      target.string());
        return {};
    }
    out << scriptTemplate->source;
    if (!out.good()) {
        spdlog::error("[ContentBrowser] Failed to write script '{}'",
                      target.string());
        return {};
    }
    out.close();

    spdlog::info("[ContentBrowser] Created script '{}' from template '{}'",
                 target.string(), scriptTemplate->label);
    Refresh();
    return target.string();
}

std::string ContentBrowser::UniqueName(const std::filesystem::path& dir,
                                       const std::string& base,
                                       const std::string& extension,
                                       bool directory) {
    (void)directory;
    std::error_code ec;
    fs::path target = dir / (base + extension);
    if (!fs::exists(target, ec)) return target.filename().string();
    for (int suffix = 1; suffix < 10000; ++suffix) {
        target = dir / (base + "_" + std::to_string(suffix) + extension);
        ec.clear();
        if (!fs::exists(target, ec)) return target.filename().string();
    }
    return {};
}

bool ContentBrowser::ValidateName(const std::string& name,
                                  std::string& error) const {
    if (name.empty()) {
        error = "Name cannot be empty.";
        return false;
    }
    if (name == "." || name == "..") {
        error = "Invalid name.";
        return false;
    }
    if (name.find('/') != std::string::npos ||
        name.find('\\') != std::string::npos) {
        error = "Name cannot contain path separators.";
        return false;
    }
    return true;
}

bool ContentBrowser::IsInsideRoot(const std::filesystem::path& path) const {
    return IsSubPath(path, m_root);
}

void ContentBrowser::OpenDialog(Dialog dialog,
                                const std::filesystem::path& path,
                                const std::string& defaultName) {
    m_dialog = dialog;
    m_dialogPath = path;
    m_dialogError.clear();
    m_dialogPopupPending = true;
    std::string name = defaultName;
    if (name.empty()) name = path.filename().string();
    std::snprintf(m_dialogName, sizeof(m_dialogName), "%s", name.c_str());
}

bool ContentBrowser::RenameEntry(const std::filesystem::path& source,
                                 const std::string& newName) {
    std::string error;
    if (!ValidateName(newName, error)) {
        m_dialogError = error;
        return false;
    }
    const fs::path target = source.parent_path() / newName;
    std::error_code ec;
    if (fs::exists(target, ec) && target != source) {
        m_dialogError = "An entry with that name already exists.";
        return false;
    }
    if (target == source) return true;
    fs::rename(source, target, ec);
    if (ec) {
        m_dialogError = "Rename failed: " + ec.message();
        return false;
    }
    spdlog::info("[ContentBrowser] Renamed '{}' -> '{}'",
                 source.string(), target.string());
    Refresh();
    return true;
}

bool ContentBrowser::DeleteEntry(const std::filesystem::path& path) {
    if (!IsInsideRoot(path) || path == fs::path(m_root)) {
        m_dialogError = "Refusing to delete the assets root.";
        return false;
    }
    std::error_code ec;
    const bool directory = fs::is_directory(path, ec);
    ec.clear();
    const uintmax_t removed = directory ? fs::remove_all(path, ec)
                                        : fs::remove(path, ec);
    if (ec || removed == 0) {
        m_dialogError = "Delete failed: " + ec.message();
        return false;
    }
    spdlog::info("[ContentBrowser] Deleted {} '{}'",
                 directory ? "directory" : "file", path.string());
    Refresh();
    return true;
}

bool ContentBrowser::CreateFolder(const std::string& name) {
    std::string error;
    if (!ValidateName(name, error)) {
        m_dialogError = error;
        return false;
    }
    const fs::path target = m_current / name;
    std::error_code ec;
    if (fs::exists(target, ec)) {
        m_dialogError = "An entry with that name already exists.";
        return false;
    }
    if (!fs::create_directory(target, ec)) {
        m_dialogError = "Create directory failed: " + ec.message();
        return false;
    }
    spdlog::info("[ContentBrowser] Created directory '{}'", target.string());
    Refresh();
    return true;
}

bool ContentBrowser::CreateLuaFile(const std::string& name) {
    std::string filename = name;
    if (fs::path(filename).extension() != ".lua") filename += ".lua";
    std::string error;
    if (!ValidateName(filename, error)) {
        m_dialogError = error;
        return false;
    }
    const fs::path scriptsDirectory = fs::path(m_root) / "scripts";
    std::error_code ec;
    fs::create_directories(scriptsDirectory, ec);
    if (ec) {
        m_dialogError = "Cannot create scripts directory.";
        return false;
    }
    const fs::path target = scriptsDirectory / filename;
    if (fs::exists(target, ec)) {
        m_dialogError = "A file with that name already exists.";
        return false;
    }
    std::ofstream out(target, std::ios::binary);
    if (!out.is_open()) {
        m_dialogError = "Cannot create Lua file.";
        return false;
    }
    out << kNewLuaSource;
    if (!out.good()) {
        m_dialogError = "Failed to write Lua file.";
        return false;
    }
    out.close();
    spdlog::info("[ContentBrowser] Created Lua script '{}'", target.string());
    Refresh();
    return true;
}

bool ContentBrowser::MoveEntry(const std::filesystem::path& source,
                               const std::filesystem::path& destinationDir) {
    std::error_code ec;
    if (!fs::exists(source, ec) || !fs::is_directory(destinationDir, ec)) {
        spdlog::error("[ContentBrowser] Move source or destination is invalid");
        return false;
    }
    if (!IsInsideRoot(destinationDir)) {
        spdlog::error("[ContentBrowser] Move destination is outside the assets root");
        return false;
    }
    if (fs::is_directory(source, ec) && IsSubPath(destinationDir, source)) {
        spdlog::error("[ContentBrowser] Cannot move a directory into itself");
        return false;
    }

    const fs::path target = destinationDir / source.filename();
    if (target == source) {
        spdlog::warn("[ContentBrowser] Move target is the same path");
        return false;
    }
    if (fs::exists(target, ec)) {
        spdlog::error("[ContentBrowser] Move destination already exists: {}",
                      target.string());
        return false;
    }
    fs::rename(source, target, ec);
    if (ec) {
        spdlog::error("[ContentBrowser] Move failed '{}' -> '{}': {}",
                      source.string(), target.string(), ec.message());
        return false;
    }
    spdlog::info("[ContentBrowser] Moved '{}' -> '{}'",
                 source.string(), target.string());
    Refresh();
    return true;
}

void ContentBrowser::DrawDialogs() {
    if (m_dialog == Dialog::None) return;

    const char* title = nullptr;
    switch (m_dialog) {
        case Dialog::Rename:    title = "Rename Entry##content_browser"; break;
        case Dialog::Delete:    title = "Delete Entry##content_browser"; break;
        case Dialog::NewFolder: title = "New Folder##content_browser"; break;
        case Dialog::NewLua:    title = "New Lua Script##content_browser"; break;
        default: return;
    }

    if (m_dialogPopupPending) {
        ImGui::OpenPopup(title);
        m_dialogPopupPending = false;
    }
    if (!ImGui::BeginPopupModal(title, nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize |
                                ImGuiWindowFlags_NoSavedSettings)) {
        return;
    }

    bool close = false;
    if (!m_dialogError.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s",
                           m_dialogError.c_str());
        ImGui::Separator();
    }

    if (m_dialog == Dialog::Rename || m_dialog == Dialog::NewFolder ||
        m_dialog == Dialog::NewLua) {
        ImGui::Text("Name:");
        ImGui::SetNextItemWidth(320.0f);
        ImGui::InputText("##dialog_name", m_dialogName,
                         sizeof(m_dialogName));
        if (ImGui::IsItemActivated()) m_dialogError.clear();
    } else if (m_dialog == Dialog::Delete) {
        const bool directory =
            fs::is_directory(m_dialogPath);
        ImGui::TextWrapped("Delete this %s?",
                           directory ? "directory and its contents" : "file");
        ImGui::TextWrapped("%s", m_dialogPath.string().c_str());
        ImGui::TextDisabled("This cannot be undone.");
    }

    ImGui::Separator();
    if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
        close = true;
    }
    ImGui::SameLine();
    const char* confirmLabel =
        m_dialog == Dialog::Delete ? "Delete" : "Create";
    if (m_dialog == Dialog::Rename) confirmLabel = "Rename";
    if (ImGui::Button(confirmLabel, ImVec2(100.0f, 0.0f))) {
        switch (m_dialog) {
            case Dialog::Rename:
                close = RenameEntry(m_dialogPath, m_dialogName);
                break;
            case Dialog::Delete:
                close = DeleteEntry(m_dialogPath);
                break;
            case Dialog::NewFolder:
                close = CreateFolder(m_dialogName);
                break;
            case Dialog::NewLua:
                close = CreateLuaFile(m_dialogName);
                break;
            default:
                break;
        }
    }

    if (close) {
        m_dialog = Dialog::None;
        m_dialogError.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void ContentBrowser::UpdateAutoRefresh() {
    if (!m_autoRefresh) return;

    // Polling instead of platform-specific watchers keeps this portable.
    // 500 ms is fast enough to feel immediate without touching the FS on
    // every frame.
    const auto now = std::chrono::steady_clock::now();
    if (now - m_lastCheck < std::chrono::milliseconds(500)) return;
    m_lastCheck = now;

    std::error_code ec;

    // The directory we are viewing may have been deleted or moved.
    if (!fs::exists(m_current, ec)) {
        if (fs::exists(m_root, ec)) {
            m_current = m_root;
            Refresh();
        }
        return;
    }

    const auto writeTime = fs::last_write_time(m_current, ec);
    if (ec) return;

    if (m_lastWriteTime == fs::file_time_type{}) {
        // First poll after construction/refresh failure: just record it.
        m_lastWriteTime = writeTime;
        return;
    }

    if (writeTime != m_lastWriteTime) {
        Refresh();
    }
}

std::string ContentBrowser::Draw() {
    std::string clicked;

    // Remember the window bounds in single-viewport ImGui coordinates. The
    // editor uses this to route OS file-manager drops to the visible folder.
    const ImVec2 windowPos = ImGui::GetWindowPos();
    const ImVec2 windowSize = ImGui::GetWindowSize();
    m_windowMinX = windowPos.x;
    m_windowMinY = windowPos.y;
    m_windowMaxX = windowPos.x + windowSize.x;
    m_windowMaxY = windowPos.y + windowSize.y;

    // Pick up external changes to the assets folder before drawing.
    UpdateAutoRefresh();

    ImGui::Text("Path: %s", m_current.string().c_str());
    ImGui::Separator();

    if (m_current != fs::path(m_root)) {
        if (ImGui::Button("..")) {
            m_current = m_current.parent_path();
            Refresh();
        }
        ImGui::SameLine();
    }
    if (ImGui::Button("Refresh")) Refresh();
    ImGui::SameLine();
    ImGui::Checkbox("Auto", &m_autoRefresh);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Automatically refresh when files are added,\n"
                          "removed or renamed in the assets folder.");
    }
    ImGui::SameLine();
    const char* viewModeNames[] = {
        "Icon View", "Compact View", "Thumbnail View", "Detailed List"};
    int viewMode = static_cast<int>(m_viewMode);
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::Combo("##content_view_mode", &viewMode, viewModeNames,
                     IM_ARRAYSIZE(viewModeNames))) {
        m_viewMode = static_cast<ViewMode>(viewMode);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Folder view mode");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("| Drop files here to import");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Drag glTF/GLB/OBJ/DAE models, textures or audio from any\n"
            "file manager and drop them into this folder. FBX drag & drop\n"
            "is disabled; export FBX assets to glTF/GLB first.");
    }
    ImGui::SameLine();
    if (ImGui::Button("Import Asset")) {
        m_importPicker.Open(m_current.string(), "Import Asset",
                            { ".gltf", ".glb", ".obj", ".dae",
                              ".png", ".jpg", ".wav", ".mp3",
                              ".ttf", ".otf" });
    }
    ImGui::Separator();

    std::string importPath = m_importPicker.Draw();
    if (!importPath.empty()) {
        ImportFiles({importPath}, m_current);
    }

    ImGui::BeginChild("content_list", ImVec2(0, 0), true);

    const auto entryExtension = [](const Entry& entry) {
        std::string ext;
        if (!entry.isDirectory) {
            const auto dotPos = entry.name.find_last_of('.');
            if (dotPos != std::string::npos) {
                ext = entry.name.substr(dotPos);
                for (char& c : ext) {
                    c = static_cast<char>(
                        std::tolower(static_cast<unsigned char>(c)));
                }
            }
        }
        return ext;
    };
    const auto isImage = [&entryExtension](const Entry& entry) {
        const std::string ext = entryExtension(entry);
        return ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
               ext == ".bmp" || ext == ".tga" || ext == ".dds" ||
               ext == ".ktx";
    };
    const auto drawThumbnail = [this, &entryExtension, &isImage](
                                   const Entry& entry, float size) {
        const std::string ext = entryExtension(entry);
        if (!entry.isDirectory && isImage(entry) && m_assets) {
            auto texture = m_assets->LoadTexture(entry.path.string(), true);
            if (texture && bgfx::isValid(texture->Handle())) {
                ImGui::Image(
                    static_cast<ImTextureID>(texture->Handle().idx),
                    ImVec2(size, size), ImVec2(0.0f, 1.0f),
                    ImVec2(1.0f, 0.0f));
                return;
            }
        }
        if (entry.isDirectory) {
            ImGui::TextDisabled("[D]");
        } else if (isImage(entry)) {
            ImGui::TextDisabled("[IMG]");
        } else if (!ext.empty()) {
            ImGui::TextDisabled("[%s]", ext.substr(1).c_str());
        } else {
            ImGui::TextDisabled("[F]");
        }
    };
    const auto entryType = [&entryExtension](const Entry& entry) {
        if (entry.isDirectory) return std::string("Folder");
        const std::string ext = entryExtension(entry);
        if (ext.empty()) return std::string("File");
        std::string type = ext.substr(1);
        for (char& c : type) {
            c = static_cast<char>(
                std::toupper(static_cast<unsigned char>(c)));
        }
        return type + " File";
    };
    const auto formatSize = [](const Entry& entry) {
        if (entry.isDirectory) return std::string("-");
        const double size = static_cast<double>(entry.size);
        char buffer[32];
        if (size < 1024.0) {
            std::snprintf(buffer, sizeof(buffer), "%.0f B", size);
        } else if (size < 1024.0 * 1024.0) {
            std::snprintf(buffer, sizeof(buffer), "%.1f KB",
                          size / 1024.0);
        } else if (size < 1024.0 * 1024.0 * 1024.0) {
            std::snprintf(buffer, sizeof(buffer), "%.1f MB",
                          size / (1024.0 * 1024.0));
        } else {
            std::snprintf(buffer, sizeof(buffer), "%.1f GB",
                          size / (1024.0 * 1024.0 * 1024.0));
        }
        return std::string(buffer);
    };

    const auto activateEntry = [this, &clicked](const Entry& entry) {
        if (!ImGui::IsMouseDoubleClicked(0)) return;
        if (entry.isDirectory) {
            m_current = entry.path;
            Refresh();
        } else {
            clicked = entry.path.string();
        }
    };
    const auto drawEntryPopup = [this](const Entry& entry) {
        if (!ImGui::BeginPopupContextItem("entry_context")) return;
        ImGui::TextDisabled("%s", entry.name.c_str());
        ImGui::Separator();
        if (entry.isDirectory) {
            if (ImGui::MenuItem("Open")) {
                m_current = entry.path;
                Refresh();
                ImGui::CloseCurrentPopup();
            }
            ImGui::Separator();
        }
        if (ImGui::MenuItem("Rename...")) {
            OpenDialog(Dialog::Rename, entry.path, entry.name);
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("Move to...")) {
            m_moveSource = entry.path;
            m_movePicker.PickFolder(m_current.string(), "Move to Folder");
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("Delete")) {
            OpenDialog(Dialog::Delete, entry.path);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    };
    const auto drawEntryDragSource = [](const Entry& entry) {
        if (entry.isDirectory) return;
        if (!ImGui::BeginDragDropSource(
                ImGuiDragDropFlags_SourceAllowNullID)) {
            return;
        }
        const std::string path = entry.path.string();
        ImGui::SetDragDropPayload("GALESHI_ASSET_PATH", path.c_str(),
                                  path.size() + 1);
        ImGui::Text("%s", entry.name.c_str());
        ImGui::EndDragDropSource();
    };

    if (m_viewMode == ViewMode::Icon ||
        m_viewMode == ViewMode::Thumbnail) {
        const bool thumbnails = m_viewMode == ViewMode::Thumbnail;
        const int columns = thumbnails ? 3 : 4;
        const float iconSize = thumbnails ? 96.0f : 40.0f;
        if (ImGui::BeginTable(
                thumbnails ? "content_thumbnails" : "content_icons",
                columns,
                ImGuiTableFlags_SizingStretchSame |
                    ImGuiTableFlags_NoSavedSettings)) {
            for (const auto& e : m_entries) {
                ImGui::TableNextColumn();
                ImGui::PushID(e.path.string().c_str());
                ImGui::BeginGroup();
                drawThumbnail(e, iconSize);
                if (ImGui::Selectable(
                        e.name.c_str(), false,
                        ImGuiSelectableFlags_AllowDoubleClick)) {
                    activateEntry(e);
                }
                drawEntryPopup(e);
                drawEntryDragSource(e);
                ImGui::EndGroup();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    } else if (m_viewMode == ViewMode::Details) {
        const ImGuiTableFlags flags =
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings;
        if (ImGui::BeginTable("content_details", 3, flags)) {
            ImGui::TableSetupColumn(
                "Name", ImGuiTableColumnFlags_WidthStretch, 3.0f);
            ImGui::TableSetupColumn(
                "Type", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn(
                "Size", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableHeadersRow();
            for (const auto& e : m_entries) {
                ImGui::TableNextRow();
                ImGui::PushID(e.path.string().c_str());

                ImGui::TableSetColumnIndex(0);
                drawThumbnail(e, 16.0f);
                ImGui::SameLine();
                if (ImGui::Selectable(
                        e.name.c_str(), false,
                        ImGuiSelectableFlags_AllowDoubleClick)) {
                    activateEntry(e);
                }
                drawEntryPopup(e);
                drawEntryDragSource(e);

                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(entryType(e).c_str());

                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(formatSize(e).c_str());

                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    } else {
        for (const auto& e : m_entries) {
            ImGui::PushID(e.path.string().c_str());
            ImGui::BeginGroup();
            drawThumbnail(e, 16.0f);
            ImGui::SameLine();
            if (ImGui::Selectable(
                    e.name.c_str(), false,
                    ImGuiSelectableFlags_AllowDoubleClick)) {
                activateEntry(e);
            }
            drawEntryPopup(e);
            drawEntryDragSource(e);
            ImGui::EndGroup();
            ImGui::PopID();
        }
    }

    // Right-click empty space in the file list for creation operations.
    // ImGuiPopupFlags_NoOpenOverItems keeps this separate from the
    // per-entry context menu above.
    if (ImGui::BeginPopupContextWindow(
            "content_browser_context",
            ImGuiPopupFlags_MouseButtonRight |
                ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::MenuItem("New Folder")) {
            OpenDialog(Dialog::NewFolder, m_current,
                       UniqueName(m_current, "New Folder", "", true));
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("New Lua Script")) {
            const fs::path scriptsDirectory = fs::path(m_root) / "scripts";
            OpenDialog(Dialog::NewLua, scriptsDirectory,
                       UniqueName(scriptsDirectory, "new_script", ".lua",
                                  false));
            ImGui::CloseCurrentPopup();
        }
        ImGui::Separator();
        ImGui::TextDisabled("Create Script Templates");
        ImGui::Separator();
        for (const ScriptTemplate& scriptTemplate : kScriptTemplates) {
            if (ImGui::MenuItem(scriptTemplate.label)) {
                const std::string created = CreateScript(scriptTemplate.id);
                if (!created.empty()) clicked = created;
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s\nTarget: %s", scriptTemplate.description,
                                  (fs::path(m_root) / "scripts").string().c_str());
            }
        }
        ImGui::EndPopup();
    }

    ImGui::EndChild();

    const std::string moveDestination = m_movePicker.Draw();
    if (!moveDestination.empty() && !m_moveSource.empty()) {
        MoveEntry(m_moveSource, moveDestination);
        m_moveSource.clear();
    } else if (!m_movePicker.IsOpen()) {
        m_moveSource.clear();
    }

    DrawDialogs();
    return clicked;
}

} // namespace Galeshi

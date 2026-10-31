#include "GaleshiEngine/Editor/ContentBrowser.hpp"
#include "GaleshiEngine/Assets/AssetManager.hpp"
#include "GaleshiEngine/Assets/Texture.hpp"

#include <imgui.h>
#include <spdlog/spdlog.h>
#include <bgfx/bgfx.h>
#include <cctype>
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
        "player_controller",
        "Player Controller",
        "Physics movement and jumping for an entity named Player.",
        "player_controller.lua",
        R"LUA(-- Player Controller
-- Assign this file in Edit -> Scripts, or use it as the main script.
local playerName = "Player"
local moveSpeed = 8.0
local jumpImpulse = 1000.0
local player = nil

local function acquirePlayer()
    player = Galeshi.world.find(playerName)
end

function RunOnce()
    acquirePlayer()
end

function RunAlways(dt)
    if not player or not player:valid() then
        acquirePlayer()
        if not player then return end
    end

    local velocity = Galeshi.physics.get_velocity(player)
    local moveX = Galeshi.input.axis("Horizontal") * moveSpeed
    local moveZ = Galeshi.input.axis("Vertical") * moveSpeed
    Galeshi.physics.set_velocity(player, moveX, velocity.y, moveZ)

    if Galeshi.input.axis("Jump") > 0.5 and math.abs(velocity.y) < 0.5 then
        Galeshi.physics.add_impulse(player, 0.0, jumpImpulse, 0.0)
    end
end
)LUA"
    },
    {
        "ai_controller",
        "AI Controller",
        "Chase an entity named Player.",
        "ai_controller.lua",
        R"LUA(-- AI Controller
-- Attach this file to an entity with an AI Controller component.
local targetName = "Player"
local moveSpeed = 3.5
local chaseRadius = 20.0

function update_ai(eid, dt)
    local agent = Galeshi.world.entity(eid)
    if not agent or not agent:valid() then return end

    Galeshi.ai.set_speed(agent, moveSpeed)
    local target = Galeshi.world.find(targetName)
    if not target or not target:valid() then return end

    local position = target:position()
    local agentPosition = agent:position()
    local dx = position.x - agentPosition.x
    local dz = position.z - agentPosition.z
    if dx * dx + dz * dz <= chaseRadius * chaseRadius then
        Galeshi.ai.set_target(agent, position.x, position.y, position.z)
    end
end
)LUA"
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
    if (ImGui::Button("Import Asset")) {
        m_importPicker.Open(m_current.string(), "Import Asset",
                            { ".fbx", ".gltf", ".glb", ".obj", ".dae",
                              ".png", ".jpg", ".wav", ".mp3" });
    }
    ImGui::Separator();

    std::string importPath = m_importPicker.Draw();
    if (!importPath.empty()) {
        std::filesystem::path dest = m_current / std::filesystem::path(importPath).filename();
        std::error_code ec;
        std::filesystem::create_directories(dest.parent_path(), ec);
        if (ec) {
            spdlog::error("[ContentBrowser] Cannot create directory '{}': {}",
                          dest.parent_path().string(), ec.message());
        } else {
            std::filesystem::copy_file(importPath, dest,
                std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                spdlog::error("[ContentBrowser] Failed to import '{}' to '{}': {}",
                              importPath, dest.string(), ec.message());
            } else {
                Refresh();
            }
        }
    }

    ImGui::BeginChild("content_list", ImVec2(0, 0), true);
    for (const auto& e : m_entries) {
        ImGui::PushID(e.path.string().c_str());

        // Определяем тип файла
        std::string ext;
        if (!e.isDirectory) {
            auto dotPos = e.name.find_last_of('.');
            if (dotPos != std::string::npos) {
                ext = e.name.substr(dotPos);
                for (auto& c : ext) c = static_cast<char>(std::tolower(c));
            }
        }

        // Thumbnail для изображений
        bool isImage = (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
                        ext == ".bmp" || ext == ".tga" || ext == ".dds" ||
                        ext == ".ktx");

        if (isImage && m_assets) {
            std::string path = e.path.string();
            auto tex = m_assets->LoadTexture(path);
            if (tex && bgfx::isValid(tex->Handle())) {
                ImTextureID texId = static_cast<ImTextureID>(tex->Handle().idx);
                ImGui::Image(texId, ImVec2(32, 32));
                ImGui::SameLine();
            }
        }

        const char* icon = e.isDirectory ? "[D]" : "[F]";
        const std::string label = std::string(icon) + " " + e.name;

        if (ImGui::Selectable(label.c_str(), false,
                              ImGuiSelectableFlags_AllowDoubleClick)) {
            if (ImGui::IsMouseDoubleClicked(0)) {
                if (e.isDirectory) {
                    m_current = e.path;
                    Refresh();
                } else {
                    clicked = e.path.string();
                }
            }
        }

        // Источник для drag-n-drop (только файлы, не папки)
        if (!e.isDirectory &&
            ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            const std::string p = e.path.string();
            ImGui::SetDragDropPayload("GALESHI_ASSET_PATH",
                                      p.c_str(), p.size() + 1);
            ImGui::Text("%s", e.name.c_str());
            ImGui::EndDragDropSource();
        }

        ImGui::PopID();
    }

    // Right-click anywhere in the file list to create one of the built-in
    // script templates. New files are always placed in <assets>/scripts.
    if (ImGui::BeginPopupContextWindow("content_browser_context",
                                       ImGuiPopupFlags_MouseButtonRight)) {
        ImGui::TextDisabled("Create Script");
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

    return clicked;
}

} // namespace Galeshi

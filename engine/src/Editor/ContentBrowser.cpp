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
local jumpImpulse = 8.0
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
    -- A is -1 and D is +1 on the Horizontal axis. Invert the axis for
    -- screen-space movement so A moves left and D moves right.
    local moveX = -Galeshi.input.axis("Horizontal") * moveSpeed
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
    const fs::path target = m_current / filename;
    std::error_code ec;
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
                              ".png", ".jpg", ".wav", ".mp3" });
    }
    ImGui::Separator();

    std::string importPath = m_importPicker.Draw();
    if (!importPath.empty()) {
        ImportFiles({importPath}, m_current);
    }

    ImGui::BeginChild("content_list", ImVec2(0, 0), true);
    for (const auto& e : m_entries) {
        ImGui::PushID(e.path.string().c_str());

        std::string ext;
        if (!e.isDirectory) {
            auto dotPos = e.name.find_last_of('.');
            if (dotPos != std::string::npos) {
                ext = e.name.substr(dotPos);
                for (auto& c : ext) c = static_cast<char>(std::tolower(c));
            }
        }

        // Image thumbnail.
        bool isImage = (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
                        ext == ".bmp" || ext == ".tga" || ext == ".dds" ||
                        ext == ".ktx");

        if (isImage && m_assets) {
            std::string path = e.path.string();
            auto tex = m_assets->LoadTexture(path, true);
            if (tex && bgfx::isValid(tex->Handle())) {
                ImTextureID texId = static_cast<ImTextureID>(tex->Handle().idx);
                // Texture uploads use the bottom-left V origin; ImGui uses
                // top-left, so flip the thumbnail UVs to match Blender.
                ImGui::Image(texId, ImVec2(32, 32),
                             ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
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

        if (ImGui::BeginPopupContextItem("entry_context")) {
            ImGui::TextDisabled("%s", e.name.c_str());
            ImGui::Separator();
            if (e.isDirectory) {
                if (ImGui::MenuItem("Open")) {
                    m_current = e.path;
                    Refresh();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::Separator();
            }
            if (ImGui::MenuItem("Rename...")) {
                OpenDialog(Dialog::Rename, e.path, e.name);
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Move to...")) {
                m_moveSource = e.path;
                m_movePicker.PickFolder(m_current.string(),
                                        "Move to Folder");
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Delete")) {
                OpenDialog(Dialog::Delete, e.path);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // Drag source: files only, not folders.
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
            OpenDialog(Dialog::NewLua, m_current,
                       UniqueName(m_current, "new_script", ".lua", false));
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

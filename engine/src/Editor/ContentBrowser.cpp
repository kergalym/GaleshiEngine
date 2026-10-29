#include "GaleshiEngine/Editor/ContentBrowser.hpp"
#include "GaleshiEngine/Assets/AssetManager.hpp"
#include "GaleshiEngine/Assets/Texture.hpp"

#include <imgui.h>
#include <spdlog/spdlog.h>
#include <bgfx/bgfx.h>
#include <cctype>

#include <algorithm>

namespace fs = std::filesystem;

namespace Galeshi {

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
        m_importPicker.Open(m_current.string(), "Import Asset", { ".fbx", ".gltf", ".glb", ".png", ".jpg", ".wav", ".mp3" });
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
    ImGui::EndChild();

    return clicked;
}

} // namespace Galeshi

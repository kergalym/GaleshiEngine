#include "GaleshiEngine/Editor/ContentBrowser.hpp"

#include <imgui.h>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

namespace Galeshi {

ContentBrowser::ContentBrowser(std::string rootDir)
    : m_root(std::move(rootDir)), m_current(m_root) {}

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
}

std::string ContentBrowser::Draw() {
    std::string clicked;

    ImGui::Text("Path: %s", m_current.string().c_str());
    ImGui::Separator();

    // Кнопка "вверх"
    if (m_current != fs::path(m_root)) {
        if (ImGui::Button("..")) {
            m_current = m_current.parent_path();
            Refresh();
        }
        ImGui::SameLine();
    }
    if (ImGui::Button("Refresh")) Refresh();
    ImGui::Separator();

    if (m_entries.empty()) Refresh();

    ImGui::BeginChild("content_list", ImVec2(0, 0), true);
    for (const auto& e : m_entries) {
        const char* icon = e.isDirectory ? "[D]" : "[F]";
        ImGui::Text("%s %s", icon, e.name.c_str());

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            if (e.isDirectory) {
                m_current = e.path;
                Refresh();
            } else {
                clicked = e.path.string();
            }
        }
    }
    ImGui::EndChild();

    return clicked;
}

} // namespace Galeshi

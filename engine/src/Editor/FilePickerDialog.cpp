#include "GaleshiEngine/Editor/FilePickerDialog.hpp"

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cstring>

namespace fs = std::filesystem;

namespace Galeshi {

static std::string ToLower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

void FilePickerDialog::Open(const std::string& startDir,
                            const std::string& title,
                            std::vector<std::string> extensions)
{
    m_open        = true;
    m_saveMode    = false;
    m_title       = title;
    m_root        = startDir;
    m_current     = startDir.empty() ? fs::current_path() : fs::path(startDir);
    m_allowedExts = std::move(extensions);
    for (auto& e : m_allowedExts) e = ToLower(e);
    m_selectedFile.clear();
    m_saveNameBuf[0] = '\0';
    m_dirty = true;
}

void FilePickerDialog::SaveAs(const std::string& startDir,
                              const std::string& defaultName,
                              const std::string& title,
                              std::vector<std::string> extensions)
{
    m_open        = true;
    m_saveMode    = true;
    m_title       = title;
    m_root        = startDir;
    m_current     = startDir.empty() ? fs::current_path() : fs::path(startDir);
    m_allowedExts = std::move(extensions);
    for (auto& e : m_allowedExts) e = ToLower(e);
    m_selectedFile.clear();

    std::memset(m_saveNameBuf, 0, sizeof(m_saveNameBuf));
    std::strncpy(m_saveNameBuf, defaultName.c_str(), sizeof(m_saveNameBuf) - 1);

    m_dirty = true;
}

void FilePickerDialog::Refresh() {
    m_entries.clear();
    std::error_code ec;
    if (!fs::exists(m_current, ec)) return;

    for (auto& p : fs::directory_iterator(m_current, ec)) {
        Entry e;
        e.path        = p.path();
        e.isDirectory = p.is_directory();
        e.name        = p.path().filename().string();

        if (!e.isDirectory && !m_saveMode) {
            std::string ext = ToLower(e.path.extension().string());
            bool ok = false;
            for (const auto& a : m_allowedExts)
                if (ext == a) { ok = true; break; }
            if (!m_allowedExts.empty() && !ok) continue;
        }
        m_entries.push_back(std::move(e));
    }
    std::sort(m_entries.begin(), m_entries.end(),
        [](const Entry& a, const Entry& b) {
            if (a.isDirectory != b.isDirectory) return a.isDirectory > b.isDirectory;
            return a.name < b.name;
        });
    m_dirty = false;
}

std::string FilePickerDialog::Draw() {
    if (!m_open) return {};

    if (m_dirty) Refresh();

    const char* titleC = m_title.c_str();
    ImGui::OpenPopup(titleC);

    std::string result;

    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal(titleC, nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        // Путь
        ImGui::Text("Path:");
        ImGui::SameLine();
        ImGui::TextUnformatted(m_current.string().c_str());
        ImGui::Separator();

        if (ImGui::Button("..")) {
            const auto parent = m_current.parent_path();
            if (parent != m_current) {
                m_current = parent;
                m_dirty = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Refresh")) m_dirty = true;

        if (!m_saveMode) {
            ImGui::SameLine();
            ImGui::Text("| Selected: %s",
                        m_selectedFile.empty() ? "(none)"
                                               : fs::path(m_selectedFile).filename().string().c_str());
        }

        ImGui::Separator();

        // Список
        const float listHeight = m_saveMode ? -80.0f : -40.0f;
        ImGui::BeginChild("##files", ImVec2(0, listHeight), true);
        for (const auto& e : m_entries) {
            const bool isSelected = (!e.isDirectory && e.path.string() == m_selectedFile);

            ImGui::PushID(e.path.string().c_str());
            const char* icon = e.isDirectory ? "[D]" : "[F]";
            const std::string label = std::string(icon) + " " + e.name;

            if (ImGui::Selectable(label.c_str(), isSelected,
                                  ImGuiSelectableFlags_AllowDoubleClick))
            {
                if (e.isDirectory) {
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        m_current = e.path;
                        m_dirty = true;
                        m_selectedFile.clear();
                    }
                } else {
                    m_selectedFile = e.path.string();
                    if (ImGui::IsMouseDoubleClicked(0) && !m_saveMode) {
                        result = m_selectedFile;
                        m_open = false;
                        ImGui::CloseCurrentPopup();
                    }
                }
            }
            ImGui::PopID();
        }
        ImGui::EndChild();

        // Поле ввода имени для Save
        if (m_saveMode) {
            ImGui::Text("File name:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-120);
            ImGui::InputText("##savename", m_saveNameBuf, sizeof(m_saveNameBuf));
        }

        // Кнопки
        if (m_saveMode) {
            const bool hasName = m_saveNameBuf[0] != '\0';
            ImGui::BeginDisabled(!hasName);
            if (ImGui::Button("Save", ImVec2(120, 0))) {
                fs::path full = m_current / m_saveNameBuf;

                // Добавляем расширение если его нет
                if (!m_allowedExts.empty()) {
                    std::string ext = ToLower(full.extension().string());
                    bool hasValidExt = false;
                    for (const auto& a : m_allowedExts)
                        if (ext == a) { hasValidExt = true; break; }
                    if (!hasValidExt) full += m_allowedExts[0];
                }

                result = full.string();
                m_open = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                m_open = false;
                ImGui::CloseCurrentPopup();
            }
        } else {
            const bool hasSelection = !m_selectedFile.empty();
            ImGui::BeginDisabled(!hasSelection);
            if (ImGui::Button("Open", ImVec2(120, 0))) {
                result = m_selectedFile;
                m_open = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                m_open = false;
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::EndPopup();
    }

    return result;
}

} // namespace Galeshi

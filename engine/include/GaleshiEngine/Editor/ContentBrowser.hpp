#pragma once

#include <chrono>
#include <filesystem>
#include "GaleshiEngine/Editor/FilePickerDialog.hpp"
#include <string>
#include <vector>

namespace Galeshi {

class AssetManager;

class ContentBrowser {
public:
    explicit ContentBrowser(std::string rootDir, AssetManager* assets = nullptr);

    // Рисует панель. Возвращает путь к файлу, если пользователь сделал
    // двойной клик по нему или создал новый скрипт через контекстное меню
    // ("" — ничего не выбрано).
    std::string Draw();

    // Creates a Lua script from one of the built-in templates inside
    // <content root>/scripts. Returns the created file path, or an empty
    // string on failure.
    std::string CreateScript(const std::string& templateId);

    void Refresh();

    // Folder currently shown in the browser. File-manager drops that land on
    // the Content Browser are imported here.
    const std::filesystem::path& CurrentDirectory() const { return m_current; }

    // Copies external files into destDir and refreshes the browser. Returns
    // true when at least one file was imported.
    bool ImportFiles(const std::vector<std::string>& files,
                     const std::filesystem::path& destDir);

    // Drop coordinates are window-relative (single-viewport ImGui space).
    bool ContainsPoint(float x, float y) const;

private:
    struct Entry {
        std::filesystem::path path;
        bool                  isDirectory;
        std::string           name;
    };

    void DrawDirectory(const std::filesystem::path& dir);

    // Polls the current directory for changes (files added/removed/renamed)
    // and refreshes the listing when needed.
    void UpdateAutoRefresh();

    std::string           m_root;
    AssetManager*         m_assets = nullptr;
    std::filesystem::path m_current;
    std::vector<Entry>    m_entries;
    FilePickerDialog m_importPicker;

    bool                                  m_autoRefresh = true;
    std::chrono::steady_clock::time_point  m_lastCheck{};
    std::filesystem::file_time_type        m_lastWriteTime{};
    float                                  m_windowMinX = 0.0f;
    float                                  m_windowMinY = 0.0f;
    float                                  m_windowMaxX = 0.0f;
    float                                  m_windowMaxY = 0.0f;
};

} // namespace Galeshi

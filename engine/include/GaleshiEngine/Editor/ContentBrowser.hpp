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
    // двойной клик по нему ("" — ничего не выбрано).
    std::string Draw();

    void Refresh();

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
};

} // namespace Galeshi

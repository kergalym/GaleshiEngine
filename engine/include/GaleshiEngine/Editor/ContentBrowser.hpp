#pragma once

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

    std::string           m_root;
    AssetManager*         m_assets = nullptr;
    std::filesystem::path m_current;
    std::vector<Entry>    m_entries;
    FilePickerDialog m_importPicker;
};

} // namespace Galeshi

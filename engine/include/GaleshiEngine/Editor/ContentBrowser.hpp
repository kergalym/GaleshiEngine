#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Galeshi {

class ContentBrowser {
public:
    explicit ContentBrowser(std::string rootDir);

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
    std::filesystem::path m_current;
    std::vector<Entry>    m_entries;
};

} // namespace Galeshi

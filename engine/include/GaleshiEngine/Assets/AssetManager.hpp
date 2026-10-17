#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Galeshi {

class Texture;
class Mesh;
class Material;

// Простой path-based кэш ассетов.
class AssetManager {
public:
    AssetManager();
    ~AssetManager();

    // --- Текстуры ---
    std::shared_ptr<Texture> LoadTexture(const std::string& path);

    // --- Модели (FBX/glTF/OBJ) ---
    // Возвращает список мешей и материалов (по одному на submesh).
    struct LoadedModel {
        std::vector<std::shared_ptr<Mesh>>     meshes;
        std::vector<std::shared_ptr<Material>> materials;
    };
    LoadedModel LoadModel(const std::string& path);

    // --- Встроенные дефолтные текстуры ---
    std::shared_ptr<Texture> WhiteTexture()       const { return m_white; }
    std::shared_ptr<Texture> BlackTexture()       const { return m_black; }
    std::shared_ptr<Texture> FlatNormalTexture()  const { return m_flatNormal; }

    void Clear();

private:
    std::unordered_map<std::string, std::shared_ptr<Texture>> m_textures;
    std::unordered_map<std::string, LoadedModel>              m_models;

    std::shared_ptr<Texture> m_white;
    std::shared_ptr<Texture> m_black;
    std::shared_ptr<Texture> m_flatNormal;
};

} // namespace Galeshi

#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Galeshi {

class Texture;
class Mesh;
class Material;

class AssetManager {
public:
    AssetManager();
    ~AssetManager();

    std::shared_ptr<Texture> LoadTexture(const std::string& path);

    struct LoadedModel {
        std::vector<std::shared_ptr<Mesh>>     meshes;
        std::vector<std::shared_ptr<Material>> materials;
        std::vector<int>                       meshMaterialIndex;
    };
    LoadedModel LoadModel(const std::string& path);

    std::shared_ptr<Texture> WhiteTexture()       const { return m_white; }
    std::shared_ptr<Texture> BlackTexture()       const { return m_black; }
    std::shared_ptr<Texture> FlatNormalTexture()  const { return m_flatNormal; }

    std::shared_ptr<Mesh> CreateCubeMesh();

    void Clear();

private:
    std::unordered_map<std::string, std::shared_ptr<Texture>> m_textures;
    std::unordered_map<std::string, LoadedModel>              m_models;

    std::shared_ptr<Mesh>    m_cubeMesh;
    std::shared_ptr<Texture> m_white;
    std::shared_ptr<Texture> m_black;
    std::shared_ptr<Texture> m_flatNormal;
};

} // namespace Galeshi

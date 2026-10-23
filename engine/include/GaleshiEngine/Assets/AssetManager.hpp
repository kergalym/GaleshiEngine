#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>

struct aiScene;
namespace Assimp { class Importer; }

namespace Galeshi {

class Texture;
class Mesh;
class Material;

class AssetManager {
public:
    AssetManager();
    ~AssetManager();

    // srgb = true for colour maps (base colour/emissive), false for data maps.
    std::shared_ptr<Texture> LoadTexture(const std::string& path, bool srgb = false);

    struct LoadedModel {
        std::vector<std::shared_ptr<Mesh>>     meshes;
        std::vector<std::shared_ptr<Material>> materials;
        std::vector<int>                       meshMaterialIndex;
        
        std::shared_ptr<Assimp::Importer>      importer; // Храним importer
        std::shared_ptr<aiScene>               rawScene;
        
        std::unordered_map<std::string, int>   boneMap;
        int                                    totalBones = 0;
    bool hasAnimation = false;
        float scale = 1.0f;
        glm::mat4                              rootTransform{1.0f}; // Трансформация Armature
    };
    
    LoadedModel LoadModel(const std::string& path);

    void UpdateAnimation(const LoadedModel& model, float time, std::vector<glm::mat4>& outBoneMatrices);

    std::shared_ptr<Texture> WhiteTexture()       const { return m_white; }
    std::shared_ptr<Texture> BlackTexture()       const { return m_black; }
    std::shared_ptr<Texture> FlatNormalTexture()  const { return m_flatNormal; }

    std::shared_ptr<Mesh> CreateCubeMesh();
    std::shared_ptr<Mesh> CreateSphereMesh();  // unit sphere, radius 0.5
    std::shared_ptr<Mesh> CreateCapsuleMesh(float halfHeight, float radius);
    void Clear();

private:
    std::unordered_map<std::string, std::shared_ptr<Texture>> m_textures;
    // Embedded FBX/glTF textures indexed by normalized lowercase path and basename.
    std::unordered_map<std::string, std::shared_ptr<Texture>> m_embeddedTextures;
    std::unordered_map<std::string, LoadedModel>              m_models;

    std::shared_ptr<Mesh>    m_cubeMesh;
    std::shared_ptr<Mesh> m_sphereMesh;
    std::shared_ptr<Texture> m_white;
    std::shared_ptr<Texture> m_black;
    std::shared_ptr<Texture> m_flatNormal;
};

} // namespace Galeshi

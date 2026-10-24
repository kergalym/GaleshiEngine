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
        std::vector<std::string>                animationNames;
        std::vector<float>                      animationDurations; // seconds
        float scale = 1.0f;
        glm::mat4                              rootTransform{1.0f}; // Трансформация Armature
    };
    
    LoadedModel LoadModel(const std::string& path);

    // Legacy single-clip API (clip 0).
    void UpdateAnimation(const LoadedModel& model, float time, std::vector<glm::mat4>& outBoneMatrices);

    // Samples an explicit animation clip.
    void UpdateAnimationClip(const LoadedModel& model, int clip, float time,
                             std::vector<glm::mat4>& outBoneMatrices);

    // Samples two clips and blends their bone transforms.  blend is clamped
    // to [0, 1], where 0 is fully clipA and 1 is fully clipB.
    void UpdateAnimationBlended(const LoadedModel& model,
                                int clipA, float timeA,
                                int clipB, float timeB,
                                float blend,
                                std::vector<glm::mat4>& outBoneMatrices);

    std::shared_ptr<Texture> WhiteTexture()       const { return m_white; }
    std::shared_ptr<Texture> BlackTexture()       const { return m_black; }
    std::shared_ptr<Texture> FlatNormalTexture()  const { return m_flatNormal; }

    std::shared_ptr<Mesh> CreateCubeMesh();
    std::shared_ptr<Mesh> CreateSphereMesh();  // unit sphere, radius 0.5
    std::shared_ptr<Mesh> CreateCapsuleMesh(float halfHeight, float radius);

    // Procedural low-poly vegetation primitives used by the instanced
    // renderer.  Trunks and canopies are drawn with separate materials so the
    // tree has a brown trunk and green foliage without vertex colours.
    std::shared_ptr<Mesh> CreateTreeTrunkMesh();
    std::shared_ptr<Mesh> CreateTreeCanopyMesh();
    std::shared_ptr<Mesh> CreateGrassMesh();

    // Release cached meshes and textures. Clear the scene first so its
    // components no longer hold shared references to these resources.
    void Clear();

    std::string ResolvePath(const std::string& path) const;
    std::string MakeRelativePath(const std::string& path) const;

private:
    std::unordered_map<std::string, std::shared_ptr<Texture>> m_textures;
    // Embedded FBX/glTF textures indexed by normalized lowercase path and basename.
    std::unordered_map<std::string, std::shared_ptr<Texture>> m_embeddedTextures;
    std::unordered_map<std::string, LoadedModel>              m_models;

    std::shared_ptr<Mesh>    m_cubeMesh;
    std::shared_ptr<Mesh>    m_sphereMesh;
    std::shared_ptr<Mesh>    m_treeTrunkMesh;
    std::shared_ptr<Mesh>    m_treeCanopyMesh;
    std::shared_ptr<Mesh>    m_grassMesh;
    std::shared_ptr<Texture> m_white;
    std::shared_ptr<Texture> m_black;
    std::shared_ptr<Texture> m_flatNormal;
};

} // namespace Galeshi

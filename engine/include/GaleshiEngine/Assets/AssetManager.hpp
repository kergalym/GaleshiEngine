#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <glm/glm.hpp>

#include "GaleshiEngine/Assets/Texture.hpp"

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
    // If the path belongs to a Substance Painter/UDIM tile set, all tiles are
    // packed into one atlas and udim receives the UV remap information.
    std::shared_ptr<Texture> LoadTexture(
        const std::string& path, bool srgb = false,
        Texture::UdimInfo* udim = nullptr, bool flipVertically = true);

    struct LoadedModel {
        std::vector<std::shared_ptr<Mesh>>     meshes;
        std::vector<std::shared_ptr<Material>> materials;
        std::vector<int>                       meshMaterialIndex;
        // Original exporter node/mesh names, used as the display fallback for
        // legitimately unnamed materials.
        std::vector<std::string>               meshNames;
        
        std::shared_ptr<Assimp::Importer>      importer;
        std::shared_ptr<aiScene>               rawScene;
        
        std::unordered_map<std::string, int>   boneMap;
        // Inverse bind matrices by bone name. FBX uses the node rest pose
        // because mesh-node transforms are baked into vertex positions.
        // glTF keeps its explicit inverseBindMatrices.
        std::unordered_map<std::string, glm::mat4> boneOffsetMatrices;
        bool                                   isGltf = false;
        // Rest-pose world transform of every node. Animation-only FBX files
        // can use a different axis/unit convention from the character; this
        // map lets retargeting remove that difference.
        std::unordered_map<std::string, glm::mat4> nodeRestGlobals;
        // Root-most skinned bone and its namespace prefix, for example
        // "Korlan:Hips" -> "Korlan:". Used to remap animation namespaces
        // (mixamorig:, Armature|, etc.) onto the selected character.
        std::string                            rootBoneName;
        std::string                            boneNamespacePrefix;
        // Top-level armature node, e.g. "Korlan" or "Armature", used in
        // retarget diagnostics.
        std::string                            armatureRootName;
        float                                  unitScaleFactor = 1.0f;
        // Global conversion from FBX units to engine meters. Blender writes
        // UnitScaleFactor=1 for a centimeter FBX, so this is normally 0.01.
        float                                  importScale = 0.01f;
        int                                    totalBones = 0;
        bool hasAnimation = false;
        std::vector<std::string>                animationNames;
        std::vector<float>                      animationDurations; // seconds
        float scale = 1.0f;
        glm::mat4                              rootTransform{1.0f};
    };
    
    LoadedModel LoadModel(const std::string& path);
    // Import through Assimp once and pin the result for later scene loads.
    // Scene loads never populate this cache; they only consume it.
    LoadedModel ImportModel(const std::string& path);
    // Persist/restore the imported-model cache. The editor calls these on
    // scene save and application shutdown/startup.
    void SaveImportCache() const;
    bool LoadImportCache();

    // Legacy single-clip API (clip 0).
    void UpdateAnimation(const LoadedModel& model, float time, std::vector<glm::mat4>& outBoneMatrices);

    // Samples an explicit animation clip.
    void UpdateAnimationClip(const LoadedModel& model, int clip, float time,
                             std::vector<glm::mat4>& outBoneMatrices);

    // Samples a clip from a separate animation FBX and maps its animated
    // node poses onto the target model's bones by name.
    void UpdateAnimationClipRetargeted(const LoadedModel& animationModel,
                                       int clip, float time,
                                       const LoadedModel& targetModel,
                                       std::vector<glm::mat4>& outBoneMatrices,
                                       float animationScale = 1.0f);

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
    std::shared_ptr<Mesh> CreateWaterMesh();   // unit XZ plane, 64x64 grid

    // Procedural low-poly vegetation primitives used by the instanced
    // renderer.  Trunks and canopies are drawn with separate materials so the
    // tree has a brown trunk and green foliage without vertex colours.
    std::shared_ptr<Mesh> CreateTreeTrunkMesh();
    std::shared_ptr<Mesh> CreateTreeCanopyMesh();
    std::shared_ptr<Mesh> CreateGrassMesh();

    // Release cached meshes and textures. Clear the scene first so its
    // components no longer hold shared references to these resources.
    // keepImportedModels preserves models pinned by ImportModel().
    void Clear(bool keepImportedModels = false);

    // Drop cached models and textures that are no longer referenced by the
    // scene. retainedModels lists model paths that are still needed by
    // non-owning systems such as SkeletalComponent animation playback.
    void ReleaseUnusedAssets(const std::vector<std::string>& retainedModels = {});

    std::string ResolvePath(const std::string& path) const;
    std::string MakeRelativePath(const std::string& path) const;

private:
    // Encoded (or BGRA8) source retained so an embedded texture can be
    // re-uploaded when its sRGB checkbox changes.
    struct EmbeddedTextureSource {
        std::string displayPath;
        std::vector<unsigned char> data;
        int width = 0;
        int height = 0;
    };

    std::unordered_map<std::string, std::shared_ptr<Texture>> m_textures;
    // Cached UV layouts for packed UDIM atlases, keyed like m_textures.
    std::unordered_map<std::string, Texture::UdimInfo>        m_udimInfo;
    // Embedded FBX/glTF textures indexed by normalized lowercase path and basename.
    std::unordered_map<std::string, std::shared_ptr<Texture>> m_embeddedTextures;
    std::unordered_map<std::string, std::shared_ptr<EmbeddedTextureSource>>
                                                              m_embeddedSources;
    std::unordered_map<std::string, LoadedModel>              m_models;
    // Models explicitly imported through the editor. Kept across scene
    // unloads so loading a scene does not re-run Assimp.
    std::unordered_map<std::string, LoadedModel>              m_importedModels;
    // Source file size and write time captured when the model was imported.
    std::unordered_map<std::string, std::pair<uint64_t, uint64_t>>
                                                              m_importedSignatures;

    std::shared_ptr<Mesh>    m_cubeMesh;
    std::shared_ptr<Mesh>    m_sphereMesh;
    std::shared_ptr<Mesh>    m_waterMesh;
    std::shared_ptr<Mesh>    m_treeTrunkMesh;
    std::shared_ptr<Mesh>    m_treeCanopyMesh;
    std::shared_ptr<Mesh>    m_grassMesh;
    std::shared_ptr<Texture> m_white;
    std::shared_ptr<Texture> m_black;
    std::shared_ptr<Texture> m_flatNormal;
};

} // namespace Galeshi

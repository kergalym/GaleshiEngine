#include "GaleshiEngine/Assets/AssetManager.hpp"
#include "GaleshiEngine/Assets/Texture.hpp"
#include "GaleshiEngine/Assets/Mesh.hpp"
#include "GaleshiEngine/Assets/Material.hpp"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <spdlog/spdlog.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <functional>
#include <vector>

namespace Galeshi {

const aiScene* g_currentAiScene = nullptr;

// Глобалка для передачи aiScene в MakeMaterialFromAssimp
// (для embedded FBX текстур — scene->mTextures)

AssetManager::AssetManager() {
    m_white      = std::shared_ptr<Texture>(Texture::CreateWhite());
    m_black      = std::shared_ptr<Texture>(Texture::CreateBlack());
    m_flatNormal = std::shared_ptr<Texture>(Texture::CreateFlatNormal());
}

AssetManager::~AssetManager() = default;
void AssetManager::Clear() {
    m_models.clear();
    m_textures.clear();
    m_embeddedTextures.clear();
}

namespace {
std::string NormalizeAssetPath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    while (path.rfind("./", 0) == 0) path.erase(0, 2);
    return path;
}

std::string LowerAssetPath(std::string path) {
    path = NormalizeAssetPath(std::move(path));
    std::transform(path.begin(), path.end(), path.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return path;
}

std::string AssetBaseName(const std::string& path) {
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}
} // namespace

std::shared_ptr<Texture> AssetManager::LoadTexture(const std::string& path) {
    if (path.empty()) return m_white;

    auto it = m_textures.find(path);
    if (it != m_textures.end()) return it->second;

    const std::string normalized = NormalizeAssetPath(path);
    it = m_textures.find(normalized);
    if (it != m_textures.end()) return it->second;

    const std::string lower = LowerAssetPath(normalized);
    auto embedded = m_embeddedTextures.find(lower);
    if (embedded != m_embeddedTextures.end()) return embedded->second;
    embedded = m_embeddedTextures.find(LowerAssetPath(AssetBaseName(normalized)));
    if (embedded != m_embeddedTextures.end()) return embedded->second;

    std::error_code ec;
    bool exists = std::filesystem::exists(path, ec);
    if (!exists && normalized != path) {
        ec.clear();
        exists = std::filesystem::exists(normalized, ec);
    }
    if (!exists) return m_white;

    Texture* raw = Texture::LoadFromFile(path);
    if (!raw && normalized != path) raw = Texture::LoadFromFile(normalized);
    if (!raw) return m_white;

    std::shared_ptr<Texture> tex(raw);
    m_textures[path] = tex;
    m_textures[normalized] = tex;
    return tex;
}

namespace {
glm::vec3 aiToVec3(const aiVector3D& v) { return {v.x, v.y, v.z}; }
glm::vec2 aiToVec2(const aiVector3D& v) { return {v.x, v.y}; }

glm::mat4 aiToGlm(const aiMatrix4x4& from) {
    glm::mat4 to;
    to[0][0] = from.a1; to[1][0] = from.a2; to[2][0] = from.a3; to[3][0] = from.a4;
    to[0][1] = from.b1; to[1][1] = from.b2; to[2][1] = from.b3; to[3][1] = from.b4;
    to[0][2] = from.c1; to[1][2] = from.c2; to[2][2] = from.c3; to[3][2] = from.c4;
    to[0][3] = from.d1; to[1][3] = from.d2; to[2][3] = from.d3; to[3][3] = from.d4;
    return to;
}

void ReadNodeHeirarchy(const aiScene* scene, float time, const aiNode* node, const glm::mat4& parentTransform, std::vector<glm::mat4>& boneMatrices, const std::unordered_map<std::string, int>& boneMap, int depth = 0) {
    if (depth > 128) { return; }
    std::string nodeName(node->mName.data);
    glm::mat4 nodeTransform = aiToGlm(node->mTransformation);

    if (scene->mNumAnimations > 0) {
        const aiAnimation* anim = scene->mAnimations[0];
        float ticksPerSecond = anim->mTicksPerSecond != 0 ? anim->mTicksPerSecond : 25.0f;
        float timeInTicks = fmod(time * ticksPerSecond, anim->mDuration);

        const aiNodeAnim* nodeAnim = nullptr;
        for (unsigned int i = 0; i < anim->mNumChannels; ++i) {
            if (std::string(anim->mChannels[i]->mNodeName.data) == nodeName) {
                nodeAnim = anim->mChannels[i];
                break;
            }
        }

        if (nodeAnim) {
            glm::vec3 pos = aiToVec3(nodeAnim->mPositionKeys[0].mValue);
            if (nodeAnim->mNumPositionKeys > 1) {
                for (unsigned int i = 0; i < nodeAnim->mNumPositionKeys - 1; ++i) {
                    if (timeInTicks < (float)nodeAnim->mPositionKeys[i + 1].mTime) {
                        float dt = (float)nodeAnim->mPositionKeys[i + 1].mTime - (float)nodeAnim->mPositionKeys[i].mTime;
                        float factor = (timeInTicks - (float)nodeAnim->mPositionKeys[i].mTime) / dt;
                        pos = glm::mix(aiToVec3(nodeAnim->mPositionKeys[i].mValue), aiToVec3(nodeAnim->mPositionKeys[i + 1].mValue), factor);
                        break;
                    }
                }
            }
            glm::quat rot(nodeAnim->mRotationKeys[0].mValue.w, nodeAnim->mRotationKeys[0].mValue.x, nodeAnim->mRotationKeys[0].mValue.y, nodeAnim->mRotationKeys[0].mValue.z);
            if (nodeAnim->mNumRotationKeys > 1) {
                for (unsigned int i = 0; i < nodeAnim->mNumRotationKeys - 1; ++i) {
                    if (timeInTicks < (float)nodeAnim->mRotationKeys[i + 1].mTime) {
                        float dt = (float)nodeAnim->mRotationKeys[i + 1].mTime - (float)nodeAnim->mRotationKeys[i].mTime;
                        float factor = (timeInTicks - (float)nodeAnim->mRotationKeys[i].mTime) / dt;
                        glm::quat start(nodeAnim->mRotationKeys[i].mValue.w, nodeAnim->mRotationKeys[i].mValue.x, nodeAnim->mRotationKeys[i].mValue.y, nodeAnim->mRotationKeys[i].mValue.z);
                        glm::quat end(nodeAnim->mRotationKeys[i + 1].mValue.w, nodeAnim->mRotationKeys[i + 1].mValue.x, nodeAnim->mRotationKeys[i + 1].mValue.y, nodeAnim->mRotationKeys[i + 1].mValue.z);
                        rot = glm::slerp(start, end, factor);
                        break;
                    }
                }
            }
            glm::vec3 scale = aiToVec3(nodeAnim->mScalingKeys[0].mValue);
            nodeTransform = glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(rot) * glm::scale(glm::mat4(1.0f), scale);
        }
    }

    glm::mat4 globalTransform = parentTransform * nodeTransform;

    if (boneMap.find(nodeName) != boneMap.end()) {
        int boneIndex = boneMap.at(nodeName);
        if (boneIndex < 254) { // 255 зарезервирован под Identity
            aiBone* bone = nullptr;
            for (unsigned m = 0; m < scene->mNumMeshes && !bone; ++m) {
                for (unsigned i = 0; i < scene->mMeshes[m]->mNumBones; ++i) {
                    if (std::string(scene->mMeshes[m]->mBones[i]->mName.data) == nodeName) {
                        bone = scene->mMeshes[m]->mBones[i];
                        break;
                    }
                }
            }
            if (bone) {
                boneMatrices[boneIndex] = globalTransform * aiToGlm(bone->mOffsetMatrix);
            }
        }
    }

    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        ReadNodeHeirarchy(scene, time, node->mChildren[i], globalTransform, boneMatrices, boneMap, depth + 1);
    }
}
} // namespace

AssetManager::LoadedModel AssetManager::LoadModel(const std::string& path) {
    if (path.empty()) return LoadedModel{};
    auto it = m_models.find(path);
    if (it != m_models.end()) return it->second;

    spdlog::info("[AssetManager] === Loading: {} ===", path);

    LoadedModel out;
    auto importer = std::make_shared<Assimp::Importer>();

    // Texture loading flips images vertically, matching the bottom-left V
    // origin used by FBX/OBJ/Collada. glTF is the exception: its UV origin is
    // top-left, so it still needs Assimp's flip to use the same convention.
    std::string extension = LowerAssetPath(
        std::filesystem::path(path).extension().string());
    unsigned int postProcess =
        aiProcess_Triangulate | aiProcess_GenSmoothNormals |
        aiProcess_CalcTangentSpace;
    if (extension == ".gltf" || extension == ".glb") {
        postProcess |= aiProcess_FlipUVs;
    }

    const aiScene* scene = importer->ReadFile(path, postProcess);

    if (!scene || !scene->mRootNode) {
        spdlog::error("[AssetManager] Assimp failed: {}", importer->GetErrorString());
        return out;
    }

    g_currentAiScene = scene;
    out.importer = importer;
    out.rawScene = std::shared_ptr<aiScene>(const_cast<aiScene*>(scene), [](aiScene*){});

    // === ДИАГНОСТИКА ===
    spdlog::info("[AssetManager] Scene stats:");
    spdlog::info("  meshes:           {}", scene->mNumMeshes);
    spdlog::info("  materials:        {}", scene->mNumMaterials);
    spdlog::info("  embedded textures:{}", scene->mNumTextures);
    spdlog::info("  animations:       {}", scene->mNumAnimations);

    for (unsigned i = 0; i < scene->mNumTextures; ++i) {
        const aiTexture* t = scene->mTextures[i];
        std::string fname = t->mFilename.C_Str();
        spdlog::info("  embedded[{}]: '{}', {}x{}, format '{}'",
                     i, fname, t->mWidth, t->mHeight, t->achFormatHint);
    }

    const std::string dir = std::filesystem::path(path).parent_path().string();

    // Build an embedded texture lookup. FBX often stores the original file
    // names instead of the "*0" form, for example
    // "..\\..\\FbxWorkingDirectory\\Foo_Diffuse.png". Both forms must work.
    std::unordered_map<std::string, unsigned> embeddedByName;
    for (unsigned ti = 0; ti < scene->mNumTextures; ++ti) {
        const std::string filename = NormalizeAssetPath(scene->mTextures[ti]->mFilename.C_Str());
        if (filename.empty()) continue;
        const std::string key = LowerAssetPath(filename);
        embeddedByName[key] = ti;
        const std::string basename = LowerAssetPath(AssetBaseName(filename));
        if (!basename.empty()) embeddedByName[basename] = ti;
    }

    auto findEmbeddedTexture = [&](const std::string& reference, int& outIndex) -> bool {
        if (reference.empty()) return false;
        if (reference[0] == '*') {
            try {
                const int idx = std::stoi(reference.substr(1));
                if (idx >= 0 && idx < static_cast<int>(scene->mNumTextures)) {
                    outIndex = idx;
                    return true;
                }
            } catch (...) {
                return false;
            }
            return false;
        }
        const std::string key = LowerAssetPath(reference);
        auto found = embeddedByName.find(key);
        if (found == embeddedByName.end())
            found = embeddedByName.find(LowerAssetPath(AssetBaseName(key)));
        if (found == embeddedByName.end()) return false;
        outIndex = static_cast<int>(found->second);
        return true;
    };

    auto loadEmbeddedTexture = [&](int index, const std::string& reference) -> std::shared_ptr<Texture> {
        if (index < 0 || index >= static_cast<int>(scene->mNumTextures)) return nullptr;
        const aiTexture* embedded = scene->mTextures[index];
        const std::string embeddedName = NormalizeAssetPath(embedded->mFilename.C_Str());
        std::string displayPath = NormalizeAssetPath(reference);
        if (displayPath.empty() || displayPath[0] == '*') displayPath = embeddedName;

        const std::string cacheKey = "embedded:" + std::to_string(index) + ":" + embeddedName;
        auto cached = m_textures.find(cacheKey);
        if (cached != m_textures.end()) return cached->second;

        Texture* raw = nullptr;
        if (embedded->mHeight == 0) {
            raw = Texture::LoadFromMemory(
                reinterpret_cast<const unsigned char*>(embedded->pcData),
                static_cast<size_t>(embedded->mWidth), displayPath);
        } else {
            raw = Texture::CreateFromBgra8(
                reinterpret_cast<const unsigned char*>(embedded->pcData),
                static_cast<int>(embedded->mWidth),
                static_cast<int>(embedded->mHeight), displayPath);
        }
        if (!raw) return nullptr;

        auto tex = std::shared_ptr<Texture>(raw);
        m_textures[cacheKey] = tex;
        m_textures[displayPath] = tex;
        m_textures[LowerAssetPath(displayPath)] = tex;
        m_embeddedTextures[LowerAssetPath(displayPath)] = tex;
        m_embeddedTextures[LowerAssetPath(AssetBaseName(displayPath))] = tex;
        if (!embeddedName.empty()) {
            m_embeddedTextures[LowerAssetPath(embeddedName)] = tex;
            m_embeddedTextures[LowerAssetPath(AssetBaseName(embeddedName))] = tex;
        }
        spdlog::info("  -> EMBEDDED[{}] '{}': OK", index, displayPath);
        return tex;
    };

    auto resolveExternalTexture = [&](const std::string& reference) -> std::shared_ptr<Texture> {
        const std::filesystem::path relative(NormalizeAssetPath(reference));
        std::filesystem::path resolved;
        std::error_code ec;
        if (relative.is_absolute()) {
            if (std::filesystem::exists(relative, ec)) resolved = relative;
        } else {
            const std::filesystem::path nearModel = std::filesystem::path(dir) / relative;
            if (std::filesystem::exists(nearModel, ec)) resolved = nearModel;
        }
        if (resolved.empty()) {
            const std::filesystem::path nearModel = std::filesystem::path(dir) / relative.filename();
            ec.clear();
            if (std::filesystem::exists(nearModel, ec)) resolved = nearModel;
        }
        if (resolved.empty()) return nullptr;

        auto tex = LoadTexture(resolved.string());
        if (tex && tex != m_white) {
            spdlog::info("  -> EXTERNAL '{}': OK", resolved.string());
            return tex;
        }
        return nullptr;
    };

    auto loadTextureReference = [&](const std::string& reference) -> std::shared_ptr<Texture> {
        if (reference.empty()) return nullptr;
        int embeddedIndex = -1;
        if (findEmbeddedTexture(reference, embeddedIndex))
            return loadEmbeddedTexture(embeddedIndex, reference);
        return resolveExternalTexture(reference);
    };

    // Assimp sometimes stores only the "*N" index in material texture slots.
    // Resolve it to the embedded file name before guessing the texture role,
    // otherwise every such slot is classified as Unknown.
    auto referenceDisplayName = [&](const std::string& reference) -> std::string {
        int embeddedIndex = -1;
        if (findEmbeddedTexture(reference, embeddedIndex)) {
            const std::string name =
                NormalizeAssetPath(scene->mTextures[embeddedIndex]->mFilename.C_Str());
            if (!name.empty()) return name;
        }
        return reference;
    };

    enum class TextureRole {
        Albedo, Normal, Metallic, Roughness, MetallicRoughness,
        Emissive, Occlusion, Opacity, Specular, Unknown
    };
    // Unreal-style ORM keeps AO in R, roughness in G and metallic in B. Only
    // this channel layout can safely be reused for the occlusion slot.
    auto isOcclusionRoughnessMetallic = [](const std::string& reference) {
        const std::string name = LowerAssetPath(reference);
        auto hasToken = [&](const char* token) {
            const size_t len = std::strlen(token);
            size_t pos = 0;
            while ((pos = name.find(token, pos)) != std::string::npos) {
                const bool left = pos == 0 ||
                    !std::isalnum(static_cast<unsigned char>(name[pos - 1]));
                const bool right = pos + len >= name.size() ||
                    !std::isalnum(static_cast<unsigned char>(name[pos + len]));
                if (left && right) return true;
                pos += len;
            }
            return false;
        };
        return hasToken("orm") || hasToken("occlusionroughnessmetallic");
    };
    auto classifyTexture = [](const std::string& reference) -> TextureRole {
        const std::string name = LowerAssetPath(reference);
        // Tokenize on separators so short markers such as "orm" or "ao" only
        // match whole words ("normal" must not be read as an ORM map).
        std::vector<std::string> tokens;
        std::string current;
        for (const unsigned char c : name) {
            if (std::isalnum(c)) {
                current.push_back(static_cast<char>(c));
            } else if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        }
        if (!current.empty()) tokens.push_back(current);

        auto tokenIs = [&](const char* value) {
            return std::find(tokens.begin(), tokens.end(), value) != tokens.end();
        };
        auto tokenHas = [&](const char* value) {
            return std::any_of(tokens.begin(), tokens.end(),
                               [&](const std::string& token) {
                                   return token.find(value) != std::string::npos;
                               });
        };

        // Combined masks first: their names also contain the individual map
        // words, but they must not be split into one of them.
        if (tokenHas("metallicroughness") || tokenHas("metalnessroughness") ||
            tokenHas("occlusionroughnessmetallic") ||
            tokenIs("orm") || tokenIs("rma") || tokenIs("mra"))
            return TextureRole::MetallicRoughness;
        // Explicit colour-map names win over secondary words in the same
        // filename, e.g. "Eye_Occlusion_Diffuse" and "Hair_Transparency_Diffuse"
        // are still albedo maps.
        if (tokenHas("diffuse") || tokenHas("albedo") || tokenHas("basecolor"))
            return TextureRole::Albedo;
        if (tokenHas("normal") || tokenIs("nrm") || tokenIs("nor"))
            return TextureRole::Normal;
        if (tokenHas("metallic") || tokenHas("metalness")) return TextureRole::Metallic;
        if (tokenHas("roughness") || tokenIs("rough")) return TextureRole::Roughness;
        if (tokenHas("emissive") || tokenHas("emission") || tokenIs("glow"))
            return TextureRole::Emissive;
        if (tokenHas("occlusion") || tokenIs("ao") || tokenIs("ambientocclusion"))
            return TextureRole::Occlusion;
        if (tokenHas("opacity") || tokenIs("alpha") || tokenHas("transparen"))
            return TextureRole::Opacity;
        if (tokenHas("specular") || tokenIs("spec")) return TextureRole::Specular;
        if (tokenHas("color")) return TextureRole::Albedo;
        return TextureRole::Unknown;
    };

    // === МАТЕРИАЛЫ ===
    out.materials.reserve(scene->mNumMaterials);
    for (unsigned i = 0; i < scene->mNumMaterials; ++i) {
        aiMaterial* aiMat = scene->mMaterials[i];
        auto mat = std::make_shared<Material>();

        aiString name;
        aiMat->Get(AI_MATKEY_NAME, name);
        mat->name = name.C_Str();

        spdlog::info("[AssetManager] Material[{}] '{}':", i, mat->name);

        std::vector<std::pair<int, std::string>> references;
        for (int type = 0; type <= static_cast<int>(AI_TEXTURE_TYPE_MAX); ++type) {
            const auto textureType = static_cast<aiTextureType>(type);
            const unsigned count = aiMat->GetTextureCount(textureType);
            for (unsigned slot = 0; slot < count; ++slot) {
                aiString texturePath;
                if (aiMat->GetTexture(textureType, slot, &texturePath) == AI_SUCCESS)
                    references.emplace_back(type, NormalizeAssetPath(texturePath.C_Str()));
            }
        }

        auto firstOf = [&](std::initializer_list<aiTextureType> types) -> std::string {
            for (const aiTextureType type : types) {
                for (const auto& ref : references) {
                    if (ref.first == static_cast<int>(type) && !ref.second.empty())
                        return ref.second;
                }
            }
            return {};
        };

        std::string albedoRef = firstOf({aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE});
        std::string normalRef = firstOf({aiTextureType_NORMAL_CAMERA, aiTextureType_NORMALS});
        std::string metallicRoughnessRef = firstOf({aiTextureType_GLTF_METALLIC_ROUGHNESS});
        std::string metallicRef = firstOf({aiTextureType_METALNESS});
        std::string roughnessRef = firstOf({aiTextureType_DIFFUSE_ROUGHNESS});
        std::string emissiveRef = firstOf({aiTextureType_EMISSION_COLOR, aiTextureType_EMISSIVE});
        std::string occlusionRef = firstOf({aiTextureType_AMBIENT_OCCLUSION});
        std::string opacityRef = firstOf({aiTextureType_OPACITY});
        std::string specularRef = firstOf({aiTextureType_SPECULAR});
        std::string firstUnknownRef;

        for (const auto& ref : references) {
            if (ref.first == static_cast<int>(aiTextureType_BASE_COLOR) ||
                ref.first == static_cast<int>(aiTextureType_DIFFUSE) ||
                ref.first == static_cast<int>(aiTextureType_NORMAL_CAMERA) ||
                ref.first == static_cast<int>(aiTextureType_NORMALS) ||
                ref.first == static_cast<int>(aiTextureType_METALNESS) ||
                ref.first == static_cast<int>(aiTextureType_DIFFUSE_ROUGHNESS) ||
                ref.first == static_cast<int>(aiTextureType_EMISSION_COLOR) ||
                ref.first == static_cast<int>(aiTextureType_EMISSIVE) ||
                ref.first == static_cast<int>(aiTextureType_AMBIENT_OCCLUSION) ||
                ref.first == static_cast<int>(aiTextureType_OPACITY) ||
                ref.first == static_cast<int>(aiTextureType_SPECULAR) ||
                ref.first == static_cast<int>(aiTextureType_GLTF_METALLIC_ROUGHNESS))
                continue;

            const TextureRole role = classifyTexture(referenceDisplayName(ref.second));
            if (role == TextureRole::Unknown) {
                if (firstUnknownRef.empty()) firstUnknownRef = ref.second;
                continue;
            }
            if (role == TextureRole::Albedo && albedoRef.empty()) albedoRef = ref.second;
            else if (role == TextureRole::Normal && normalRef.empty()) normalRef = ref.second;
            else if (role == TextureRole::Metallic && metallicRef.empty()) metallicRef = ref.second;
            else if (role == TextureRole::Roughness && roughnessRef.empty()) roughnessRef = ref.second;
            else if (role == TextureRole::MetallicRoughness && metallicRoughnessRef.empty()) metallicRoughnessRef = ref.second;
            else if (role == TextureRole::Emissive && emissiveRef.empty()) emissiveRef = ref.second;
            else if (role == TextureRole::Occlusion && occlusionRef.empty()) occlusionRef = ref.second;
            else if (role == TextureRole::Opacity && opacityRef.empty()) opacityRef = ref.second;
            else if (role == TextureRole::Specular && specularRef.empty()) specularRef = ref.second;
        }

        // A packed ORM/MR texture can be exposed through several Assimp
        // slots (for example SHININESS and METALNESS). Prefer the combined
        // map, whose G/B channels hold roughness/metallic; sampling the same
        // file as a separate metallic map would read its R (AO) channel.
        auto promotePacked = [&](std::string& ref) {
            if (!ref.empty() && metallicRoughnessRef.empty() &&
                classifyTexture(referenceDisplayName(ref)) == TextureRole::MetallicRoughness) {
                metallicRoughnessRef = ref;
                ref.clear();
            }
        };
        promotePacked(metallicRef);
        promotePacked(roughnessRef);

        auto sameRef = [](const std::string& lhs, const std::string& rhs) {
            return !lhs.empty() && LowerAssetPath(lhs) == LowerAssetPath(rhs);
        };
        if (!metallicRoughnessRef.empty()) {
            if (sameRef(metallicRef, metallicRoughnessRef)) metallicRef.clear();
            if (sameRef(roughnessRef, metallicRoughnessRef)) roughnessRef.clear();
        }

        if (albedoRef.empty() && !firstUnknownRef.empty()) albedoRef = firstUnknownRef;

        // Classic FBX sometimes stores metallic/roughness data in specular
        // or shininess slots. Use the filename as a hint.
        if (metallicRef.empty() && classifyTexture(referenceDisplayName(specularRef)) == TextureRole::Metallic)
            metallicRef = specularRef;
        if (roughnessRef.empty() && classifyTexture(referenceDisplayName(specularRef)) == TextureRole::Roughness)
            roughnessRef = specularRef;

        aiColor4D color;
        if (aiMat->Get(AI_MATKEY_BASE_COLOR, color) == AI_SUCCESS ||
            aiMat->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS) {
            mat->albedoFactor = {color.r, color.g, color.b, color.a};
        }

        float opacity = 1.0f;
        if (aiMat->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS)
            mat->albedoFactor.a *= glm::clamp(opacity, 0.0f, 1.0f);
        float transparency = 0.0f;
        if (aiMat->Get(AI_MATKEY_TRANSPARENCYFACTOR, transparency) == AI_SUCCESS)
            mat->albedoFactor.a *= 1.0f - glm::clamp(transparency, 0.0f, 1.0f);

        float metallic = 0.0f;
        const bool hasMetallicFactor =
            aiMat->Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS;
        if (hasMetallicFactor)
            mat->metallicFactor = glm::clamp(metallic, 0.0f, 1.0f);

        float roughness = 0.5f;
        float glossiness = 0.0f;
        const bool hasRoughnessFactor =
            aiMat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS;
        if (hasRoughnessFactor) {
            mat->roughnessFactor = glm::clamp(roughness, 0.04f, 1.0f);
        } else if (aiMat->Get(AI_MATKEY_GLOSSINESS_FACTOR, glossiness) == AI_SUCCESS) {
            mat->roughnessFactor = glm::clamp(1.0f - glossiness, 0.04f, 1.0f);
        } else {
            float shininess = 0.0f;
            if (aiMat->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS && shininess > 0.0f)
                mat->roughnessFactor =
                    glm::clamp(std::sqrt(2.0f / (shininess + 2.0f)), 0.04f, 1.0f);
        }

        aiColor3D emissive(0.0f, 0.0f, 0.0f);
        if (aiMat->Get(AI_MATKEY_COLOR_EMISSIVE, emissive) == AI_SUCCESS)
            mat->emissiveFactor = {emissive.r, emissive.g, emissive.b};
        float emissiveIntensity = 1.0f;
        if (aiMat->Get(AI_MATKEY_EMISSIVE_INTENSITY, emissiveIntensity) == AI_SUCCESS)
            mat->emissiveFactor *= glm::max(emissiveIntensity, 0.0f);

        float bumpScale = 1.0f;
        if (aiMat->Get(AI_MATKEY_BUMPSCALING, bumpScale) == AI_SUCCESS)
            mat->normalStrength = glm::max(bumpScale, 0.0f);

        float transmission = 0.0f;
        if (aiMat->Get(AI_MATKEY_TRANSMISSION_FACTOR, transmission) == AI_SUCCESS &&
            transmission > 0.05f) {
            mat->isGlass = true;
            // Prefer an explicit alpha value. Many FBX glass materials keep
            // alpha at 1.0 and only carry a transmission factor, so derive a
            // sensible base opacity from transmission in that case.
            if (mat->albedoFactor.a < 0.999f) {
                mat->glassOpacity = glm::clamp(mat->albedoFactor.a, 0.05f, 1.0f);
            } else {
                mat->glassOpacity = glm::clamp(1.0f - transmission * 0.75f, 0.1f, 0.9f);
            }
        }

        auto assign = [&](std::shared_ptr<Texture>& slot, const std::string& ref) {
            if (slot || ref.empty()) return;
            auto tex = loadTextureReference(ref);
            if (tex && tex != m_white) slot = tex;
        };

        assign(mat->albedoMap, albedoRef);
        assign(mat->normalMap, normalRef);
        assign(mat->metallicRoughnessMap, metallicRoughnessRef);
        assign(mat->metallicMap, metallicRef);
        assign(mat->roughnessMap, roughnessRef);
        assign(mat->emissiveMap, emissiveRef);
        assign(mat->occlusionMap, occlusionRef);
        assign(mat->opacityMap, opacityRef);
        assign(mat->specularMap, specularRef);

        // Reuse the R channel of packed ORM maps as ambient occlusion when the
        // material does not have a dedicated AO texture.
        if (!mat->occlusionMap && mat->metallicRoughnessMap &&
            isOcclusionRoughnessMetallic(referenceDisplayName(metallicRoughnessRef))) {
            mat->occlusionMap = mat->metallicRoughnessMap;
        }

        // FBX materials usually keep the viewport diffuse colour next to a
        // BaseColor texture. Multiplying both makes textured characters almost
        // black (the Korlan armour is a good example), so the texture replaces
        // the RGB tint. Alpha is preserved for opacity/glass handling.
        if (mat->albedoMap) {
            mat->albedoFactor.r = 1.0f;
            mat->albedoFactor.g = 1.0f;
            mat->albedoFactor.b = 1.0f;
        }
        // Texture-driven material factors default to 1.0. Assimp leaves the
        // metallic factor at 0 for FBX files that only provide a packed map.
        if (!hasMetallicFactor && (mat->metallicRoughnessMap || mat->metallicMap))
            mat->metallicFactor = 1.0f;
        if (!hasRoughnessFactor && (mat->metallicRoughnessMap || mat->roughnessMap))
            mat->roughnessFactor = 1.0f;

        if (!albedoRef.empty()) mat->sourcePath = albedoRef;

        spdlog::info("  PBR: base=({:.2f},{:.2f},{:.2f},{:.2f}) metal={:.2f} rough={:.2f} normal={:.2f} glass={}",
                     mat->albedoFactor.r, mat->albedoFactor.g, mat->albedoFactor.b, mat->albedoFactor.a,
                     mat->metallicFactor, mat->roughnessFactor, mat->normalStrength, mat->isGlass);
        spdlog::info("  maps: albedo={} normal={} metal={} rough={} mr={} emissive={} ao={} opacity={} specular={}",
                     mat->albedoMap != nullptr, mat->normalMap != nullptr,
                     mat->metallicMap != nullptr, mat->roughnessMap != nullptr,
                     mat->metallicRoughnessMap != nullptr, mat->emissiveMap != nullptr,
                     mat->occlusionMap != nullptr, mat->opacityMap != nullptr,
                     mat->specularMap != nullptr);

        if (!mat->albedoMap) {
            spdlog::warn("  No albedo texture found for material '{}'", mat->name);
        }

        out.materials.push_back(mat);
    }

    // === МЕШИ ===
    // Build mesh node transform map to apply correct offsets
    std::unordered_map<unsigned, glm::mat4> meshToNodeTransform;
    std::function<void(aiNode*, const glm::mat4&, int)> processNode = [&](aiNode* node, const glm::mat4& parentTransform, int depth) {
        if (depth > 128) return;
        glm::mat4 globalTransform = parentTransform * aiToGlm(node->mTransformation);
        for (unsigned i = 0; i < node->mNumMeshes; ++i) {
            meshToNodeTransform[node->mMeshes[i]] = globalTransform;
        }
        for (unsigned i = 0; i < node->mNumChildren; ++i) {
            processNode(node->mChildren[i], globalTransform, depth + 1);
        }
    };
    processNode(scene->mRootNode, glm::mat4(1.0f), 0);

    for (unsigned i = 0; i < scene->mNumMeshes; ++i) {
        const aiMesh* am = scene->mMeshes[i];
        glm::mat4 meshTransform = meshToNodeTransform[i];
        spdlog::info("[AssetManager] Mesh[{}]: {} verts, {} bones, material index {}",
                     i, am->mNumVertices, am->mNumBones, am->mMaterialIndex);

        std::vector<Vertex> verts;
        verts.reserve(am->mNumVertices);
        for (unsigned v = 0; v < am->mNumVertices; ++v) {
            Vertex vert{};
            vert.position = glm::vec3(meshTransform * glm::vec4(aiToVec3(am->mVertices[v]), 1.0f));
            if (am->HasNormals()) {
                vert.normal = glm::normalize(glm::vec3(meshTransform * glm::vec4(aiToVec3(am->mNormals[v]), 0.0f)));
            }
            if (am->HasTangentsAndBitangents()) vert.tangent = aiToVec3(am->mTangents[v]);
            if (am->HasTextureCoords(0)) vert.uv = aiToVec2(am->mTextureCoords[0][v]);
            vert.boneWeights = glm::vec4(0.0f);
            vert.boneIDs = glm::vec4(0.0f);
            verts.push_back(vert);
        }

        std::vector<uint32_t> indices;
        for (unsigned f = 0; f < am->mNumFaces; ++f) {
            const aiFace& face = am->mFaces[f];
            for (unsigned j = 0; j < face.mNumIndices; ++j)
                indices.push_back(face.mIndices[j]);
        }

        auto mesh = std::make_shared<Mesh>();
        mesh->Upload(verts, indices);
        out.meshes.push_back(mesh);
        out.meshMaterialIndex.push_back(am->mMaterialIndex);
    }

    // === BONES (из первого меша) ===
    std::unordered_map<std::string, int> globalBoneMap;
    int nextBoneIndex = 0;
    for (unsigned m = 0; m < scene->mNumMeshes; ++m) {
        const aiMesh* am = scene->mMeshes[m];
        for (unsigned b = 0; b < am->mNumBones; ++b) {
            std::string bname = am->mBones[b]->mName.data;
            if (globalBoneMap.find(bname) == globalBoneMap.end() && nextBoneIndex < 254)
                globalBoneMap[bname] = nextBoneIndex++;
        }
    }
    out.boneMap = globalBoneMap;
    out.totalBones = nextBoneIndex;
    out.hasAnimation = (out.rawScene && out.rawScene->mNumAnimations > 0);

    // Заполняем bone weights для каждого меша
    for (size_t i = 0; i < out.meshes.size(); ++i) {
        const aiMesh* am = scene->mMeshes[i];
        if (am->mNumBones == 0) continue;

        std::vector<Vertex> verts = out.meshes[i]->Vertices();
        std::vector<uint32_t> idx = out.meshes[i]->Indices();

        for (unsigned b = 0; b < am->mNumBones; ++b) {
            aiBone* bone = am->mBones[b];
            int globalIdx = globalBoneMap[bone->mName.data];
            for (unsigned w = 0; w < bone->mNumWeights; ++w) {
                unsigned vId = bone->mWeights[w].mVertexId;
                float weight = bone->mWeights[w].mWeight;
                for (int k = 0; k < 4; ++k) {
                    if (verts[vId].boneWeights[k] == 0.0f) {
                        verts[vId].boneIDs[k] = static_cast<float>(globalIdx);
                        verts[vId].boneWeights[k] = weight;
                        break;
                    }
                }
            }
        }
        for (auto& v : verts) {
            float totalWeight = v.boneWeights.x + v.boneWeights.y + v.boneWeights.z + v.boneWeights.w;
            if (totalWeight > 0.001f) {
                // Normalize weights to prevent vertex shrinking
                v.boneWeights /= totalWeight;
            } else {
                // Вершины без весов привязываем к dummy bone 255 (Identity)
                v.boneIDs[0] = 255.0f;
                v.boneWeights[0] = 1.0f;
            }
        }
        out.meshes[i]->Upload(verts, idx);
    }

    m_models[path] = out;
    spdlog::info("[AssetManager] === Loaded: {} ({} meshes, {} mats, {} bones) ===",
                 path, out.meshes.size(), out.materials.size(), nextBoneIndex);
    return out;


}

void AssetManager::UpdateAnimation(const LoadedModel& model, float time, std::vector<glm::mat4>& outBoneMatrices) {
    if (outBoneMatrices.size() != 256) outBoneMatrices.resize(256, glm::mat4(1.0f));
    outBoneMatrices[255] = glm::mat4(1.0f); // Dummy bone всегда Identity
    if (!model.rawScene) return;
    ReadNodeHeirarchy(model.rawScene.get(), time, model.rawScene->mRootNode, glm::mat4(1.0f), outBoneMatrices, model.boneMap);
}

std::shared_ptr<Mesh> AssetManager::CreateCubeMesh() {
    if (m_cubeMesh) return m_cubeMesh;
    struct V { float x,y,z, nx,ny,nz, tx,ty,tz, u,v; };
    static const V verts[24] = {
        { 0.5,-0.5,-0.5,  1,0,0, 0,0,-1, 0,0}, { 0.5,0.5,-0.5,  1,0,0, 0,0,-1, 0,1},
        { 0.5,0.5,0.5,  1,0,0, 0,0,-1, 1,1}, { 0.5,-0.5,0.5,  1,0,0, 0,0,-1, 1,0},
        { -0.5,-0.5,0.5, -1,0,0, 0,0,1, 0,0}, { -0.5,0.5,0.5, -1,0,0, 0,0,1, 0,1},
        { -0.5,0.5,-0.5, -1,0,0, 0,0,1, 1,1}, { -0.5,-0.5,-0.5, -1,0,0, 0,0,1, 1,0},
        { -0.5,0.5,-0.5,  0,1,0, 1,0,0, 0,0}, { -0.5,0.5,0.5,  0,1,0, 1,0,0, 0,1},
        { 0.5,0.5,0.5,  0,1,0, 1,0,0, 1,1}, { 0.5,0.5,-0.5,  0,1,0, 1,0,0, 1,0},
        { -0.5,-0.5,0.5,  0,-1,0,1,0,0, 0,0}, { -0.5,-0.5,-0.5,  0,-1,0,1,0,0, 0,1},
        { 0.5,-0.5,-0.5,  0,-1,0,1,0,0, 1,1}, { 0.5,-0.5,0.5,  0,-1,0,1,0,0, 1,0},
        { -0.5,-0.5,0.5,  0,0,1, 1,0,0, 0,0}, { -0.5,0.5,0.5,  0,0,1, 1,0,0, 0,1},
        { 0.5,0.5,0.5,  0,0,1, 1,0,0, 1,1}, { 0.5,-0.5,0.5,  0,0,1, 1,0,0, 1,0},
        { 0.5,-0.5,-0.5,  0,0,-1,-1,0,0, 0,0}, { 0.5,0.5,-0.5,  0,0,-1,-1,0,0, 0,1},
        { -0.5,0.5,-0.5,  0,0,-1,-1,0,0, 1,1}, { -0.5,-0.5,-0.5,  0,0,-1,-1,0,0, 1,0},
    };
    static const uint32_t idx[36] = {
        0,1,2, 0,2,3,  4,5,6, 4,6,7,
        8,9,10, 8,10,11, 12,13,14, 12,14,15,
        16,17,18, 16,18,19, 20,21,22, 20,22,23,
    };
    std::vector<Vertex> vs;
    for (auto& v : verts) {
        Vertex vert{};
        vert.position = {v.x, v.y, v.z}; vert.normal = {v.nx, v.ny, v.nz};
        vert.tangent = {v.tx, v.ty, v.tz}; vert.uv = {v.u, v.v};
        vert.boneIDs = glm::vec4(255.0f, 0, 0, 0); // Для кубов тоже используем Dummy Bone
        vert.boneWeights = glm::vec4(1.0f, 0, 0, 0);
        vs.push_back(vert);
    }
    std::vector<uint32_t> is(idx, idx + 36);
    m_cubeMesh = std::make_shared<Mesh>();
    m_cubeMesh->Upload(vs, is);
    return m_cubeMesh;
}

std::shared_ptr<Mesh> AssetManager::CreateSphereMesh() {
    if (m_sphereMesh) return m_sphereMesh;

    const int segments = 32;
    const int rings = 16;
    const float radius = 0.5f;  // unit sphere, diameter = 1.0

    std::vector<Vertex> verts;
    std::vector<uint32_t> indices;
    verts.reserve((segments + 1) * (rings + 1));

    for (int y = 0; y <= rings; ++y) {
        float v = (float)y / (float)rings;
        float phi = v * 3.14159265f;
        for (int x = 0; x <= segments; ++x) {
            float u = (float)x / (float)segments;
            float theta = u * 2.0f * 3.14159265f;
            Vertex vert{};
            vert.position.x = radius * sinf(phi) * cosf(theta);
            vert.position.y = radius * cosf(phi);
            vert.position.z = radius * sinf(phi) * sinf(theta);
            vert.normal = glm::normalize(vert.position);
            vert.tangent = glm::vec3(-sinf(theta), 0.0f, cosf(theta));
            vert.uv = { u, 1.0f - v };
            vert.boneIDs = glm::vec4(255.0f, 0, 0, 0);
            vert.boneWeights = glm::vec4(1.0f, 0, 0, 0);
            verts.push_back(vert);
        }
    }

    for (int y = 0; y < rings; ++y) {
        for (int x = 0; x < segments; ++x) {
            uint32_t i0 = y * (segments + 1) + x;
            uint32_t i1 = i0 + 1;
            uint32_t i2 = i0 + segments + 1;
            uint32_t i3 = i2 + 1;
            indices.push_back(i0); indices.push_back(i2); indices.push_back(i1);
            indices.push_back(i1); indices.push_back(i2); indices.push_back(i3);
        }
    }

    m_sphereMesh = std::make_shared<Mesh>();
    m_sphereMesh->Upload(verts, indices);
    return m_sphereMesh;
}

std::shared_ptr<Mesh> AssetManager::CreateCapsuleMesh(float halfHeight, float radius) {
    // Baked mesh: halfHeight + radius are actual sizes, scale = 1.0
    const int segments = 24;
    const int rings = 8;

    std::vector<Vertex> verts;
    std::vector<uint32_t> indices;

    // --- Top hemisphere (y from halfHeight to halfHeight + radius) ---
    int topOffset = 0;
    for (int y = 0; y <= rings; ++y) {
        float v = (float)y / (float)rings;
        float phi = v * 3.14159265f * 0.5f;  // 0..pi/2
        for (int x = 0; x <= segments; ++x) {
            float u = (float)x / (float)segments;
            float theta = u * 2.0f * 3.14159265f;
            Vertex vert{};
            vert.position.x = radius * sinf(phi) * cosf(theta);
            vert.position.y = halfHeight + radius * cosf(phi);
            vert.position.z = radius * sinf(phi) * sinf(theta);
            vert.normal = glm::normalize(glm::vec3(sinf(phi) * cosf(theta), cosf(phi), sinf(phi) * sinf(theta)));
            vert.tangent = glm::vec3(-sinf(theta), 0.0f, cosf(theta));
            vert.uv = { u, 1.0f - v * 0.25f };
            vert.boneIDs = glm::vec4(255.0f, 0, 0, 0);
            vert.boneWeights = glm::vec4(1.0f, 0, 0, 0);
            verts.push_back(vert);
        }
    }

    // --- Cylinder (y from -halfHeight to +halfHeight) ---
    int cylOffset = (int)verts.size();
    for (int y = 0; y <= 1; ++y) {
        float yPos = (y == 0) ? halfHeight : -halfHeight;
        for (int x = 0; x <= segments; ++x) {
            float u = (float)x / (float)segments;
            float theta = u * 2.0f * 3.14159265f;
            Vertex vert{};
            vert.position.x = radius * cosf(theta);
            vert.position.y = yPos;
            vert.position.z = radius * sinf(theta);
            vert.normal = glm::normalize(glm::vec3(cosf(theta), 0.0f, sinf(theta)));
            vert.tangent = glm::vec3(-sinf(theta), 0.0f, cosf(theta));
            vert.uv = { u, 0.75f - y * 0.5f };
            vert.boneIDs = glm::vec4(255.0f, 0, 0, 0);
            vert.boneWeights = glm::vec4(1.0f, 0, 0, 0);
            verts.push_back(vert);
        }
    }

    // --- Bottom hemisphere (y from -halfHeight - radius to -halfHeight) ---
    int botOffset = (int)verts.size();
    for (int y = 0; y <= rings; ++y) {
        float v = (float)y / (float)rings;
        float phi = 3.14159265f * 0.5f + v * 3.14159265f * 0.5f;  // pi/2..pi
        for (int x = 0; x <= segments; ++x) {
            float u = (float)x / (float)segments;
            float theta = u * 2.0f * 3.14159265f;
            Vertex vert{};
            vert.position.x = radius * sinf(phi) * cosf(theta);
            vert.position.y = -halfHeight + radius * cosf(phi);
            vert.position.z = radius * sinf(phi) * sinf(theta);
            vert.normal = glm::normalize(glm::vec3(sinf(phi) * cosf(theta), cosf(phi), sinf(phi) * sinf(theta)));
            vert.tangent = glm::vec3(-sinf(theta), 0.0f, cosf(theta));
            vert.uv = { u, 0.25f - v * 0.25f };
            vert.boneIDs = glm::vec4(255.0f, 0, 0, 0);
            vert.boneWeights = glm::vec4(1.0f, 0, 0, 0);
            verts.push_back(vert);
        }
    }

    auto addQuad = [&](uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
        indices.push_back(a); indices.push_back(c); indices.push_back(b);
        indices.push_back(b); indices.push_back(c); indices.push_back(d);
    };

    // Top hemisphere triangles
    for (int y = 0; y < rings; ++y) {
        for (int x = 0; x < segments; ++x) {
            uint32_t i0 = topOffset + y * (segments + 1) + x;
            uint32_t i1 = i0 + 1;
            uint32_t i2 = i0 + segments + 1;
            uint32_t i3 = i2 + 1;
            indices.push_back(i0); indices.push_back(i2); indices.push_back(i1);
            indices.push_back(i1); indices.push_back(i2); indices.push_back(i3);
        }
    }
    // Top hemisphere bottom edge -> cylinder top edge
    for (int x = 0; x < segments; ++x) {
        uint32_t i0 = topOffset + rings * (segments + 1) + x;
        uint32_t i1 = i0 + 1;
        uint32_t i2 = cylOffset + x;
        uint32_t i3 = i2 + 1;
        addQuad(i0, i1, i2, i3);
    }
    // Cylinder sides
    for (int x = 0; x < segments; ++x) {
        uint32_t i0 = cylOffset + x;
        uint32_t i1 = i0 + 1;
        uint32_t i2 = cylOffset + segments + 1 + x;
        uint32_t i3 = i2 + 1;
        addQuad(i0, i1, i2, i3);
    }
    // Cylinder bottom edge -> bottom hemisphere top edge
    for (int x = 0; x < segments; ++x) {
        uint32_t i0 = cylOffset + segments + 1 + x;
        uint32_t i1 = i0 + 1;
        uint32_t i2 = botOffset + x;
        uint32_t i3 = i2 + 1;
        addQuad(i0, i1, i2, i3);
    }
    // Bottom hemisphere triangles
    for (int y = 0; y < rings; ++y) {
        for (int x = 0; x < segments; ++x) {
            uint32_t i0 = botOffset + y * (segments + 1) + x;
            uint32_t i1 = i0 + 1;
            uint32_t i2 = i0 + segments + 1;
            uint32_t i3 = i2 + 1;
            indices.push_back(i0); indices.push_back(i2); indices.push_back(i1);
            indices.push_back(i1); indices.push_back(i2); indices.push_back(i3);
        }
    }

    auto mesh = std::make_shared<Mesh>();
    mesh->Upload(verts, indices);
    return mesh;
}

} // namespace Galeshi

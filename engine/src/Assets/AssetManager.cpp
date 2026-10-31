#include "GaleshiEngine/Assets/AssetManager.hpp"
#include "GaleshiEngine/Assets/Texture.hpp"
#include "GaleshiEngine/Assets/Mesh.hpp"
#include "GaleshiEngine/Assets/Material.hpp"
#include "GaleshiEngine/Core/ProjectManager.hpp"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <spdlog/spdlog.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <functional>
#include <unordered_set>
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

std::string AssetManager::MakeRelativePath(const std::string& path) const {
    if (path.empty()) return path;
    std::filesystem::path p(path);
    if (p.is_relative()) return p.generic_string(); // Already relative

    std::string projPath = ProjectManager::GetProjectPath();
    if (!projPath.empty()) {
        std::filesystem::path proj = std::filesystem::absolute(projPath).lexically_normal();
        std::filesystem::path absPath = std::filesystem::absolute(p).lexically_normal();
        std::error_code ec;
        auto rel = std::filesystem::relative(absPath, proj, ec);
        if (!ec && !rel.empty() && rel.generic_string().rfind("..", 0) != 0) {
            return rel.generic_string();
        }
    }
    // Fallback: try to find "assets/" in the path
    std::string s = p.generic_string();
    size_t pos = s.find("/assets/");
    if (pos != std::string::npos) {
        return s.substr(pos + 1);
    }
    return p.generic_string();
}

std::string AssetManager::ResolvePath(const std::string& path) const {
    if (path.empty()) return path;
    std::filesystem::path p(path);
    if (p.is_absolute()) return path;
    std::string projPath = ProjectManager::GetProjectPath();
    if (!projPath.empty()) {
        return (std::filesystem::path(projPath) / path).string();
    }
    return path;
}

void AssetManager::ReleaseUnusedAssets(const std::vector<std::string>& retainedModels) {
    std::unordered_set<std::string> retained;
    retained.reserve(retainedModels.size());
    for (const auto& path : retainedModels) {
        if (!path.empty()) retained.insert(MakeRelativePath(path));
    }

    // First release model-cache entries whose meshes and materials are only
    // owned by the cache itself. Scene components keep shared_ptrs, so their
    // use_count is greater than one while the asset is still visible.
    int releasedModels = 0;
    for (auto it = m_models.begin(); it != m_models.end();) {
        const LoadedModel& model = it->second;
        bool externalReference = retained.count(it->first) != 0;
        if (!externalReference) {
            for (const auto& mesh : model.meshes) {
                if (mesh && mesh.use_count() > 1) { externalReference = true; break; }
            }
        }
        if (!externalReference) {
            for (const auto& material : model.materials) {
                if (material && material.use_count() > 1) { externalReference = true; break; }
            }
        }
        if (!externalReference) {
            it = m_models.erase(it);
            ++releasedModels;
        } else {
            ++it;
        }
    }

    // Texture caches intentionally contain multiple keys for one texture
    // (path/normalized/embedded aliases). Count those keys before deciding
    // whether any non-cache owner remains.
    std::unordered_map<const Texture*, int> cacheKeyCount;
    for (const auto& entry : m_textures) {
        if (entry.second) ++cacheKeyCount[entry.second.get()];
    }
    for (const auto& entry : m_embeddedTextures) {
        if (entry.second) ++cacheKeyCount[entry.second.get()];
    }

    std::unordered_set<const Texture*> releasedTextures;
    for (const auto& entry : m_textures) {
        const auto& tex = entry.second;
        if (!tex) continue;
        const auto countIt = cacheKeyCount.find(tex.get());
        const int keys = countIt == cacheKeyCount.end() ? 1 : countIt->second;
        if (tex.use_count() <= keys) releasedTextures.insert(tex.get());
    }
    for (const auto& entry : m_embeddedTextures) {
        const auto& tex = entry.second;
        if (!tex) continue;
        const auto countIt = cacheKeyCount.find(tex.get());
        const int keys = countIt == cacheKeyCount.end() ? 1 : countIt->second;
        if (tex.use_count() <= keys) releasedTextures.insert(tex.get());
    }

    for (auto it = m_textures.begin(); it != m_textures.end();) {
        if (it->second && releasedTextures.count(it->second.get())) {
            it = m_textures.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_embeddedTextures.begin(); it != m_embeddedTextures.end();) {
        if (it->second && releasedTextures.count(it->second.get())) {
            it = m_embeddedTextures.erase(it);
        } else {
            ++it;
        }
    }

    // Primitive meshes are recreated on demand. Imported models are the
    // large allocations; the small cached primitives are intentionally kept
    // because the renderer uses some of them without a scene-side owner.
    // The small cube/sphere primitives are cached by default, but if the last
    // entity using one was removed its GPU buffers can be released too.
    if (m_cubeMesh && m_cubeMesh.use_count() == 1) m_cubeMesh.reset();
    if (m_sphereMesh && m_sphereMesh.use_count() == 1) m_sphereMesh.reset();

    if (releasedModels > 0 || !releasedTextures.empty()) {
        spdlog::info("[AssetManager] Released {} unused models and {} textures",
                     releasedModels, releasedTextures.size());
    }
}

void AssetManager::Clear() {
    // Scene entities must release their shared_ptrs before this is called.
    // Clearing the model cache destroys imported meshes plus their materials
    // and textures; removing the cached primitives releases their GPU buffers
    // as well. The small default textures are shared engine resources and
    // remain alive.
    m_models.clear();
    m_textures.clear();
    m_embeddedTextures.clear();
    m_cubeMesh.reset();
    m_sphereMesh.reset();
    m_waterMesh.reset();
    m_treeTrunkMesh.reset();
    m_treeCanopyMesh.reset();
    m_grassMesh.reset();
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

// Colour and data textures must not share a cache entry: the same bitmap can
// be requested as sRGB for albedo and as linear data elsewhere.
std::string TextureCacheKey(const std::string& path, bool srgb) {
    return srgb ? "srgb:" + path : path;
}
} // namespace

std::shared_ptr<Texture> AssetManager::LoadTexture(const std::string& originalPath, bool srgb) {
    if (originalPath.empty()) return m_white;
    const std::string path = MakeRelativePath(originalPath);

    const std::string normalized = NormalizeAssetPath(path);
    const std::string key       = TextureCacheKey(path, srgb);
    const std::string normKey   = TextureCacheKey(normalized, srgb);

    auto it = m_textures.find(key);
    if (it != m_textures.end()) return it->second;
    if (normKey != key) {
        it = m_textures.find(normKey);
        if (it != m_textures.end()) return it->second;
    }

    const std::string lower = LowerAssetPath(normalized);
    auto embedded = m_embeddedTextures.find(TextureCacheKey(lower, srgb));
    if (embedded != m_embeddedTextures.end()) return embedded->second;
    embedded = m_embeddedTextures.find(
        TextureCacheKey(LowerAssetPath(AssetBaseName(normalized)), srgb));
    if (embedded != m_embeddedTextures.end()) return embedded->second;

    // Resolve against the project root, not the process working directory.
    // The editor normally runs from build/bin, while scene texture paths are
    // project-relative; checking `path` directly silently replaced external
    // character textures with the white fallback.
    std::error_code ec;
    const std::string resolved = ResolvePath(path);
    bool exists = std::filesystem::exists(resolved, ec);
    std::string resolvedFallback;
    if (!exists && normalized != path) {
        ec.clear();
        resolvedFallback = ResolvePath(normalized);
        exists = std::filesystem::exists(resolvedFallback, ec);
    }
    if (!exists) return m_white;

    Texture* raw = Texture::LoadFromFile(resolved, srgb);
    if (!raw && !resolvedFallback.empty()) {
        raw = Texture::LoadFromFile(resolvedFallback, srgb);
    }
    if (!raw) return m_white;

    std::shared_ptr<Texture> tex(raw);
    m_textures[key] = tex;
    m_textures[normKey] = tex;
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

void ReadNodeHeirarchy(const aiScene* scene, const aiAnimation* anim, float time,
                       const aiNode* node, const glm::mat4& parentTransform,
                       std::vector<glm::mat4>& boneMatrices,
                       const std::unordered_map<std::string, int>& boneMap,
                       int depth = 0) {
    if (!node || depth > 128) return;

    const std::string nodeName(node->mName.data);
    glm::mat4 nodeTransform = aiToGlm(node->mTransformation);

    if (anim) {
        const float ticksPerSecond = anim->mTicksPerSecond != 0.0 ? anim->mTicksPerSecond : 25.0f;
        const double duration = anim->mDuration;
        float timeInTicks = static_cast<float>(time * ticksPerSecond);
        if (duration > 0.0) {
            const double wrapped = std::fmod(static_cast<double>(timeInTicks), duration);
            timeInTicks = static_cast<float>(wrapped < 0.0 ? wrapped + duration : wrapped);
        }

        const aiNodeAnim* nodeAnim = nullptr;
        for (unsigned int i = 0; i < anim->mNumChannels; ++i) {
            if (anim->mChannels[i] && nodeName == anim->mChannels[i]->mNodeName.data) {
                nodeAnim = anim->mChannels[i];
                break;
            }
        }

        if (nodeAnim) {
            glm::vec3 pos(0.0f);
            if (nodeAnim->mNumPositionKeys > 0) {
                pos = aiToVec3(nodeAnim->mPositionKeys[0].mValue);
                for (unsigned int i = 0; i + 1 < nodeAnim->mNumPositionKeys; ++i) {
                    const auto& k0 = nodeAnim->mPositionKeys[i];
                    const auto& k1 = nodeAnim->mPositionKeys[i + 1];
                    if (timeInTicks < static_cast<float>(k1.mTime)) {
                        const float span = static_cast<float>(k1.mTime - k0.mTime);
                        const float factor = span > 1e-6f
                            ? std::clamp((timeInTicks - static_cast<float>(k0.mTime)) / span, 0.0f, 1.0f)
                            : 0.0f;
                        pos = glm::mix(aiToVec3(k0.mValue), aiToVec3(k1.mValue), factor);
                        break;
                    }
                    pos = aiToVec3(k1.mValue);
                }
            }

            glm::quat rot(1.0f, 0.0f, 0.0f, 0.0f);
            if (nodeAnim->mNumRotationKeys > 0) {
                const aiQuaternion& q0 = nodeAnim->mRotationKeys[0].mValue;
                rot = glm::normalize(glm::quat(q0.w, q0.x, q0.y, q0.z));
                for (unsigned int i = 0; i + 1 < nodeAnim->mNumRotationKeys; ++i) {
                    const auto& k0 = nodeAnim->mRotationKeys[i];
                    const auto& k1 = nodeAnim->mRotationKeys[i + 1];
                    if (timeInTicks < static_cast<float>(k1.mTime)) {
                        const float span = static_cast<float>(k1.mTime - k0.mTime);
                        const float factor = span > 1e-6f
                            ? std::clamp((timeInTicks - static_cast<float>(k0.mTime)) / span, 0.0f, 1.0f)
                            : 0.0f;
                        const glm::quat start(k0.mValue.w, k0.mValue.x, k0.mValue.y, k0.mValue.z);
                        const glm::quat end(k1.mValue.w, k1.mValue.x, k1.mValue.y, k1.mValue.z);
                        rot = glm::normalize(glm::slerp(start, end, factor));
                        break;
                    }
                    rot = glm::normalize(glm::quat(k1.mValue.w, k1.mValue.x, k1.mValue.y, k1.mValue.z));
                }
            }

            glm::vec3 scale(1.0f);
            if (nodeAnim->mNumScalingKeys > 0) {
                scale = aiToVec3(nodeAnim->mScalingKeys[0].mValue);
                for (unsigned int i = 0; i + 1 < nodeAnim->mNumScalingKeys; ++i) {
                    const auto& k0 = nodeAnim->mScalingKeys[i];
                    const auto& k1 = nodeAnim->mScalingKeys[i + 1];
                    if (timeInTicks < static_cast<float>(k1.mTime)) {
                        const float span = static_cast<float>(k1.mTime - k0.mTime);
                        const float factor = span > 1e-6f
                            ? std::clamp((timeInTicks - static_cast<float>(k0.mTime)) / span, 0.0f, 1.0f)
                            : 0.0f;
                        scale = glm::mix(aiToVec3(k0.mValue), aiToVec3(k1.mValue), factor);
                        break;
                    }
                    scale = aiToVec3(k1.mValue);
                }
            }

            nodeTransform = glm::translate(glm::mat4(1.0f), pos) *
                            glm::mat4_cast(rot) *
                            glm::scale(glm::mat4(1.0f), scale);
        }
    }

    const glm::mat4 globalTransform = parentTransform * nodeTransform;

    const auto boneIt = boneMap.find(nodeName);
    if (boneIt != boneMap.end()) {
        const int boneIndex = boneIt->second;
        if (boneIndex >= 0 && boneIndex < 254) {
            aiBone* bone = nullptr;
            for (unsigned m = 0; m < scene->mNumMeshes && !bone; ++m) {
                for (unsigned i = 0; i < scene->mMeshes[m]->mNumBones; ++i) {
                    if (nodeName == scene->mMeshes[m]->mBones[i]->mName.data) {
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
        ReadNodeHeirarchy(scene, anim, time, node->mChildren[i], globalTransform,
                          boneMatrices, boneMap, depth + 1);
    }
}
} // namespace

AssetManager::LoadedModel AssetManager::LoadModel(const std::string& originalPath) {
    if (originalPath.empty()) return LoadedModel{};
    const std::string path = MakeRelativePath(originalPath);
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

    const aiScene* scene = importer->ReadFile(ResolvePath(path), postProcess);

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

    auto loadEmbeddedTexture = [&](int index, const std::string& reference,
                                   bool srgb) -> std::shared_ptr<Texture> {
        if (index < 0 || index >= static_cast<int>(scene->mNumTextures)) return nullptr;
        const aiTexture* embedded = scene->mTextures[index];
        const std::string embeddedName = NormalizeAssetPath(embedded->mFilename.C_Str());
        std::string displayPath = NormalizeAssetPath(reference);
        if (displayPath.empty() || displayPath[0] == '*') displayPath = embeddedName;

        const std::string cacheKey = "embedded:" + std::to_string(index) + ":" +
                                     (srgb ? "srgb:" : "linear:") + embeddedName;
        auto cached = m_textures.find(cacheKey);
        if (cached != m_textures.end()) return cached->second;

        Texture* raw = nullptr;
        if (embedded->mHeight == 0) {
            raw = Texture::LoadFromMemory(
                reinterpret_cast<const unsigned char*>(embedded->pcData),
                static_cast<size_t>(embedded->mWidth), displayPath, srgb);
        } else {
            raw = Texture::CreateFromBgra8(
                reinterpret_cast<const unsigned char*>(embedded->pcData),
                static_cast<int>(embedded->mWidth),
                static_cast<int>(embedded->mHeight), displayPath, srgb);
        }
        if (!raw) return nullptr;

        auto tex = std::shared_ptr<Texture>(raw);
        m_textures[cacheKey] = tex;
        m_textures[TextureCacheKey(displayPath, srgb)] = tex;
        m_textures[TextureCacheKey(LowerAssetPath(displayPath), srgb)] = tex;
        m_embeddedTextures[TextureCacheKey(LowerAssetPath(displayPath), srgb)] = tex;
        m_embeddedTextures[TextureCacheKey(LowerAssetPath(AssetBaseName(displayPath)), srgb)] = tex;
        if (!embeddedName.empty()) {
            m_embeddedTextures[TextureCacheKey(LowerAssetPath(embeddedName), srgb)] = tex;
            m_embeddedTextures[TextureCacheKey(LowerAssetPath(AssetBaseName(embeddedName)), srgb)] = tex;
        }
        spdlog::info("  -> EMBEDDED[{}] '{}': OK{}", index, displayPath,
                     srgb ? " [sRGB]" : "");
        return tex;
    };

    auto resolveExternalTexture = [&](const std::string& reference,
                                      bool srgb) -> std::shared_ptr<Texture> {
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

        auto tex = LoadTexture(resolved.string(), srgb);
        if (tex && tex != m_white) {
            spdlog::info("  -> EXTERNAL '{}': OK{}", resolved.string(),
                         srgb ? " [sRGB]" : "");
            return tex;
        }
        return nullptr;
    };

    auto loadTextureReference = [&](const std::string& reference,
                                    bool srgb) -> std::shared_ptr<Texture> {
        if (reference.empty()) return nullptr;
        int embeddedIndex = -1;
        if (findEmbeddedTexture(reference, embeddedIndex))
            return loadEmbeddedTexture(embeddedIndex, reference, srgb);
        return resolveExternalTexture(reference, srgb);
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

        // Assimp exposes AI_MATKEY_TWOSIDED for formats that carry it. If the
        // key is missing (classic FBX often omits it) keep the old renderer
        // behaviour of drawing imported meshes from both sides; the material
        // checkbox lets artists opt into backface culling explicitly.
        int twoSided = 1;
        if (aiMat->Get(AI_MATKEY_TWOSIDED, twoSided) == AI_SUCCESS) {
            mat->twoSided = twoSided != 0;
        } else {
            mat->twoSided = true;
        }

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

        auto assign = [&](std::shared_ptr<Texture>& slot, const std::string& ref,
                          bool srgb) {
            if (slot || ref.empty()) return;
            auto tex = loadTextureReference(ref, srgb);
            if (tex && tex != m_white) slot = tex;
        };

        // Base-colour and emissive textures carry sRGB-encoded colour. All
        // other maps are linear data and must not be gamma-decoded.
        assign(mat->albedoMap, albedoRef, true);
        assign(mat->normalMap, normalRef, false);
        assign(mat->metallicRoughnessMap, metallicRoughnessRef, false);
        assign(mat->metallicMap, metallicRef, false);
        assign(mat->roughnessMap, roughnessRef, false);
        assign(mat->emissiveMap, emissiveRef, true);
        assign(mat->occlusionMap, occlusionRef, false);
        assign(mat->opacityMap, opacityRef, false);
        assign(mat->specularMap, specularRef, false);

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
    if (out.rawScene) {
        out.animationNames.reserve(out.rawScene->mNumAnimations);
        out.animationDurations.reserve(out.rawScene->mNumAnimations);
        for (unsigned i = 0; i < out.rawScene->mNumAnimations; ++i) {
            const aiAnimation* anim = out.rawScene->mAnimations[i];
            if (!anim) {
                out.animationNames.emplace_back("Animation " + std::to_string(i));
                out.animationDurations.push_back(0.0f);
                continue;
            }
            out.animationNames.emplace_back(
                anim->mName.length > 0 ? anim->mName.data
                                       : ("Animation " + std::to_string(i)));
            const double ticks = anim->mTicksPerSecond != 0.0 ? anim->mTicksPerSecond : 25.0;
            out.animationDurations.push_back(
                static_cast<float>(anim->mDuration > 0.0 ? anim->mDuration / ticks : 0.0));
        }
    }

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

void AssetManager::UpdateAnimation(const LoadedModel& model, float time,
                                   std::vector<glm::mat4>& outBoneMatrices) {
    UpdateAnimationClip(model, 0, time, outBoneMatrices);
}

void AssetManager::UpdateAnimationClip(const LoadedModel& model, int clip, float time,
                                       std::vector<glm::mat4>& outBoneMatrices) {
    if (outBoneMatrices.size() != 256) {
        outBoneMatrices.assign(256, glm::mat4(1.0f));
    } else {
        for (auto& matrix : outBoneMatrices) matrix = glm::mat4(1.0f);
    }
    outBoneMatrices[255] = glm::mat4(1.0f); // Dummy bone is always identity.
    if (!model.rawScene || !model.rawScene->mRootNode) return;

    const aiAnimation* anim = nullptr;
    if (clip >= 0 && clip < static_cast<int>(model.rawScene->mNumAnimations)) {
        anim = model.rawScene->mAnimations[clip];
    }
    ReadNodeHeirarchy(model.rawScene.get(), anim, time, model.rawScene->mRootNode,
                      glm::mat4(1.0f), outBoneMatrices, model.boneMap);
}

void AssetManager::UpdateAnimationBlended(const LoadedModel& model,
                                          int clipA, float timeA,
                                          int clipB, float timeB,
                                          float blend,
                                          std::vector<glm::mat4>& outBoneMatrices) {
    blend = std::clamp(blend, 0.0f, 1.0f);
    if (blend <= 0.0001f) {
        UpdateAnimationClip(model, clipA, timeA, outBoneMatrices);
        return;
    }
    if (blend >= 0.9999f) {
        UpdateAnimationClip(model, clipB, timeB, outBoneMatrices);
        return;
    }

    std::vector<glm::mat4> a, b;
    UpdateAnimationClip(model, clipA, timeA, a);
    UpdateAnimationClip(model, clipB, timeB, b);
    if (outBoneMatrices.size() != 256) outBoneMatrices.resize(256, glm::mat4(1.0f));

    for (int i = 0; i < 255; ++i) {
        glm::vec3 scaleA, translationA, skewA;
        glm::vec4 perspectiveA;
        glm::quat rotationA;
        glm::vec3 scaleB, translationB, skewB;
        glm::vec4 perspectiveB;
        glm::quat rotationB;

        const bool okA = glm::decompose(a[i], scaleA, rotationA, translationA, skewA, perspectiveA);
        const bool okB = glm::decompose(b[i], scaleB, rotationB, translationB, skewB, perspectiveB);
        if (okA && okB) {
            const glm::vec3 translation = glm::mix(translationA, translationB, blend);
            const glm::quat rotation = glm::normalize(glm::slerp(rotationA, rotationB, blend));
            const glm::vec3 scale = glm::mix(scaleA, scaleB, blend);
            outBoneMatrices[i] = glm::translate(glm::mat4(1.0f), translation) *
                                 glm::mat4_cast(rotation) *
                                 glm::scale(glm::mat4(1.0f), scale);
        } else {
            // Fallback for matrices that cannot be decomposed (very rare for
            // skeleton bind poses): component-wise interpolation is still
            // better than a pop.
            for (int c = 0; c < 4; ++c) {
                outBoneMatrices[i][c] = glm::mix(a[i][c], b[i][c], blend);
            }
        }
    }
    outBoneMatrices[255] = glm::mat4(1.0f);
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
        // The Z faces use the reverse winding so their front faces point
        // outward like the other four sides.
        16,18,17, 16,19,18, 20,22,21, 20,23,22,
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

std::shared_ptr<Mesh> AssetManager::CreateWaterMesh() {
    if (m_waterMesh) return m_waterMesh;

    // A dense unit plane in XZ. The water shader displaces it in world space;
    // the component's width/depth are applied through the model matrix.
    constexpr int kSegments = 64;
    std::vector<Vertex> verts;
    std::vector<uint32_t> indices;
    verts.reserve((kSegments + 1) * (kSegments + 1));
    indices.reserve(kSegments * kSegments * 6);

    for (int z = 0; z <= kSegments; ++z) {
        const float v = static_cast<float>(z) / kSegments;
        for (int x = 0; x <= kSegments; ++x) {
            const float u = static_cast<float>(x) / kSegments;
            Vertex vert{};
            vert.position = {u - 0.5f, 0.0f, v - 0.5f};
            vert.normal = {0.0f, 1.0f, 0.0f};
            vert.tangent = {1.0f, 0.0f, 0.0f};
            vert.uv = {u, v};
            vert.boneIDs = glm::vec4(255.0f, 0.0f, 0.0f, 0.0f);
            vert.boneWeights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            verts.push_back(vert);
        }
    }

    for (int z = 0; z < kSegments; ++z) {
        for (int x = 0; x < kSegments; ++x) {
            const uint32_t i0 = static_cast<uint32_t>(z * (kSegments + 1) + x);
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + kSegments + 1;
            const uint32_t i3 = i2 + 1;
            indices.push_back(i0); indices.push_back(i2); indices.push_back(i1);
            indices.push_back(i1); indices.push_back(i2); indices.push_back(i3);
        }
    }

    m_waterMesh = std::make_shared<Mesh>();
    m_waterMesh->Upload(verts, indices);
    return m_waterMesh;
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

namespace {

Vertex VegetationVertex(const glm::vec3& position, const glm::vec3& normal,
                        const glm::vec3& tangent, const glm::vec2& uv) {
    Vertex v{};
    v.position = position;
    v.normal = normal;
    v.tangent = tangent;
    v.uv = uv;
    v.boneIDs = glm::vec4(255.0f, 0.0f, 0.0f, 0.0f);
    v.boneWeights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    return v;
}

void AppendCone(std::vector<Vertex>& verts, std::vector<uint32_t>& indices,
                const glm::vec3& baseCenter, float radius, float height,
                int segments, bool cap) {
    const uint32_t baseStart = static_cast<uint32_t>(verts.size());
    for (int i = 0; i < segments; ++i) {
        const float a = static_cast<float>(i) / static_cast<float>(segments) * 6.2831853f;
        const float ca = std::cos(a), sa = std::sin(a);
        const glm::vec3 p = baseCenter + glm::vec3(ca * radius, 0.0f, sa * radius);
        const glm::vec3 n = glm::normalize(glm::vec3(ca * height, radius, sa * height));
        verts.push_back(VegetationVertex(p, n, glm::vec3(-sa, 0.0f, ca),
                                         glm::vec2(static_cast<float>(i) / segments, 0.0f)));
    }
    const uint32_t apex = static_cast<uint32_t>(verts.size());
    verts.push_back(VegetationVertex(baseCenter + glm::vec3(0.0f, height, 0.0f),
                                     glm::vec3(0.0f, 1.0f, 0.0f),
                                     glm::vec3(1.0f, 0.0f, 0.0f), glm::vec2(0.5f, 1.0f)));
    for (int i = 0; i < segments; ++i) {
        const uint32_t a = baseStart + static_cast<uint32_t>(i);
        const uint32_t b = baseStart + static_cast<uint32_t>((i + 1) % segments);
        indices.push_back(a);
        indices.push_back(apex);
        indices.push_back(b);
    }
    if (cap) {
        const uint32_t center = static_cast<uint32_t>(verts.size());
        verts.push_back(VegetationVertex(baseCenter, glm::vec3(0.0f, -1.0f, 0.0f),
                                         glm::vec3(1.0f, 0.0f, 0.0f), glm::vec2(0.5f, 0.5f)));
        for (int i = 0; i < segments; ++i) {
            const uint32_t a = baseStart + static_cast<uint32_t>(i);
            const uint32_t b = baseStart + static_cast<uint32_t>((i + 1) % segments);
            indices.push_back(center);
            indices.push_back(a);
            indices.push_back(b);
        }
    }
}

} // namespace

std::shared_ptr<Mesh> AssetManager::CreateTreeTrunkMesh() {
    if (m_treeTrunkMesh) return m_treeTrunkMesh;
    std::vector<Vertex> verts;
    std::vector<uint32_t> indices;
    constexpr int segments = 8;
    constexpr float height = 3.0f;
    constexpr float bottomRadius = 0.22f;
    constexpr float topRadius = 0.12f;

    const uint32_t bottomStart = static_cast<uint32_t>(verts.size());
    for (int i = 0; i < segments; ++i) {
        const float a = static_cast<float>(i) / segments * 6.2831853f;
        const float ca = std::cos(a), sa = std::sin(a);
        verts.push_back(VegetationVertex(
            {ca * bottomRadius, 0.0f, sa * bottomRadius},
            {ca, 0.0f, sa}, {-sa, 0.0f, ca},
            {static_cast<float>(i) / segments, 0.0f}));
    }
    const uint32_t topStart = static_cast<uint32_t>(verts.size());
    for (int i = 0; i < segments; ++i) {
        const float a = static_cast<float>(i) / segments * 6.2831853f;
        const float ca = std::cos(a), sa = std::sin(a);
        verts.push_back(VegetationVertex(
            {ca * topRadius, height, sa * topRadius},
            {ca, 0.0f, sa}, {-sa, 0.0f, ca},
            {static_cast<float>(i) / segments, 1.0f}));
    }
    for (int i = 0; i < segments; ++i) {
        const uint32_t b0 = bottomStart + static_cast<uint32_t>(i);
        const uint32_t b1 = bottomStart + static_cast<uint32_t>((i + 1) % segments);
        const uint32_t t0 = topStart + static_cast<uint32_t>(i);
        const uint32_t t1 = topStart + static_cast<uint32_t>((i + 1) % segments);
        indices.push_back(b0); indices.push_back(t0); indices.push_back(b1);
        indices.push_back(b1); indices.push_back(t0); indices.push_back(t1);
    }

    m_treeTrunkMesh = std::make_shared<Mesh>();
    m_treeTrunkMesh->Upload(verts, indices);
    return m_treeTrunkMesh;
}

std::shared_ptr<Mesh> AssetManager::CreateTreeCanopyMesh() {
    if (m_treeCanopyMesh) return m_treeCanopyMesh;
    std::vector<Vertex> verts;
    std::vector<uint32_t> indices;
    AppendCone(verts, indices, {0.0f, 1.35f, 0.0f}, 1.35f, 1.65f, 10, true);
    AppendCone(verts, indices, {0.0f, 2.25f, 0.0f}, 1.02f, 1.45f, 10, false);
    AppendCone(verts, indices, {0.0f, 3.05f, 0.0f}, 0.68f, 1.15f, 10, false);

    m_treeCanopyMesh = std::make_shared<Mesh>();
    m_treeCanopyMesh->Upload(verts, indices);
    return m_treeCanopyMesh;
}

std::shared_ptr<Mesh> AssetManager::CreateGrassMesh() {
    if (m_grassMesh) return m_grassMesh;
    std::vector<Vertex> verts;
    std::vector<uint32_t> indices;

    auto addBlade = [&](const glm::vec3& right, const glm::vec3& normal) {
        const uint32_t base = static_cast<uint32_t>(verts.size());
        const glm::vec3 center(0.0f);
        const float width = 0.16f;
        const float height = 0.85f;
        const glm::vec3 p0 = center - right * width;
        const glm::vec3 p1 = center + right * width;
        const glm::vec3 p2 = center + right * width * 0.45f + glm::vec3(0.0f, height, 0.0f);
        const glm::vec3 p3 = center - right * width * 0.45f + glm::vec3(0.0f, height, 0.0f);
        verts.push_back(VegetationVertex(p0, normal, right, {0.0f, 0.0f}));
        verts.push_back(VegetationVertex(p1, normal, right, {1.0f, 0.0f}));
        verts.push_back(VegetationVertex(p2, normal, right, {1.0f, 1.0f}));
        verts.push_back(VegetationVertex(p3, normal, right, {0.0f, 1.0f}));
        indices.push_back(base + 0); indices.push_back(base + 1); indices.push_back(base + 2);
        indices.push_back(base + 0); indices.push_back(base + 2); indices.push_back(base + 3);
    };
    addBlade(glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.35f, 1.0f));
    addBlade(glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(1.0f, 0.35f, 0.0f));
    addBlade(glm::normalize(glm::vec3(1.0f, 0.0f, 1.0f)), glm::vec3(0.0f, 1.0f, 0.0f));

    m_grassMesh = std::make_shared<Mesh>();
    m_grassMesh->Upload(verts, indices);
    return m_grassMesh;
}

} // namespace Galeshi

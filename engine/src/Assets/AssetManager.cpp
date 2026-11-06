#include "GaleshiEngine/Assets/AssetManager.hpp"
#include "GaleshiEngine/Assets/Texture.hpp"
#include "GaleshiEngine/Assets/Mesh.hpp"
#include "GaleshiEngine/Assets/Material.hpp"
#include "GaleshiEngine/Core/ProjectManager.hpp"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/GltfMaterial.h>
#include <spdlog/spdlog.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace Galeshi {

const aiScene* g_currentAiScene = nullptr;

// Current aiScene used by MakeMaterialFromAssimp.

namespace {

constexpr char kModelCacheMagic[8] = {'G', 'A', 'M', 'C', 'A', 'C', 'H', '1'};
constexpr uint32_t kModelCacheVersion = 1;

void CacheWriteU32(std::vector<uint8_t>& out, uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<uint8_t>(value >> (i * 8)));
    }
}

void CacheWriteU64(std::vector<uint8_t>& out, uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<uint8_t>(value >> (i * 8)));
    }
}

void CacheWriteVarUInt(std::vector<uint8_t>& out, uint64_t value) {
    while (value >= 0x80u) {
        out.push_back(static_cast<uint8_t>(value) | 0x80u);
        value >>= 7;
    }
    out.push_back(static_cast<uint8_t>(value));
}

void CacheWriteString(std::vector<uint8_t>& out, const std::string& value) {
    CacheWriteVarUInt(out, value.size());
    out.insert(out.end(), value.begin(), value.end());
}

struct CacheReader {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t pos = 0;

    size_t Remaining() const { return size - pos; }
    uint8_t U8() {
        if (pos >= size) throw std::runtime_error("model cache: truncated data");
        return data[pos++];
    }
    uint32_t U32() {
        if (pos + 4 > size) throw std::runtime_error("model cache: truncated u32");
        uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            value |= static_cast<uint32_t>(data[pos++]) << (i * 8);
        }
        return value;
    }
    uint64_t U64() {
        if (pos + 8 > size) throw std::runtime_error("model cache: truncated u64");
        uint64_t value = 0;
        for (int i = 0; i < 8; ++i) {
            value |= static_cast<uint64_t>(data[pos++]) << (i * 8);
        }
        return value;
    }
    uint64_t VarUInt() {
        uint64_t value = 0;
        int shift = 0;
        for (int i = 0; i < 10; ++i) {
            const uint8_t byte = U8();
            value |= static_cast<uint64_t>(byte & 0x7fu) << shift;
            if ((byte & 0x80u) == 0) return value;
            shift += 7;
        }
        throw std::runtime_error("model cache: invalid varint");
    }
    std::string String() {
        const uint64_t length = VarUInt();
        if (length > Remaining()) {
            throw std::runtime_error("model cache: invalid string length");
        }
        std::string value(reinterpret_cast<const char*>(data + pos),
                          static_cast<size_t>(length));
        pos += static_cast<size_t>(length);
        return value;
    }
};

void CacheWriteFloat(std::vector<uint8_t>& out, float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    CacheWriteU32(out, bits);
}

float CacheReadFloat(CacheReader& reader) {
    const uint32_t bits = reader.U32();
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void CacheWriteDouble(std::vector<uint8_t>& out, double value) {
    uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    CacheWriteU64(out, bits);
}

double CacheReadDouble(CacheReader& reader) {
    const uint64_t bits = reader.U64();
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void CacheWriteVec2(std::vector<uint8_t>& out, const glm::vec2& value) {
    CacheWriteFloat(out, value.x);
    CacheWriteFloat(out, value.y);
}

glm::vec2 CacheReadVec2(CacheReader& reader) {
    return {CacheReadFloat(reader), CacheReadFloat(reader)};
}

void CacheWriteVec3(std::vector<uint8_t>& out, const glm::vec3& value) {
    CacheWriteFloat(out, value.x);
    CacheWriteFloat(out, value.y);
    CacheWriteFloat(out, value.z);
}

glm::vec3 CacheReadVec3(CacheReader& reader) {
    return {CacheReadFloat(reader), CacheReadFloat(reader),
            CacheReadFloat(reader)};
}

void CacheWriteVec4(std::vector<uint8_t>& out, const glm::vec4& value) {
    CacheWriteFloat(out, value.x);
    CacheWriteFloat(out, value.y);
    CacheWriteFloat(out, value.z);
    CacheWriteFloat(out, value.w);
}

glm::vec4 CacheReadVec4(CacheReader& reader) {
    return {CacheReadFloat(reader), CacheReadFloat(reader),
            CacheReadFloat(reader), CacheReadFloat(reader)};
}

std::string ModelCachePath() {
    const std::string projectPath = ProjectManager::GetProjectPath();
    if (projectPath.empty()) return {};
    return (std::filesystem::path(projectPath) / ".galeshi_model_cache.bin")
        .string();
}

bool ReadModelCacheFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;
    in.seekg(0, std::ios::end);
    const std::streamoff length = in.tellg();
    if (length < 0) return false;
    in.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(length));
    if (!out.empty()) {
        in.read(reinterpret_cast<char*>(out.data()),
                static_cast<std::streamsize>(out.size()));
    }
    return in.good() || in.eof();
}

bool WriteModelCacheFile(const std::string& path,
                         const std::vector<uint8_t>& data) {
    const std::string temporary = path + ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;
        if (!data.empty()) {
            out.write(reinterpret_cast<const char*>(data.data()),
                      static_cast<std::streamsize>(data.size()));
        }
        out.close();
        if (!out.good()) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        return false;
    }
    return true;
}

} // namespace

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

void AssetManager::Clear(bool keepImportedModels) {
    // Scene entities must release their shared_ptrs before this is called.
    // Clearing the model cache destroys imported meshes plus their materials
    // and textures; removing the cached primitives releases their GPU buffers
    // as well. The small default textures are shared engine resources and
    // remain alive. Imported models stay pinned when requested so a scene
    // load can reuse the Assimp result instead of importing again.
    if (!keepImportedModels) {
        m_importedModels.clear();
        m_importedSignatures.clear();
        m_embeddedTextures.clear();
        m_embeddedSources.clear();
    }
    m_models.clear();
    m_textures.clear();
    m_udimInfo.clear();
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
std::string TextureCacheKey(const std::string& path, bool srgb,
                            bool flipVertically = true) {
    std::string key = srgb ? "srgb:" + path : path;
    if (!flipVertically) key = "top-left:" + key;
    return key;
}
} // namespace

std::shared_ptr<Texture> AssetManager::LoadTexture(
    const std::string& originalPath, bool srgb, Texture::UdimInfo* udimInfo,
    bool flipVertically) {
    if (originalPath.empty()) return m_white;
    const std::string path = MakeRelativePath(originalPath);

    const std::string normalized = NormalizeAssetPath(path);
    const std::string key       = TextureCacheKey(path, srgb, flipVertically);
    const std::string normKey   =
        TextureCacheKey(normalized, srgb, flipVertically);

    // Substance Painter exports one file per UDIM tile. Probe the tile set
    // before the ordinary one-file cache so a file path from a material always
    // resolves to the complete atlas.
    if (Texture::IsUdimPath(path)) {
        const std::string udimKey = "udim:" + key;
        auto cached = m_textures.find(udimKey);
        if (cached != m_textures.end()) {
            if (udimInfo) {
                auto info = m_udimInfo.find(udimKey);
                if (info != m_udimInfo.end()) *udimInfo = info->second;
            }
            return cached->second;
        }

        std::error_code udimEc;
        const std::string resolvedUdim = ResolvePath(path);
        std::string resolvedUdimFallback;
        bool udimExists = std::filesystem::exists(resolvedUdim, udimEc);
        if (!udimExists && normalized != path) {
            udimEc.clear();
            resolvedUdimFallback = ResolvePath(normalized);
            udimExists = std::filesystem::exists(resolvedUdimFallback, udimEc);
        }
        if (udimExists) {
            Texture::UdimInfo info;
            Texture* rawUdim = Texture::LoadUdimFromFile(
                resolvedUdim, srgb, &info, flipVertically);
            if (!rawUdim && !resolvedUdimFallback.empty()) {
                rawUdim = Texture::LoadUdimFromFile(
                    resolvedUdimFallback, srgb, &info, flipVertically);
            }
            if (rawUdim) {
                std::shared_ptr<Texture> tex(rawUdim);
                m_textures[udimKey] = tex;
                m_udimInfo[udimKey] = info;
                m_textures[key] = tex;
                m_textures[normKey] = tex;
                if (udimInfo) *udimInfo = info;
                spdlog::info(
                    "[AssetManager] UDIM atlas '{}' tiles={}x{}",
                    path, info.tilesX, info.tilesY);
                return tex;
            }
        }
    }

    auto it = m_textures.find(key);
    if (it != m_textures.end()) return it->second;
    if (normKey != key) {
        it = m_textures.find(normKey);
        if (it != m_textures.end()) return it->second;
    }

    const std::string lower = LowerAssetPath(normalized);
    auto embedded = m_embeddedTextures.find(
        TextureCacheKey(lower, srgb, flipVertically));
    if (embedded != m_embeddedTextures.end()) return embedded->second;
    embedded = m_embeddedTextures.find(
        TextureCacheKey(LowerAssetPath(AssetBaseName(normalized)), srgb,
                        flipVertically));
    if (embedded != m_embeddedTextures.end()) return embedded->second;

    // The requested embedded variant has not been uploaded yet (for example
    // after the user toggled sRGB). Re-upload it from the retained source.
    auto loadEmbeddedVariant =
        [&](const std::string& lookup) -> std::shared_ptr<Texture> {
        const auto found = m_embeddedSources.find(lookup);
        if (found == m_embeddedSources.end() || !found->second ||
            found->second->data.empty()) {
            return nullptr;
        }
        const EmbeddedTextureSource& source = *found->second;
        Texture* raw = source.height == 0
            ? Texture::LoadFromMemory(source.data.data(), source.data.size(),
                                      source.displayPath, srgb, flipVertically)
            : Texture::CreateFromBgra8(source.data.data(), source.width,
                                       source.height, source.displayPath, srgb,
                                       flipVertically);
        if (!raw) return nullptr;
        std::shared_ptr<Texture> tex(raw);
        m_textures[key] = tex;
        m_textures[normKey] = tex;
        m_embeddedTextures[TextureCacheKey(lower, srgb, flipVertically)] = tex;
        m_embeddedTextures[TextureCacheKey(
            LowerAssetPath(AssetBaseName(normalized)), srgb, flipVertically)] = tex;
        return tex;
    };
    if (auto tex = loadEmbeddedVariant(lower)) return tex;
    if (auto tex = loadEmbeddedVariant(
            LowerAssetPath(AssetBaseName(normalized)))) {
        return tex;
    }

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

    Texture* raw = Texture::LoadFromFile(resolved, srgb, flipVertically);
    if (!raw && !resolvedFallback.empty()) {
        raw = Texture::LoadFromFile(resolvedFallback, srgb, flipVertically);
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

aiMatrix4x4 glmToAi(const glm::mat4& from) {
    aiMatrix4x4 to;
    to.a1 = from[0][0]; to.a2 = from[1][0]; to.a3 = from[2][0]; to.a4 = from[3][0];
    to.b1 = from[0][1]; to.b2 = from[1][1]; to.b3 = from[2][1]; to.b4 = from[3][1];
    to.c1 = from[0][2]; to.c2 = from[1][2]; to.c3 = from[2][2]; to.c4 = from[3][2];
    to.d1 = from[0][3]; to.d2 = from[1][3]; to.d3 = from[2][3]; to.d4 = from[3][3];
    return to;
}

void ReadNodeHeirarchy(const aiScene* scene, const aiAnimation* anim, float time,
                       const aiNode* node, const glm::mat4& parentTransform,
                       std::vector<glm::mat4>& boneMatrices,
                       const std::unordered_map<std::string, int>& boneMap,
                       const std::unordered_map<std::string, glm::mat4>&
                           boneOffsets,
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
            const auto offset = boneOffsets.find(nodeName);
            if (offset != boneOffsets.end()) {
                // The cached offset is the inverse bind matrix. In the
                // column-vector convention used by the engine the skinning
                // matrix is pose * inverseBind.
                boneMatrices[boneIndex] = globalTransform * offset->second;
            }
        }
    }

    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        ReadNodeHeirarchy(scene, anim, time, node->mChildren[i], globalTransform,
                          boneMatrices, boneMap, boneOffsets, depth + 1);
    }
}

// Collapses the animated node hierarchy into a name -> world-transform map.
// Separate animation FBX files often contain no skinned mesh (and therefore
// no aiBone list), so their poses cannot be sampled through boneMap alone.
void SampleNodeTransforms(const aiScene* scene, const aiAnimation* anim,
                          float time, const aiNode* node,
                          const glm::mat4& parentTransform,
                          std::unordered_map<std::string, glm::mat4>& out,
                          int depth = 0) {
    if (!node || depth > 128) return;

    const std::string nodeName(node->mName.data);
    glm::mat4 nodeTransform = aiToGlm(node->mTransformation);

    if (anim) {
        const float ticksPerSecond =
            anim->mTicksPerSecond != 0.0 ? anim->mTicksPerSecond : 25.0f;
        const double duration = anim->mDuration;
        float timeInTicks = time * ticksPerSecond;
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
                const auto& q0 = nodeAnim->mRotationKeys[0].mValue;
                rot = glm::normalize(glm::quat(q0.w, q0.x, q0.y, q0.z));
                for (unsigned int i = 0; i + 1 < nodeAnim->mNumRotationKeys; ++i) {
                    const auto& k0 = nodeAnim->mRotationKeys[i];
                    const auto& k1 = nodeAnim->mRotationKeys[i + 1];
                    if (timeInTicks < static_cast<float>(k1.mTime)) {
                        const float span = static_cast<float>(k1.mTime - k0.mTime);
                        const float factor = span > 1e-6f
                            ? std::clamp((timeInTicks - static_cast<float>(k0.mTime)) / span, 0.0f, 1.0f)
                            : 0.0f;
                        const glm::quat start(k0.mValue.w, k0.mValue.x,
                                              k0.mValue.y, k0.mValue.z);
                        const glm::quat end(k1.mValue.w, k1.mValue.x,
                                            k1.mValue.y, k1.mValue.z);
                        rot = glm::normalize(glm::slerp(start, end, factor));
                        break;
                    }
                    rot = glm::normalize(glm::quat(k1.mValue.w, k1.mValue.x,
                                                   k1.mValue.y, k1.mValue.z));
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
    out[nodeName] = globalTransform;
    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        SampleNodeTransforms(scene, anim, time, node->mChildren[i],
                             globalTransform, out, depth + 1);
    }
}

// Rebuilds target node globals from target rest locals. Matched bones use
// their desired global pose; unanimated/child bones keep their local rest and
// therefore inherit animation from their parents instead of being frozen at
// world-space identity.
void BuildRetargetedGlobals(
    const aiNode* node, const glm::mat4& parentTransform,
    const std::unordered_map<std::string, glm::mat4>& desiredGlobals,
    std::unordered_map<std::string, glm::mat4>& outGlobals, int depth = 0) {
    if (!node || depth > 128) return;
    const std::string nodeName(node->mName.data);
    glm::mat4 localTransform = aiToGlm(node->mTransformation);
    const auto desired = desiredGlobals.find(nodeName);
    if (desired != desiredGlobals.end()) {
        localTransform = glm::inverse(parentTransform) * desired->second;
    }
    const glm::mat4 globalTransform = parentTransform * localTransform;
    outGlobals[nodeName] = globalTransform;
    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        BuildRetargetedGlobals(node->mChildren[i], globalTransform,
                               desiredGlobals, outGlobals, depth + 1);
    }
}

std::string StripBoneNamespace(std::string name) {
    const size_t separator = name.find_last_of(":|");
    if (separator != std::string::npos) name.erase(0, separator + 1);
    return name;
}

std::string BoneNamespacePrefix(const std::string& name) {
    const size_t separator = name.find_last_of(":|");
    return separator == std::string::npos ? std::string{}
                                          : name.substr(0, separator + 1);
}

// Normalize only the bone-name portion. Exporters often swap separators
// ("Head.001" vs "Head_001", "Breast.L" vs "Breast_L") without changing the
// skeleton.
std::string NormalizeBoneName(std::string name) {
    name = StripBoneNamespace(std::move(name));
    std::string normalized;
    normalized.reserve(name.size());
    bool lastUnderscore = false;
    for (char c : name) {
        if (c == '.' || c == '-' || c == ':' || c == '|') c = '_';
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (c == '_') {
            if (lastUnderscore) continue;
            lastUnderscore = true;
        } else {
            lastUnderscore = false;
        }
        normalized.push_back(c);
    }
    return normalized;
}

std::string FindRootBoneName(const aiNode* node,
                             const std::unordered_map<std::string, int>& boneMap,
                             bool parentIsBone = false) {
    if (!node) return {};
    const bool isBone = boneMap.find(node->mName.C_Str()) != boneMap.end();
    if (isBone && !parentIsBone) return node->mName.C_Str();
    for (unsigned i = 0; i < node->mNumChildren; ++i) {
        const std::string found =
            FindRootBoneName(node->mChildren[i], boneMap, isBone);
        if (!found.empty()) return found;
    }
    return {};
}

std::string FindArmatureRootName(const aiNode* sceneRoot,
                                 const std::string& bonePrefix) {
    if (!sceneRoot) return {};
    std::string barePrefix = bonePrefix;
    if (!barePrefix.empty() &&
        (barePrefix.back() == ':' || barePrefix.back() == '|')) {
        barePrefix.pop_back();
    }
    if (!barePrefix.empty()) {
        for (unsigned i = 0; i < sceneRoot->mNumChildren; ++i) {
            if (sceneRoot->mChildren[i] &&
                barePrefix == sceneRoot->mChildren[i]->mName.C_Str()) {
                return barePrefix;
            }
        }
    }
    for (unsigned i = 0; i < sceneRoot->mNumChildren; ++i) {
        const aiNode* child = sceneRoot->mChildren[i];
        if (child && child->mNumChildren > 0) {
            return child->mName.C_Str();
        }
    }
    return sceneRoot->mNumChildren > 0 && sceneRoot->mChildren[0]
        ? sceneRoot->mChildren[0]->mName.C_Str() : std::string{};
}

void ScaleMatrixTranslation(glm::mat4& matrix, float scale) {
    matrix[3][0] *= scale;
    matrix[3][1] *= scale;
    matrix[3][2] *= scale;
}

// Conjugate a bone palette by a uniform import scale. Vertices are imported
// as p * importScale, so skinning matrices must be S * P * S^-1.
void ScaleBonePalette(std::vector<glm::mat4>& palettes, float scale) {
    if (std::abs(scale - 1.0f) <= 1e-7f) return;
    const glm::mat4 s = glm::scale(glm::mat4(1.0f), glm::vec3(scale));
    const glm::mat4 invS = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f / scale));
    for (size_t i = 0; i + 1 < palettes.size(); ++i) {
        palettes[i] = s * palettes[i] * invS;
    }
}

void CacheWriteMatrix(const glm::mat4& matrix, std::vector<uint8_t>& out) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            CacheWriteFloat(out, matrix[column][row]);
        }
    }
}

glm::mat4 CacheReadMatrix(CacheReader& reader) {
    glm::mat4 matrix(1.0f);
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            matrix[column][row] = CacheReadFloat(reader);
        }
    }
    return matrix;
}

void CacheWriteNode(const aiNode* node, std::vector<uint8_t>& out) {
    if (!node) {
        CacheWriteString(out, {});
        CacheWriteMatrix(glm::mat4(1.0f), out);
        CacheWriteVarUInt(out, 0);
        return;
    }
    CacheWriteString(out, node->mName.C_Str());
    CacheWriteMatrix(aiToGlm(node->mTransformation), out);
    CacheWriteVarUInt(out, node->mNumChildren);
    for (unsigned i = 0; i < node->mNumChildren; ++i) {
        CacheWriteNode(node->mChildren[i], out);
    }
}

aiNode* CacheReadNode(CacheReader& reader, int depth = 0) {
    if (depth > 128) throw std::runtime_error("model cache: node depth");
    const std::string name = reader.String();
    const glm::mat4 matrix = CacheReadMatrix(reader);
    auto* node = new aiNode(name);
    node->mTransformation = glmToAi(matrix);
    const uint64_t childCount = reader.VarUInt();
    if (childCount > 1000000) {
        delete node;
        throw std::runtime_error("model cache: too many node children");
    }
    node->mNumChildren = static_cast<unsigned>(childCount);
    if (childCount > 0) {
        node->mChildren = new aiNode*[childCount];
        for (uint64_t i = 0; i < childCount; ++i) {
            node->mChildren[i] = CacheReadNode(reader, depth + 1);
            node->mChildren[i]->mParent = node;
        }
    }
    return node;
}

void CacheWriteAnimation(const aiAnimation* animation,
                         std::vector<uint8_t>& out) {
    CacheWriteString(out, animation ? animation->mName.C_Str() : std::string{});
    CacheWriteDouble(out, animation ? animation->mDuration : 0.0);
    CacheWriteDouble(out, animation ? animation->mTicksPerSecond : 0.0);
    const unsigned channelCount = animation ? animation->mNumChannels : 0;
    CacheWriteVarUInt(out, channelCount);
    for (unsigned c = 0; c < channelCount; ++c) {
        const aiNodeAnim* channel = animation->mChannels[c];
        if (!channel) {
            CacheWriteString(out, {});
            CacheWriteVarUInt(out, 0);
            CacheWriteVarUInt(out, 0);
            CacheWriteVarUInt(out, 0);
            continue;
        }
        CacheWriteString(out, channel->mNodeName.C_Str());
        CacheWriteVarUInt(out, channel->mNumPositionKeys);
        for (unsigned i = 0; i < channel->mNumPositionKeys; ++i) {
            CacheWriteDouble(out, channel->mPositionKeys[i].mTime);
            CacheWriteFloat(out, channel->mPositionKeys[i].mValue.x);
            CacheWriteFloat(out, channel->mPositionKeys[i].mValue.y);
            CacheWriteFloat(out, channel->mPositionKeys[i].mValue.z);
        }
        CacheWriteVarUInt(out, channel->mNumRotationKeys);
        for (unsigned i = 0; i < channel->mNumRotationKeys; ++i) {
            CacheWriteDouble(out, channel->mRotationKeys[i].mTime);
            CacheWriteFloat(out, channel->mRotationKeys[i].mValue.w);
            CacheWriteFloat(out, channel->mRotationKeys[i].mValue.x);
            CacheWriteFloat(out, channel->mRotationKeys[i].mValue.y);
            CacheWriteFloat(out, channel->mRotationKeys[i].mValue.z);
        }
        CacheWriteVarUInt(out, channel->mNumScalingKeys);
        for (unsigned i = 0; i < channel->mNumScalingKeys; ++i) {
            CacheWriteDouble(out, channel->mScalingKeys[i].mTime);
            CacheWriteFloat(out, channel->mScalingKeys[i].mValue.x);
            CacheWriteFloat(out, channel->mScalingKeys[i].mValue.y);
            CacheWriteFloat(out, channel->mScalingKeys[i].mValue.z);
        }
    }
}

double CacheReadTime(CacheReader& reader) {
    return CacheReadDouble(reader);
}

aiAnimation* CacheReadAnimation(CacheReader& reader) {
    auto* animation = new aiAnimation();
    animation->mName = aiString(reader.String());
    animation->mDuration = CacheReadTime(reader);
    animation->mTicksPerSecond = CacheReadTime(reader);
    const uint64_t channelCount = reader.VarUInt();
    if (channelCount > 1000000) {
        delete animation;
        throw std::runtime_error("model cache: too many animation channels");
    }
    animation->mNumChannels = static_cast<unsigned>(channelCount);
    if (channelCount > 0) {
        animation->mChannels = new aiNodeAnim*[channelCount];
        for (uint64_t c = 0; c < channelCount; ++c) {
            auto* channel = new aiNodeAnim();
            channel->mNodeName = aiString(reader.String());
            const uint64_t positions = reader.VarUInt();
            if (positions > 10000000) {
                delete channel;
                delete animation;
                throw std::runtime_error("model cache: too many animation keys");
            }
            channel->mNumPositionKeys = static_cast<unsigned>(positions);
            if (positions > 0) {
                channel->mPositionKeys = new aiVectorKey[positions];
                for (uint64_t i = 0; i < positions; ++i) {
                    channel->mPositionKeys[i].mTime = CacheReadTime(reader);
                    channel->mPositionKeys[i].mValue.x = CacheReadFloat(reader);
                    channel->mPositionKeys[i].mValue.y = CacheReadFloat(reader);
                    channel->mPositionKeys[i].mValue.z = CacheReadFloat(reader);
                }
            }
            const uint64_t rotations = reader.VarUInt();
            if (rotations > 10000000) {
                delete channel;
                delete animation;
                throw std::runtime_error("model cache: too many animation keys");
            }
            channel->mNumRotationKeys = static_cast<unsigned>(rotations);
            if (rotations > 0) {
                channel->mRotationKeys = new aiQuatKey[rotations];
                for (uint64_t i = 0; i < rotations; ++i) {
                    channel->mRotationKeys[i].mTime = CacheReadTime(reader);
                    channel->mRotationKeys[i].mValue.w = CacheReadFloat(reader);
                    channel->mRotationKeys[i].mValue.x = CacheReadFloat(reader);
                    channel->mRotationKeys[i].mValue.y = CacheReadFloat(reader);
                    channel->mRotationKeys[i].mValue.z = CacheReadFloat(reader);
                }
            }
            const uint64_t scalings = reader.VarUInt();
            if (scalings > 10000000) {
                delete channel;
                delete animation;
                throw std::runtime_error("model cache: too many animation keys");
            }
            channel->mNumScalingKeys = static_cast<unsigned>(scalings);
            if (scalings > 0) {
                channel->mScalingKeys = new aiVectorKey[scalings];
                for (uint64_t i = 0; i < scalings; ++i) {
                    channel->mScalingKeys[i].mTime = CacheReadTime(reader);
                    channel->mScalingKeys[i].mValue.x = CacheReadFloat(reader);
                    channel->mScalingKeys[i].mValue.y = CacheReadFloat(reader);
                    channel->mScalingKeys[i].mValue.z = CacheReadFloat(reader);
                }
            }
            animation->mChannels[c] = channel;
        }
    }
    return animation;
}

void CacheWriteTexture(const std::shared_ptr<Texture>& texture,
                       std::vector<uint8_t>& out) {
    const bool hasTexture = texture && !texture->Path().empty() &&
                            texture->Path() != "<white>";
    CacheWriteVarUInt(out, hasTexture ? 1 : 0);
    if (!hasTexture) return;
    CacheWriteString(out, texture->Path());
    CacheWriteVarUInt(out, texture->IsSrgb() ? 1 : 0);
    CacheWriteVarUInt(out, texture->FlippedVertically() ? 1 : 0);
}

std::shared_ptr<Texture> CacheReadTexture(AssetManager& assets,
                                          CacheReader& reader) {
    if (reader.VarUInt() == 0) return nullptr;
    const std::string path = reader.String();
    const bool srgb = reader.VarUInt() != 0;
    const bool flip = reader.VarUInt() != 0;
    return assets.LoadTexture(path, srgb, nullptr, flip);
}

void CacheWriteMaterial(const Material& material, std::vector<uint8_t>& out) {
    CacheWriteString(out, material.name);
    CacheWriteString(out, material.sourcePath);
    CacheWriteVarUInt(out, material.sceneOverride ? 1 : 0);
    CacheWriteVarUInt(out, material.isGlass ? 1 : 0);
    CacheWriteVarUInt(out, material.twoSided ? 1 : 0);
    CacheWriteVarUInt(out, material.backfaceCulling ? 1 : 0);
    CacheWriteVec4(out, material.albedoFactor);
    CacheWriteVarUInt(out, material.albedoSrgb ? 1 : 0);
    CacheWriteVarUInt(out, material.metallicRoughnessSrgb ? 1 : 0);
    CacheWriteVarUInt(out, material.metallicSrgb ? 1 : 0);
    CacheWriteVarUInt(out, material.roughnessSrgb ? 1 : 0);
    CacheWriteVarUInt(out, material.normalSrgb ? 1 : 0);
    CacheWriteVarUInt(out, material.emissiveSrgb ? 1 : 0);
    CacheWriteVarUInt(out, material.occlusionSrgb ? 1 : 0);
    CacheWriteVarUInt(out, material.opacitySrgb ? 1 : 0);
    CacheWriteVarUInt(out, material.specularSrgb ? 1 : 0);
    CacheWriteVarUInt(out, material.landscapeAlbedoSrgb ? 1 : 0);
    CacheWriteVarUInt(out, material.landscapeOrmSrgb ? 1 : 0);
    CacheWriteVarUInt(out, material.landscapeNormalSrgb ? 1 : 0);
    CacheWriteVarUInt(out, material.landscapeBlend ? 1 : 0);
    CacheWriteFloat(out, material.metallicFactor);
    CacheWriteFloat(out, material.specularFactor);
    CacheWriteFloat(out, material.roughnessFactor);
    CacheWriteFloat(out, material.normalStrength);
    CacheWriteFloat(out, material.occlusionStrength);
    CacheWriteFloat(out, material.alphaCutoff);
    CacheWriteVarUInt(out, material.alphaTest ? 1 : 0);
    CacheWriteVarUInt(out, material.alphaBlend ? 1 : 0);
    CacheWriteFloat(out, material.glassOpacity);
    CacheWriteVec3(out, material.emissiveFactor);
    CacheWriteVarUInt(out, material.subsurfaceEnabled ? 1 : 0);
    CacheWriteVec3(out, material.subsurfaceColor);
    CacheWriteFloat(out, material.subsurfaceStrength);
    CacheWriteFloat(out, material.subsurfaceRadius);
    CacheWriteVarUInt(out, material.translucencyEnabled ? 1 : 0);
    CacheWriteVec3(out, material.translucencyColor);
    CacheWriteFloat(out, material.translucencyOpacity);
    CacheWriteFloat(out, material.translucencyStrength);
    CacheWriteFloat(out, material.translucencyThickness);
    CacheWriteFloat(out, material.translucencyDistortion);
    CacheWriteFloat(out, material.translucencyShadowDepth);
    CacheWriteVarUInt(out, material.castsShadow ? 1 : 0);
    CacheWriteFloat(out, material.shadowDepthOffset);
    CacheWriteVarUInt(out, material.usesUdim ? 1 : 0);
    CacheWriteVec2(out, material.udimUvScale);
    CacheWriteVec2(out, material.udimUvOffset);
    CacheWriteTexture(material.albedoMap, out);
    CacheWriteTexture(material.metallicRoughnessMap, out);
    CacheWriteTexture(material.metallicMap, out);
    CacheWriteTexture(material.roughnessMap, out);
    CacheWriteTexture(material.normalMap, out);
    CacheWriteTexture(material.emissiveMap, out);
    CacheWriteTexture(material.occlusionMap, out);
    CacheWriteTexture(material.opacityMap, out);
    CacheWriteTexture(material.specularMap, out);
    CacheWriteTexture(material.landscapeAlbedoMap, out);
    CacheWriteTexture(material.landscapeOrmMap, out);
    CacheWriteTexture(material.landscapeNormalMap, out);
}

std::shared_ptr<Material> CacheReadMaterial(AssetManager& assets,
                                            CacheReader& reader) {
    auto material = std::make_shared<Material>();
    material->name = reader.String();
    material->sourcePath = reader.String();
    material->sceneOverride = reader.VarUInt() != 0;
    material->isGlass = reader.VarUInt() != 0;
    material->twoSided = reader.VarUInt() != 0;
    material->backfaceCulling = reader.VarUInt() != 0;
    material->albedoFactor = CacheReadVec4(reader);
    material->albedoSrgb = reader.VarUInt() != 0;
    material->metallicRoughnessSrgb = reader.VarUInt() != 0;
    material->metallicSrgb = reader.VarUInt() != 0;
    material->roughnessSrgb = reader.VarUInt() != 0;
    material->normalSrgb = reader.VarUInt() != 0;
    material->emissiveSrgb = reader.VarUInt() != 0;
    material->occlusionSrgb = reader.VarUInt() != 0;
    material->opacitySrgb = reader.VarUInt() != 0;
    material->specularSrgb = reader.VarUInt() != 0;
    material->landscapeAlbedoSrgb = reader.VarUInt() != 0;
    material->landscapeOrmSrgb = reader.VarUInt() != 0;
    material->landscapeNormalSrgb = reader.VarUInt() != 0;
    material->landscapeBlend = reader.VarUInt() != 0;
    material->metallicFactor = CacheReadFloat(reader);
    material->specularFactor = CacheReadFloat(reader);
    material->roughnessFactor = CacheReadFloat(reader);
    material->normalStrength = CacheReadFloat(reader);
    material->occlusionStrength = CacheReadFloat(reader);
    material->alphaCutoff = CacheReadFloat(reader);
    material->alphaTest = reader.VarUInt() != 0;
    material->alphaBlend = reader.VarUInt() != 0;
    material->glassOpacity = CacheReadFloat(reader);
    material->emissiveFactor = CacheReadVec3(reader);
    material->subsurfaceEnabled = reader.VarUInt() != 0;
    material->subsurfaceColor = CacheReadVec3(reader);
    material->subsurfaceStrength = CacheReadFloat(reader);
    material->subsurfaceRadius = CacheReadFloat(reader);
    material->translucencyEnabled = reader.VarUInt() != 0;
    material->translucencyColor = CacheReadVec3(reader);
    material->translucencyOpacity = CacheReadFloat(reader);
    material->translucencyStrength = CacheReadFloat(reader);
    material->translucencyThickness = CacheReadFloat(reader);
    material->translucencyDistortion = CacheReadFloat(reader);
    material->translucencyShadowDepth = CacheReadFloat(reader);
    material->castsShadow = reader.VarUInt() != 0;
    material->shadowDepthOffset = CacheReadFloat(reader);
    material->usesUdim = reader.VarUInt() != 0;
    material->udimUvScale = CacheReadVec2(reader);
    material->udimUvOffset = CacheReadVec2(reader);
    material->albedoMap = CacheReadTexture(assets, reader);
    material->metallicRoughnessMap = CacheReadTexture(assets, reader);
    material->metallicMap = CacheReadTexture(assets, reader);
    material->roughnessMap = CacheReadTexture(assets, reader);
    material->normalMap = CacheReadTexture(assets, reader);
    material->emissiveMap = CacheReadTexture(assets, reader);
    material->occlusionMap = CacheReadTexture(assets, reader);
    material->opacityMap = CacheReadTexture(assets, reader);
    material->specularMap = CacheReadTexture(assets, reader);
    material->landscapeAlbedoMap = CacheReadTexture(assets, reader);
    material->landscapeOrmMap = CacheReadTexture(assets, reader);
    material->landscapeNormalMap = CacheReadTexture(assets, reader);
    return material;
}

void CacheWriteMesh(const std::shared_ptr<Mesh>& mesh,
                    std::vector<uint8_t>& out) {
    const auto& vertices = mesh ? mesh->Vertices() : std::vector<Vertex>{};
    const auto& indices = mesh ? mesh->Indices() : std::vector<uint32_t>{};
    CacheWriteVarUInt(out, vertices.size());
    for (const Vertex& vertex : vertices) {
        CacheWriteVec3(out, vertex.position);
        CacheWriteVec3(out, vertex.normal);
        CacheWriteVec3(out, vertex.tangent);
        CacheWriteVec2(out, vertex.uv);
        CacheWriteVec4(out, vertex.boneIDs);
        CacheWriteVec4(out, vertex.boneWeights);
    }
    CacheWriteVarUInt(out, indices.size());
    for (uint32_t index : indices) CacheWriteU32(out, index);
}

std::shared_ptr<Mesh> CacheReadMesh(CacheReader& reader) {
    const uint64_t vertexCount = reader.VarUInt();
    if (vertexCount > 100000000) {
        throw std::runtime_error("model cache: too many vertices");
    }
    std::vector<Vertex> vertices(static_cast<size_t>(vertexCount));
    for (Vertex& vertex : vertices) {
        vertex.position = CacheReadVec3(reader);
        vertex.normal = CacheReadVec3(reader);
        vertex.tangent = CacheReadVec3(reader);
        vertex.uv = CacheReadVec2(reader);
        vertex.boneIDs = CacheReadVec4(reader);
        vertex.boneWeights = CacheReadVec4(reader);
    }
    const uint64_t indexCount = reader.VarUInt();
    if (indexCount > 300000000) {
        throw std::runtime_error("model cache: too many indices");
    }
    std::vector<uint32_t> indices(static_cast<size_t>(indexCount));
    for (uint32_t& index : indices) index = reader.U32();
    auto mesh = std::make_shared<Mesh>();
    if (!vertices.empty()) mesh->Upload(vertices, indices);
    return mesh;
}

void CacheWriteLoadedModel(const AssetManager::LoadedModel& model,
                           std::vector<uint8_t>& out) {
    CacheWriteVarUInt(out, model.meshes.size());
    for (const auto& mesh : model.meshes) CacheWriteMesh(mesh, out);
    CacheWriteVarUInt(out, model.materials.size());
    for (const auto& material : model.materials) {
        if (material) {
            CacheWriteVarUInt(out, 1);
            CacheWriteMaterial(*material, out);
        } else {
            CacheWriteVarUInt(out, 0);
        }
    }
    CacheWriteVarUInt(out, model.meshMaterialIndex.size());
    for (int index : model.meshMaterialIndex) {
        CacheWriteU32(out, static_cast<uint32_t>(index));
    }
    CacheWriteVarUInt(out, model.meshNames.size());
    for (const auto& name : model.meshNames) CacheWriteString(out, name);
    CacheWriteVarUInt(out, model.boneMap.size());
    for (const auto& [name, index] : model.boneMap) {
        CacheWriteString(out, name);
        CacheWriteU32(out, static_cast<uint32_t>(index));
    }
    CacheWriteVarUInt(out, model.boneOffsetMatrices.size());
    for (const auto& [name, matrix] : model.boneOffsetMatrices) {
        CacheWriteString(out, name);
        CacheWriteMatrix(matrix, out);
    }
    CacheWriteVarUInt(out, model.nodeRestGlobals.size());
    for (const auto& [name, matrix] : model.nodeRestGlobals) {
        CacheWriteString(out, name);
        CacheWriteMatrix(matrix, out);
    }
    CacheWriteString(out, model.rootBoneName);
    CacheWriteString(out, model.boneNamespacePrefix);
    CacheWriteString(out, model.armatureRootName);
    CacheWriteVarUInt(out, model.isGltf ? 1 : 0);
    CacheWriteFloat(out, model.unitScaleFactor);
    CacheWriteFloat(out, model.importScale);
    CacheWriteU32(out, static_cast<uint32_t>(model.totalBones));
    CacheWriteVarUInt(out, model.hasAnimation ? 1 : 0);
    CacheWriteVarUInt(out, model.animationNames.size());
    for (const auto& name : model.animationNames) CacheWriteString(out, name);
    CacheWriteVarUInt(out, model.animationDurations.size());
    for (float duration : model.animationDurations) {
        CacheWriteFloat(out, duration);
    }
    CacheWriteFloat(out, model.scale);
    CacheWriteMatrix(model.rootTransform, out);
    const bool hasScene = model.rawScene && model.rawScene->mRootNode;
    CacheWriteVarUInt(out, hasScene ? 1 : 0);
    if (hasScene) {
        CacheWriteNode(model.rawScene->mRootNode, out);
        CacheWriteVarUInt(out, model.rawScene->mNumAnimations);
        for (unsigned i = 0; i < model.rawScene->mNumAnimations; ++i) {
            CacheWriteAnimation(model.rawScene->mAnimations[i], out);
        }
    }
}

AssetManager::LoadedModel CacheReadLoadedModel(AssetManager& assets,
                                                CacheReader& reader) {
    AssetManager::LoadedModel model;
    const uint64_t meshCount = reader.VarUInt();
    if (meshCount > 1000000) throw std::runtime_error("model cache: meshes");
    model.meshes.reserve(static_cast<size_t>(meshCount));
    for (uint64_t i = 0; i < meshCount; ++i) {
        model.meshes.push_back(CacheReadMesh(reader));
    }
    const uint64_t materialCount = reader.VarUInt();
    if (materialCount > 1000000) throw std::runtime_error("model cache: materials");
    model.materials.reserve(static_cast<size_t>(materialCount));
    for (uint64_t i = 0; i < materialCount; ++i) {
        if (reader.VarUInt() != 0) {
            model.materials.push_back(CacheReadMaterial(assets, reader));
        } else {
            model.materials.push_back(nullptr);
        }
    }
    const uint64_t materialIndexCount = reader.VarUInt();
    model.meshMaterialIndex.reserve(static_cast<size_t>(materialIndexCount));
    for (uint64_t i = 0; i < materialIndexCount; ++i) {
        model.meshMaterialIndex.push_back(static_cast<int>(reader.U32()));
    }
    const uint64_t meshNameCount = reader.VarUInt();
    model.meshNames.reserve(static_cast<size_t>(meshNameCount));
    for (uint64_t i = 0; i < meshNameCount; ++i) {
        model.meshNames.push_back(reader.String());
    }
    const uint64_t boneCount = reader.VarUInt();
    for (uint64_t i = 0; i < boneCount; ++i) {
        std::string name = reader.String();
        const int index = static_cast<int>(reader.U32());
        model.boneMap.emplace(std::move(name), index);
    }
    const uint64_t offsetCount = reader.VarUInt();
    for (uint64_t i = 0; i < offsetCount; ++i) {
        std::string name = reader.String();
        const glm::mat4 matrix = CacheReadMatrix(reader);
        model.boneOffsetMatrices.emplace(std::move(name), matrix);
    }
    const uint64_t restCount = reader.VarUInt();
    for (uint64_t i = 0; i < restCount; ++i) {
        std::string name = reader.String();
        const glm::mat4 matrix = CacheReadMatrix(reader);
        model.nodeRestGlobals.emplace(std::move(name), matrix);
    }
    model.rootBoneName = reader.String();
    model.boneNamespacePrefix = reader.String();
    model.armatureRootName = reader.String();
    model.isGltf = reader.VarUInt() != 0;
    model.unitScaleFactor = CacheReadFloat(reader);
    model.importScale = CacheReadFloat(reader);
    model.totalBones = static_cast<int>(reader.U32());
    model.hasAnimation = reader.VarUInt() != 0;
    const uint64_t animationNameCount = reader.VarUInt();
    model.animationNames.reserve(static_cast<size_t>(animationNameCount));
    for (uint64_t i = 0; i < animationNameCount; ++i) {
        model.animationNames.push_back(reader.String());
    }
    const uint64_t durationCount = reader.VarUInt();
    model.animationDurations.reserve(static_cast<size_t>(durationCount));
    for (uint64_t i = 0; i < durationCount; ++i) {
        model.animationDurations.push_back(CacheReadFloat(reader));
    }
    model.scale = CacheReadFloat(reader);
    model.rootTransform = CacheReadMatrix(reader);
    if (reader.VarUInt() != 0) {
        auto scene = std::make_shared<aiScene>();
        scene->mRootNode = CacheReadNode(reader);
        const uint64_t animationCount = reader.VarUInt();
        if (animationCount > 1000000) {
            throw std::runtime_error("model cache: animations");
        }
        scene->mNumAnimations = static_cast<unsigned>(animationCount);
        if (animationCount > 0) {
            scene->mAnimations = new aiAnimation*[animationCount];
            for (uint64_t i = 0; i < animationCount; ++i) {
                scene->mAnimations[i] = CacheReadAnimation(reader);
            }
        }
        model.rawScene = std::move(scene);
    }
    return model;
}
} // namespace

AssetManager::LoadedModel AssetManager::ImportModel(
    const std::string& originalPath) {
    if (originalPath.empty()) return LoadedModel{};
    const std::string path = MakeRelativePath(originalPath);
    // An explicit import must re-read the file even if a scene-load cache
    // entry already exists.
    m_models.erase(path);
    m_importedModels.erase(path);
    m_importedSignatures.erase(path);
    LoadedModel model = LoadModel(path);
    if (!model.meshes.empty() || model.hasAnimation) {
        m_importedModels[path] = model;
        const std::string resolved = ResolvePath(path);
        std::error_code ec;
        const uint64_t fileSize =
            std::filesystem::is_regular_file(resolved, ec)
                ? static_cast<uint64_t>(std::filesystem::file_size(resolved, ec))
                : 0;
        uint64_t fileTime = 0;
        if (!ec) {
            const auto time = std::filesystem::last_write_time(resolved, ec);
            if (!ec) {
                fileTime = static_cast<uint64_t>(time.time_since_epoch().count());
            }
        }
        m_importedSignatures[path] = {fileSize, fileTime};
        spdlog::info("[AssetManager] Imported model cached: {}", path);
    }
    return model;
}

void AssetManager::SaveImportCache() const {
    const std::string path = ModelCachePath();
    if (path.empty()) return;

    struct SourceRecord {
        std::shared_ptr<EmbeddedTextureSource> source;
        std::vector<std::string> keys;
    };
    std::vector<SourceRecord> sources;
    std::unordered_map<const EmbeddedTextureSource*, size_t> sourceIndex;
    for (const auto& [key, source] : m_embeddedSources) {
        if (!source) continue;
        auto [it, inserted] = sourceIndex.emplace(source.get(), sources.size());
        if (inserted) {
            SourceRecord record;
            record.source = source;
            sources.push_back(std::move(record));
        }
        sources[it->second].keys.push_back(key);
    }

    std::vector<uint8_t> out;
    out.insert(out.end(), kModelCacheMagic, kModelCacheMagic + 8);
    CacheWriteU32(out, kModelCacheVersion);
    CacheWriteU32(out, static_cast<uint32_t>(sources.size()));
    CacheWriteU32(out, static_cast<uint32_t>(m_importedModels.size()));

    for (const SourceRecord& record : sources) {
        const EmbeddedTextureSource& source = *record.source;
        CacheWriteString(out, source.displayPath);
        CacheWriteU32(out, static_cast<uint32_t>(source.data.size()));
        CacheWriteU32(out, static_cast<uint32_t>(source.width));
        CacheWriteU32(out, static_cast<uint32_t>(source.height));
        if (!source.data.empty()) {
            out.insert(out.end(), source.data.begin(), source.data.end());
        }
        CacheWriteU32(out, static_cast<uint32_t>(record.keys.size()));
        for (const std::string& key : record.keys) {
            CacheWriteString(out, key);
        }
    }

    for (const auto& [modelPath, model] : m_importedModels) {
        CacheWriteString(out, modelPath);
        const auto signature = m_importedSignatures.find(modelPath);
        uint64_t fileSize = 0;
        uint64_t fileTime = 0;
        if (signature != m_importedSignatures.end()) {
            fileSize = signature->second.first;
            fileTime = signature->second.second;
        } else {
            const std::string resolved = ResolvePath(modelPath);
            std::error_code ec;
            fileSize = std::filesystem::is_regular_file(resolved, ec)
                ? static_cast<uint64_t>(
                      std::filesystem::file_size(resolved, ec))
                : 0;
            if (!ec) {
                const auto time =
                    std::filesystem::last_write_time(resolved, ec);
                if (!ec) {
                    fileTime = static_cast<uint64_t>(
                        time.time_since_epoch().count());
                }
            }
        }
        CacheWriteU64(out, fileSize);
        CacheWriteU64(out, fileTime);
        CacheWriteLoadedModel(model, out);
    }

    if (!WriteModelCacheFile(path, out)) {
        spdlog::warn("[AssetManager] Could not write model cache: {}", path);
    }
}

bool AssetManager::LoadImportCache() {
    const std::string path = ModelCachePath();
    if (path.empty()) return false;
    std::vector<uint8_t> data;
    if (!ReadModelCacheFile(path, data)) return false;
    try {
        CacheReader reader{data.data(), data.size(), 0};
        if (data.size() < 8 ||
            std::memcmp(data.data(), kModelCacheMagic, 8) != 0) {
            return false;
        }
        reader.pos = 8;
        if (reader.U32() != kModelCacheVersion) return false;
        const uint64_t sourceCount = reader.U32();
        const uint64_t modelCount = reader.U32();
        if (sourceCount > 100000 || modelCount > 100000) return false;

        m_embeddedSources.clear();
        m_embeddedTextures.clear();
        m_importedModels.clear();
        m_importedSignatures.clear();

        for (uint64_t i = 0; i < sourceCount; ++i) {
            const std::string displayPath = reader.String();
            const uint64_t dataSize = reader.U32();
            const int width = static_cast<int>(reader.U32());
            const int height = static_cast<int>(reader.U32());
            if (dataSize > reader.Remaining()) {
                throw std::runtime_error("model cache: source data");
            }
            auto source = std::make_shared<EmbeddedTextureSource>();
            source->displayPath = displayPath;
            source->width = width;
            source->height = height;
            source->data.resize(static_cast<size_t>(dataSize));
            if (dataSize > 0) {
                std::memcpy(source->data.data(), reader.data + reader.pos,
                            static_cast<size_t>(dataSize));
                reader.pos += static_cast<size_t>(dataSize);
            }
            const uint64_t keyCount = reader.U32();
            if (keyCount > 100000) {
                throw std::runtime_error("model cache: source keys");
            }
            for (uint64_t key = 0; key < keyCount; ++key) {
                m_embeddedSources[reader.String()] = source;
            }
        }

        for (uint64_t i = 0; i < modelCount; ++i) {
            const std::string modelPath = reader.String();
            const uint64_t fileSize = reader.U64();
            const uint64_t fileTime = reader.U64();
            LoadedModel model = CacheReadLoadedModel(*this, reader);

            const std::string resolved = ResolvePath(modelPath);
            std::error_code ec;
            bool valid = std::filesystem::is_regular_file(resolved, ec);
            if (valid && fileSize > 0) {
                valid = std::filesystem::file_size(resolved, ec) == fileSize;
            }
            if (valid && fileTime > 0) {
                const auto time = std::filesystem::last_write_time(resolved, ec);
                valid = !ec &&
                    static_cast<uint64_t>(time.time_since_epoch().count()) ==
                        fileTime;
            }
            if (valid && (!model.meshes.empty() || model.hasAnimation)) {
                m_importedModels[modelPath] = std::move(model);
                m_importedSignatures[modelPath] = {fileSize, fileTime};
            }
        }
        spdlog::info("[AssetManager] Loaded model cache: {} models",
                     m_importedModels.size());
        return !m_importedModels.empty();
    } catch (const std::exception& e) {
        spdlog::warn("[AssetManager] Could not read model cache {}: {}",
                     path, e.what());
        m_importedModels.clear();
        m_importedSignatures.clear();
        m_embeddedSources.clear();
        m_embeddedTextures.clear();
        return false;
    }
}

AssetManager::LoadedModel AssetManager::LoadModel(const std::string& originalPath) {
    if (originalPath.empty()) return LoadedModel{};
    const std::string path = MakeRelativePath(originalPath);
    auto it = m_models.find(path);
    if (it != m_models.end()) return it->second;
    auto imported = m_importedModels.find(path);
    if (imported != m_importedModels.end()) {
        m_models[path] = imported->second;
        spdlog::info("[AssetManager] Using imported model cache: {}", path);
        return imported->second;
    }

    spdlog::info("[AssetManager] === Loading: {} ===", path);

    LoadedModel out;
    auto importer = std::make_shared<Assimp::Importer>();

    // Texture loading flips images vertically, matching the bottom-left V
    // origin used by FBX/OBJ/Collada. glTF is the exception: its UV origin is
    // top-left, so it still needs Assimp's flip to use the same convention.
    std::string extension = LowerAssetPath(
        std::filesystem::path(path).extension().string());
    out.isGltf = extension == ".gltf" || extension == ".glb";

    // Keep the node/bone hierarchy alive. In particular NEVER add
    // aiProcess_PreTransformVertices here: that step bakes node transforms
    // into vertices, removes animations and destroys the skeleton/bone links.
    // aiProcess_OptimizeGraph / OptimizeMeshes / JoinIdenticalVertices can
    // also rename or collapse nodes that animation channels address, so they
    // are intentionally absent for skinned FBX assets.
    constexpr unsigned int kSkinnedImportFlags =
        aiProcess_Triangulate |
        aiProcess_GenSmoothNormals |
        aiProcess_CalcTangentSpace |
        aiProcess_LimitBoneWeights |
        aiProcess_PopulateArmatureData;
    static_assert((kSkinnedImportFlags & aiProcess_PreTransformVertices) == 0,
                  "Skinned FBX import must not pre-transform vertices");
    unsigned int postProcess = kSkinnedImportFlags;
    if (extension == ".gltf" || extension == ".glb") {
        // Assimp's glTF2 importer already flips V once internally. Undo that
        // flip here and keep glTF images in their authored top-left origin
        // (flipVertically=false below) so UVs and images stay consistent.
        postProcess |= aiProcess_FlipUVs;
        // glTF's front-face winding is opposite to the engine's FBX import
        // path after Assimp's coordinate conversion. Flip the indices once at
        // import time so Two Sided remains a genuine material option rather
        // than a required workaround.
        postProcess |= aiProcess_FlipWindingOrder;
    }
    spdlog::info(
        "[AssetManager] Assimp flags=Triangulate|GenSmoothNormals|"
        "CalcTangentSpace|LimitBoneWeights|PopulateArmatureData "
        "PreTransformVertices=off ({})",
        path);

    const aiScene* scene = importer->ReadFile(ResolvePath(path), postProcess);

    if (!scene || !scene->mRootNode) {
        spdlog::error("[AssetManager] Assimp failed: {}", importer->GetErrorString());
        return out;
    }

    g_currentAiScene = scene;
    out.importer = importer;
    out.rawScene = std::shared_ptr<aiScene>(const_cast<aiScene*>(scene), [](aiScene*){});
    if (scene->mMetaData) {
        scene->mMetaData->Get("UnitScaleFactor", out.unitScaleFactor);
    }
    // Do not apply the FBX centimeter conversion again: Blender's exported
    // node/armature scale already carries it. Applying UnitScaleFactor/100
    // here makes an already-correct 1.00 model 100x too small.
    out.importScale = 1.0f;
    spdlog::info("[AssetManager] UnitScaleFactor: {}, importScale: {} ({})",
                 out.unitScaleFactor, out.importScale, path);
    SampleNodeTransforms(scene, nullptr, 0.0f, scene->mRootNode,
                         glm::mat4(1.0f), out.nodeRestGlobals);

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
                                   bool srgb,
                                   bool flipVertically) -> std::shared_ptr<Texture> {
        if (index < 0 || index >= static_cast<int>(scene->mNumTextures)) return nullptr;
        const aiTexture* embedded = scene->mTextures[index];
        const std::string embeddedName = NormalizeAssetPath(embedded->mFilename.C_Str());
        std::string displayPath = NormalizeAssetPath(reference);
        if (displayPath.empty() || displayPath[0] == '*') displayPath = embeddedName;

        const std::string cacheKey = "embedded:" + std::to_string(index) + ":" +
                                     (srgb ? "srgb:" : "linear:") +
                                     (flipVertically ? "flip:" : "top-left:") +
                                     embeddedName;
        auto cached = m_textures.find(cacheKey);
        if (cached != m_textures.end()) return cached->second;

        if (embedded->pcData && embedded->mWidth > 0 &&
            !displayPath.empty() &&
            m_embeddedSources.find(LowerAssetPath(displayPath)) ==
                m_embeddedSources.end()) {
            auto source = std::make_shared<EmbeddedTextureSource>();
            source->displayPath = displayPath;
            source->width = static_cast<int>(embedded->mWidth);
            source->height = static_cast<int>(embedded->mHeight);
            const std::size_t bytes = embedded->mHeight == 0
                ? static_cast<std::size_t>(embedded->mWidth)
                : static_cast<std::size_t>(embedded->mWidth) *
                      static_cast<std::size_t>(embedded->mHeight) * 4u;
            source->data.assign(
                reinterpret_cast<const unsigned char*>(embedded->pcData),
                reinterpret_cast<const unsigned char*>(embedded->pcData) + bytes);
            auto remember = [&](const std::string& key) {
                if (!key.empty()) m_embeddedSources[key] = source;
            };
            remember(LowerAssetPath(displayPath));
            remember(LowerAssetPath(AssetBaseName(displayPath)));
            if (!embeddedName.empty()) {
                remember(LowerAssetPath(embeddedName));
                remember(LowerAssetPath(AssetBaseName(embeddedName)));
            }
        }

        Texture* raw = nullptr;
        if (embedded->mHeight == 0) {
            raw = Texture::LoadFromMemory(
                reinterpret_cast<const unsigned char*>(embedded->pcData),
                static_cast<size_t>(embedded->mWidth), displayPath, srgb,
                flipVertically);
        } else {
            raw = Texture::CreateFromBgra8(
                reinterpret_cast<const unsigned char*>(embedded->pcData),
                static_cast<int>(embedded->mWidth),
                static_cast<int>(embedded->mHeight), displayPath, srgb,
                flipVertically);
        }
        if (!raw) return nullptr;

        auto tex = std::shared_ptr<Texture>(raw);
        m_textures[cacheKey] = tex;
        m_textures[TextureCacheKey(displayPath, srgb, flipVertically)] = tex;
        m_textures[TextureCacheKey(LowerAssetPath(displayPath), srgb,
                                   flipVertically)] = tex;
        m_embeddedTextures[TextureCacheKey(LowerAssetPath(displayPath), srgb,
                                           flipVertically)] = tex;
        m_embeddedTextures[TextureCacheKey(
            LowerAssetPath(AssetBaseName(displayPath)), srgb,
            flipVertically)] = tex;
        if (!embeddedName.empty()) {
            m_embeddedTextures[TextureCacheKey(
                LowerAssetPath(embeddedName), srgb, flipVertically)] = tex;
            m_embeddedTextures[TextureCacheKey(
                LowerAssetPath(AssetBaseName(embeddedName)), srgb,
                flipVertically)] = tex;
        }
        spdlog::info("  -> EMBEDDED[{}] '{}': OK{}", index, displayPath,
                     srgb ? " [sRGB]" : "");
        return tex;
    };

    auto resolveExternalTexture = [&](const std::string& reference,
                                      bool srgb,
                                      Texture::UdimInfo* udimInfo,
                                      bool flipVertically)
        -> std::shared_ptr<Texture> {
        const std::filesystem::path relative(NormalizeAssetPath(reference));
        std::filesystem::path resolved;
        std::error_code ec;

        auto resolveCandidate = [&](const std::filesystem::path& candidate) {
            if (candidate.empty()) return false;
            const std::filesystem::path resolvedCandidate =
                candidate.is_absolute()
                    ? candidate
                    : std::filesystem::path(ResolvePath(candidate.string()));
            if (!std::filesystem::exists(resolvedCandidate, ec)) return false;
            resolved = resolvedCandidate;
            return true;
        };

        if (relative.is_absolute()) {
            resolveCandidate(relative);
        } else if (resolveCandidate(std::filesystem::path(dir) / relative)) {
            // Resolved above.
        } else {
            resolveCandidate(std::filesystem::path(dir) / relative.filename());
        }
        if (resolved.empty()) return nullptr;

        auto tex = LoadTexture(resolved.string(), srgb, udimInfo,
                               flipVertically);
        if (tex && tex != m_white) {
            spdlog::info("  -> EXTERNAL '{}': OK{}", resolved.string(),
                         srgb ? " [sRGB]" : "");
            return tex;
        }
        return nullptr;
    };

    auto loadTextureReference = [&](const std::string& reference,
                                    bool srgb,
                                    Texture::UdimInfo* udimInfo = nullptr,
                                    bool flipVertically = true)
        -> std::shared_ptr<Texture> {
        if (reference.empty()) return nullptr;
        int embeddedIndex = -1;
        if (findEmbeddedTexture(reference, embeddedIndex))
            return loadEmbeddedTexture(embeddedIndex, reference, srgb,
                                       flipVertically);
        return resolveExternalTexture(reference, srgb, udimInfo,
                                      flipVertically);
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
    // Packed ORM maps keep AO in R, roughness in G and metallic in B. Only
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

    out.materials.reserve(scene->mNumMaterials);
    for (unsigned i = 0; i < scene->mNumMaterials; ++i) {
        aiMaterial* aiMat = scene->mMaterials[i];
        auto mat = std::make_shared<Material>();

        aiString name;
        aiMat->Get(AI_MATKEY_NAME, name);
        // Preserve the material name exactly as written by the exporter.
        // Blender/glTF/FBX names are the source of truth; do not invent a
        // fallback name here, because that would make the Details material
        // list diverge from the source asset.
        mat->name = name.C_Str();

        // Two Sided is disabled by default. Assimp writes an explicit false
        // for glTF materials without doubleSided, which matches this default.
        int twoSided = 0;
        if (aiMat->Get(AI_MATKEY_TWOSIDED, twoSided) == AI_SUCCESS) {
            mat->twoSided = twoSided != 0;
        } else {
            mat->twoSided = false;
        }
        mat->backfaceCulling = !mat->twoSided;

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
        // glTF stores occlusion in LIGHTMAP; FBX commonly uses
        // AMBIENT_OCCLUSION. Accept both formats.
        std::string occlusionRef =
            firstOf({aiTextureType_AMBIENT_OCCLUSION, aiTextureType_LIGHTMAP});
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

        // glTF stores MASK/BLEND alpha in the base-color texture. MASK uses a
        // cutoff; BLEND is resolved after the base-color texture is loaded so
        // opaque skin materials mislabelled as BLEND stay in the opaque pass.
        aiString gltfAlphaMode;
        if (aiMat->Get(AI_MATKEY_GLTF_ALPHAMODE, gltfAlphaMode) == AI_SUCCESS) {
            const std::string mode = gltfAlphaMode.C_Str();
            if (mode == "MASK") {
                mat->alphaTest = true;
                float cutoff = mat->alphaCutoff;
                if (aiMat->Get(AI_MATKEY_GLTF_ALPHACUTOFF, cutoff) == AI_SUCCESS) {
                    mat->alphaCutoff = glm::clamp(cutoff, 0.0f, 1.0f);
                }
            } else if (mode == "BLEND") {
                mat->alphaBlend = true;
            }
        }

        float metallic = 0.0f;
        const bool hasMetallicFactor =
            aiMat->Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS;
        if (hasMetallicFactor)
            mat->metallicFactor = glm::clamp(metallic, 0.0f, 1.0f);

        float specularFactor = 1.0f;
        if (aiMat->Get(AI_MATKEY_SPECULAR_FACTOR, specularFactor) == AI_SUCCESS)
            mat->specularFactor = glm::clamp(specularFactor, 0.0f, 1.0f);

        // Use the standard material default when the source does not provide
        // a roughness value.
        float roughness = 0.0f;
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
        if (out.isGltf) {
            // Blender's normal-map strength is glTF normalTexture.scale.
            if (aiMat->Get(AI_MATKEY_GLTF_TEXTURE_SCALE(
                               aiTextureType_NORMALS, 0),
                           bumpScale) == AI_SUCCESS) {
                mat->normalStrength = glm::max(bumpScale, 0.0f);
            }
        } else if (aiMat->Get(AI_MATKEY_BUMPSCALING, bumpScale) == AI_SUCCESS) {
            mat->normalStrength = glm::max(bumpScale, 0.0f);
        }
        if (out.isGltf) {
            float occlusionStrength = 1.0f;
            if (aiMat->Get(AI_MATKEY_GLTF_TEXTURE_STRENGTH(
                               aiTextureType_AMBIENT_OCCLUSION, 0),
                           occlusionStrength) == AI_SUCCESS) {
                mat->occlusionStrength =
                    glm::clamp(occlusionStrength, 0.0f, 1.0f);
            }
        }

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

        const bool textureFlipVertically = !out.isGltf;
        auto assign = [&](std::shared_ptr<Texture>& slot, const std::string& ref,
                          bool srgb) {
            if (slot || ref.empty()) return;
            Texture::UdimInfo udim;
            auto tex = loadTextureReference(ref, srgb, &udim,
                                            textureFlipVertically);
            if (tex && tex != m_white) {
                slot = tex;
                if (udim.valid) {
                    if (!mat->usesUdim) {
                        mat->usesUdim = true;
                        mat->udimUvScale = udim.uvScale;
                        mat->udimUvOffset = udim.uvOffset;
                    } else if (
                        mat->udimUvScale.x != udim.uvScale.x ||
                        mat->udimUvScale.y != udim.uvScale.y ||
                        mat->udimUvOffset.x != udim.uvOffset.x ||
                        mat->udimUvOffset.y != udim.uvOffset.y) {
                        spdlog::warn(
                            "[AssetManager] Material '{}' has UDIM maps with "
                            "different tile layouts; using the first layout",
                            mat->name);
                    }
                }
            }
        };

        // Base-colour and emissive textures carry sRGB-encoded colour. All
        // other maps are linear data and must not be gamma-decoded. The
        // per-map flags default to these same values and remain user-editable.
        mat->albedoSrgb = true;
        mat->normalSrgb = false;
        mat->metallicRoughnessSrgb = false;
        mat->metallicSrgb = false;
        mat->roughnessSrgb = false;
        mat->emissiveSrgb = true;
        mat->occlusionSrgb = false;
        mat->opacitySrgb = false;
        mat->specularSrgb = false;
        assign(mat->albedoMap, albedoRef, mat->albedoSrgb);
        assign(mat->normalMap, normalRef, mat->normalSrgb);
        assign(mat->metallicRoughnessMap, metallicRoughnessRef,
               mat->metallicRoughnessSrgb);
        assign(mat->metallicMap, metallicRef, mat->metallicSrgb);
        assign(mat->roughnessMap, roughnessRef, mat->roughnessSrgb);
        assign(mat->emissiveMap, emissiveRef, mat->emissiveSrgb);
        assign(mat->occlusionMap, occlusionRef, mat->occlusionSrgb);
        assign(mat->opacityMap, opacityRef, mat->opacitySrgb);
        assign(mat->specularMap, specularRef, mat->specularSrgb);

        // glTF BLEND is declared on the material, but exporters also mark
        // fully opaque skin/body materials as BLEND. Use the base-color alpha
        // to tell true transparency cards from opaque materials. Nearly
        // opaque textures (eyes) remain alpha-tested; mostly transparent ones
        // (hair, brows, lashes) go through the sorted forward blend pass.
        if (mat->alphaBlend) {
            const bool textureAlpha =
                mat->albedoMap && mat->albedoMap->HasAlpha();
            const bool constantAlpha = mat->albedoFactor.a < 0.999f;
            if (!textureAlpha && !constantAlpha) {
                mat->alphaBlend = false;
            } else if (constantAlpha ||
                       (textureAlpha &&
                        mat->albedoMap->AlphaCoverage() < 0.5f)) {
                mat->alphaTest = false;
            } else {
                // High alpha coverage: a cutout is cheaper and avoids sorting
                // artifacts on nearly solid geometry such as eyeballs.
                mat->alphaBlend = false;
                mat->alphaTest = true;
            }
        }

        // glTF's metallicRoughness texture is usually a packed ORM map:
        // R = occlusion, G = roughness, B = metallic. Use it for ambient
        // occlusion whenever the file has no separate occlusion texture.
        if (!mat->occlusionMap && mat->metallicRoughnessMap &&
            (out.isGltf ||
             isOcclusionRoughnessMetallic(
                 referenceDisplayName(metallicRoughnessRef)))) {
            mat->occlusionMap = mat->metallicRoughnessMap;
        }

        if (mat->albedoMap) {
            mat->albedoFactor.r = 1.0f;
            mat->albedoFactor.g = 1.0f;
            mat->albedoFactor.b = 1.0f;
        }
        if (!hasMetallicFactor &&
            (mat->metallicRoughnessMap || mat->metallicMap)) {
            mat->metallicFactor = 0.0f;
        }
        if (!hasRoughnessFactor && (mat->metallicRoughnessMap || mat->roughnessMap))
            mat->roughnessFactor = 1.0f;

        if (!albedoRef.empty()) mat->sourcePath = albedoRef;

        spdlog::info("  PBR: base=({:.2f},{:.2f},{:.2f},{:.2f}) metal={:.2f} rough={:.2f} normal={:.2f} glass={} alphaTest={} alphaBlend={}",
                     mat->albedoFactor.r, mat->albedoFactor.g, mat->albedoFactor.b, mat->albedoFactor.a,
                     mat->metallicFactor, mat->roughnessFactor, mat->normalStrength, mat->isGlass,
                     mat->alphaTest, mat->alphaBlend);
        spdlog::info("  maps: albedo={} normal={} metal={} rough={} mr={} emissive={} ao={} opacity={} specular={}",
                     mat->albedoMap != nullptr, mat->normalMap != nullptr,
                     mat->metallicMap != nullptr, mat->roughnessMap != nullptr,
                     mat->metallicRoughnessMap != nullptr, mat->emissiveMap != nullptr,
                     mat->occlusionMap != nullptr, mat->opacityMap != nullptr,
                     mat->specularMap != nullptr);

        if (!mat->albedoMap) {
            spdlog::warn(
                "  No albedo texture found for material slot {} '{}'",
                i, mat->name.empty() ? "(unnamed)" : mat->name);
        }

        out.materials.push_back(mat);
    }

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
        const glm::mat4 meshTransform = meshToNodeTransform[i];
        const glm::mat4 importTransform =
            glm::scale(glm::mat4(1.0f), glm::vec3(out.importScale));
        // glTF explicitly ignores the transform of a skinned mesh node: the
        // joint matrices already contain the skeleton/common-root transform.
        // Keeping vertices in mesh space and using inverseBindMatrices is the
        // spec-compliant path. Non-skinned glTF meshes still bake their node
        // transform like ordinary meshes.
        const bool gltfSkinnedMesh = out.isGltf && am->mNumBones > 0;
        const glm::mat4 vertexTransform = gltfSkinnedMesh
            ? importTransform
            : importTransform * meshTransform;
        const glm::mat4 normalTransform = gltfSkinnedMesh
            ? glm::mat4(1.0f)
            : meshTransform;
        spdlog::info("[AssetManager] Mesh[{}]: {} verts, {} bones, material index {}",
                     i, am->mNumVertices, am->mNumBones, am->mMaterialIndex);

        const Material* meshMaterial =
            am->mMaterialIndex < out.materials.size()
                ? out.materials[static_cast<size_t>(am->mMaterialIndex)].get()
                : nullptr;
        const bool remapUdimUvs = meshMaterial && meshMaterial->usesUdim;

        std::vector<Vertex> verts;
        verts.reserve(am->mNumVertices);
        for (unsigned v = 0; v < am->mNumVertices; ++v) {
            Vertex vert{};
            vert.position = glm::vec3(vertexTransform * glm::vec4(aiToVec3(am->mVertices[v]), 1.0f));
            if (am->HasNormals()) {
                vert.normal = glm::normalize(glm::vec3(normalTransform * glm::vec4(aiToVec3(am->mNormals[v]), 0.0f)));
            }
            if (am->HasTangentsAndBitangents()) vert.tangent = aiToVec3(am->mTangents[v]);
            if (am->HasTextureCoords(0)) {
                vert.uv = aiToVec2(am->mTextureCoords[0][v]);
                if (remapUdimUvs) {
                    vert.uv = vert.uv * meshMaterial->udimUvScale +
                              meshMaterial->udimUvOffset;
                }
            }
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
        out.meshNames.emplace_back(am->mName.C_Str());
    }

    std::unordered_map<std::string, int> globalBoneMap;
    int nextBoneIndex = 0;
    for (unsigned m = 0; m < scene->mNumMeshes; ++m) {
        const aiMesh* am = scene->mMeshes[m];
        for (unsigned b = 0; b < am->mNumBones; ++b) {
            std::string bname = am->mBones[b]->mName.data;
            if (globalBoneMap.find(bname) == globalBoneMap.end() &&
                nextBoneIndex < 254) {
                globalBoneMap[bname] = nextBoneIndex++;
            }
            out.boneOffsetMatrices.emplace(
                bname, aiToGlm(am->mBones[b]->mOffsetMatrix));
        }
    }
    out.boneMap = globalBoneMap;
    out.totalBones = nextBoneIndex;
    out.rootBoneName = FindRootBoneName(scene->mRootNode, globalBoneMap);
    if (out.rootBoneName.empty() && !globalBoneMap.empty()) {
        out.rootBoneName = globalBoneMap.begin()->first;
    }
    out.boneNamespacePrefix = BoneNamespacePrefix(out.rootBoneName);
    out.armatureRootName =
        FindArmatureRootName(scene->mRootNode, out.boneNamespacePrefix);

    // Build the FBX bind pose from the node hierarchy instead of
    // aiBone::mOffsetMatrix. The FBX offset also contains the mesh-node
    // transform (often scale 100 for Blender FBX) in addition to the inverse
    // bind transform. Vertices are already baked into mesh-node space, so
    // applying that offset would transform them twice and produce spikes.
    //
    // glTF is different: its inverseBindMatrices are authoritative and are
    // allowed to differ from inverse(node rest). Keep the glTF offset intact.
    if (out.isGltf) {
        spdlog::info("[Skinning] '{}' glTF inverse-bind from "
                     "inverseBindMatrices: {}",
                     path, out.boneOffsetMatrices.size());
    } else {
        int restBindCount = 0;
        for (auto& [boneName, bindMatrix] : out.boneOffsetMatrices) {
            const auto rest = out.nodeRestGlobals.find(boneName);
            if (rest != out.nodeRestGlobals.end()) {
                bindMatrix = glm::inverse(rest->second);
                ++restBindCount;
            }
        }
        spdlog::info("[Skinning] '{}' FBX inverse-bind from node rest: {}/{}",
                     path, restBindCount, out.boneOffsetMatrices.size());
    }

    if (!out.boneMap.empty()) {
        std::vector<std::string> names;
        names.reserve(out.boneMap.size());
        for (const auto& [name, index] : out.boneMap) {
            (void)index;
            names.push_back(name);
        }
        std::sort(names.begin(), names.end());
        spdlog::info("[Bones] target '{}' bones={} root='{}' prefix='{}'",
                     path, names.size(), out.rootBoneName,
                     out.boneNamespacePrefix);
        for (const std::string& name : names) {
            spdlog::info("[Bones]   {}", name);
        }
    }
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
        if (!out.animationNames.empty()) {
            spdlog::info("[Animations] '{}' embedded clips={}",
                         path, out.animationNames.size());
            for (size_t i = 0; i < out.animationNames.size(); ++i) {
                spdlog::info("[Animations]   [{}] '{}' duration={:.4f}s",
                             i, out.animationNames[i],
                             out.animationDurations[i]);
            }
        }
    }

    for (size_t i = 0; i < out.meshes.size(); ++i) {
        const aiMesh* am = scene->mMeshes[i];
        if (am->mNumBones == 0) continue;

        std::vector<Vertex> verts = out.meshes[i]->Vertices();
        std::vector<uint32_t> idx = out.meshes[i]->Indices();
        std::vector<std::vector<std::pair<int, float>>> influences(verts.size());

        for (unsigned b = 0; b < am->mNumBones; ++b) {
            aiBone* bone = am->mBones[b];
            const auto globalIndex = globalBoneMap.find(bone->mName.data);
            if (globalIndex == globalBoneMap.end()) continue;
            for (unsigned w = 0; w < bone->mNumWeights; ++w) {
                const unsigned vertexId = bone->mWeights[w].mVertexId;
                if (vertexId >= influences.size()) continue;
                const float weight = bone->mWeights[w].mWeight;
                if (weight <= 0.0f) continue;
                influences[vertexId].push_back({globalIndex->second, weight});
            }
        }
        for (size_t vertexId = 0; vertexId < verts.size(); ++vertexId) {
            Vertex& vertex = verts[vertexId];
            auto& weights = influences[vertexId];
            std::sort(weights.begin(), weights.end(),
                      [](const auto& a, const auto& b) {
                          return a.second > b.second;
                      });
            vertex.boneIDs = glm::vec4(0.0f);
            vertex.boneWeights = glm::vec4(0.0f);
            float totalWeight = 0.0f;
            for (int k = 0; k < 4 && k < static_cast<int>(weights.size()); ++k) {
                vertex.boneIDs[k] = static_cast<float>(weights[k].first);
                vertex.boneWeights[k] = weights[k].second;
                totalWeight += weights[k].second;
            }
            if (totalWeight > 0.001f) {
                // glTF and FBX may contain more than four influences. Keep
                // the four strongest and normalize them for the 4-influence
                // skinning path.
                vertex.boneWeights /= totalWeight;
            } else {
                // Vertices without valid weights stay at bind pose.
                vertex.boneIDs[0] = 255.0f;
                vertex.boneWeights[0] = 1.0f;
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
    static std::unordered_set<const void*> loggedEmbeddedChannels;
    if (anim && loggedEmbeddedChannels.insert(model.rawScene.get()).second) {
        std::vector<std::string> names;
        names.reserve(anim->mNumChannels);
        for (unsigned i = 0; i < anim->mNumChannels; ++i) {
            if (anim->mChannels[i]) {
                names.emplace_back(anim->mChannels[i]->mNodeName.C_Str());
            }
        }
        std::sort(names.begin(), names.end());
        spdlog::info("[Bones] embedded animation '{}' channels={}",
                     anim->mName.C_Str(), names.size());
        for (const std::string& name : names) {
            spdlog::info("[Bones]   {}", name);
        }
    }
    ReadNodeHeirarchy(model.rawScene.get(), anim, time,
                      model.rawScene->mRootNode, glm::mat4(1.0f),
                      outBoneMatrices, model.boneMap,
                      model.boneOffsetMatrices);
    ScaleBonePalette(outBoneMatrices, model.importScale);
}

void AssetManager::UpdateAnimationClipRetargeted(
    const LoadedModel& animationModel, int clip, float time,
    const LoadedModel& targetModel, std::vector<glm::mat4>& outBoneMatrices,
    float animationScale) {
    if (outBoneMatrices.size() != 256) {
        outBoneMatrices.assign(256, glm::mat4(1.0f));
    } else {
        for (auto& matrix : outBoneMatrices) matrix = glm::mat4(1.0f);
    }
    outBoneMatrices[255] = glm::mat4(1.0f);

    if (!animationModel.rawScene || !animationModel.rawScene->mRootNode ||
        !targetModel.rawScene || targetModel.boneMap.empty()) {
        return;
    }
    if (clip < 0 ||
        clip >= static_cast<int>(animationModel.rawScene->mNumAnimations)) {
        return;
    }
    animationScale = std::max(animationScale, 0.0f);

    // Map source armature space to target armature space. The two FBX files
    // can use different root orientations (for example Blender's animation
    // export swaps Y/Z relative to the character FBX), even when every bone's
    // local rest transform matches.
    glm::mat4 spaceCorrection(1.0f);
    {
        const auto sourceRoot = animationModel.nodeRestGlobals.find(
            animationModel.armatureRootName);
        const auto targetRoot = targetModel.nodeRestGlobals.find(
            targetModel.armatureRootName);
        if (sourceRoot != animationModel.nodeRestGlobals.end() &&
            targetRoot != targetModel.nodeRestGlobals.end()) {
            spaceCorrection =
                targetRoot->second * glm::inverse(sourceRoot->second);
        }
    }

    const aiAnimation* animation =
        animationModel.rawScene->mAnimations[clip];
    const float unitBridge = targetModel.importScale > 1e-6f
        ? animationModel.importScale / targetModel.importScale : 1.0f;

    // Global-space retargeting: sample the complete source pose in world
    // space, then apply each bone's rest-to-animated delta to the target bind.
    std::unordered_map<std::string, glm::mat4> sourceGlobalPose;
    sourceGlobalPose.reserve(256);
    SampleNodeTransforms(
        animationModel.rawScene.get(), animation, time,
        animationModel.rawScene->mRootNode, glm::mat4(1.0f),
        sourceGlobalPose);

    std::unordered_set<std::string> sourceChannelNodes;
    if (animation) {
        for (unsigned i = 0; i < animation->mNumChannels; ++i) {
            if (animation->mChannels[i]) {
                sourceChannelNodes.insert(
                    animation->mChannels[i]->mNodeName.C_Str());
            }
        }
    }

    static std::unordered_set<const void*> loggedChannelLists;
    if (animation && loggedChannelLists.insert(animationModel.rawScene.get()).second) {
        std::vector<std::string> names(sourceChannelNodes.begin(),
                                       sourceChannelNodes.end());
        std::sort(names.begin(), names.end());
        spdlog::info("[Bones] animation '{}' channels={}",
                     animation->mName.C_Str(), names.size());
        for (const std::string& name : names) {
            spdlog::info("[Bones]   {}", name);
        }
    }

    // Animation exporters disagree about namespace prefixes. Some produce
    // "Korlan:Hips", while others use "Hips" or "mixamorig:Hips". Build
    // aliases from all source nodes to the selected
    // character's root-bone prefix without renaming any assets. Non-animated
    // nodes still carry their animated parent's global pose.
    // A normalized fallback also handles separators such as "Head.001"
    // versus "Head_001" and "Breast.L" versus "Breast_L".
    std::unordered_map<std::string, std::string> sourceByBase;
    std::unordered_map<std::string, std::string> sourceByTargetName;
    std::unordered_map<std::string, std::string> sourceByNormalized;
    std::unordered_set<std::string> ambiguousBase;
    std::unordered_set<std::string> ambiguousTarget;
    std::unordered_set<std::string> ambiguousNormalized;
    const std::string& targetPrefix = targetModel.boneNamespacePrefix;
    for (const auto& [nodeName, matrix] : sourceGlobalPose) {
        (void)matrix;
        const std::string base = StripBoneNamespace(nodeName);
        const auto baseInsert = sourceByBase.emplace(base, nodeName);
        if (!baseInsert.second && baseInsert.first->second != nodeName) {
            ambiguousBase.insert(base);
        }
        const std::string normalized = NormalizeBoneName(nodeName);
        const auto normalizedInsert =
            sourceByNormalized.emplace(normalized, nodeName);
        if (!normalizedInsert.second &&
            normalizedInsert.first->second != nodeName) {
            ambiguousNormalized.insert(normalized);
        }
        if (!targetPrefix.empty()) {
            const std::string targetName = targetPrefix + base;
            const auto targetInsert =
                sourceByTargetName.emplace(targetName, nodeName);
            if (!targetInsert.second &&
                targetInsert.first->second != nodeName) {
                ambiguousTarget.insert(targetName);
            }
        }
    }
    for (const auto& base : ambiguousBase) sourceByBase.erase(base);
    for (const auto& targetName : ambiguousTarget) {
        sourceByTargetName.erase(targetName);
    }
    for (const auto& normalized : ambiguousNormalized) {
        sourceByNormalized.erase(normalized);
    }

    int matchedBones = 0;
    int aliasBones = 0;
    int retargetedBones = 0;
    std::unordered_set<std::string> matchedChannelNodes;
    std::unordered_map<std::string, glm::mat4> desiredGlobals;
    desiredGlobals.reserve(256);

    for (const auto& [boneName, targetIndex] : targetModel.boneMap) {
        if (targetIndex < 0 || targetIndex >= 255) continue;

        // Match every source node, not only animated channels. A source node
        // without a channel still inherits its animated parent's global pose,
        // which is essential for the 102 bones that K_Walking does not key.
        auto animated = sourceGlobalPose.find(boneName);
        if (animated == sourceGlobalPose.end() && !targetPrefix.empty()) {
            const auto targetAlias = sourceByTargetName.find(boneName);
            if (targetAlias != sourceByTargetName.end()) {
                animated = sourceGlobalPose.find(targetAlias->second);
                if (animated != sourceGlobalPose.end()) ++aliasBones;
            }
        }
        if (animated == sourceGlobalPose.end()) {
            const auto baseAlias = sourceByBase.find(
                StripBoneNamespace(boneName));
            if (baseAlias != sourceByBase.end()) {
                animated = sourceGlobalPose.find(baseAlias->second);
                if (animated != sourceGlobalPose.end()) ++aliasBones;
            }
        }
        if (animated == sourceGlobalPose.end()) {
            const auto normalizedAlias = sourceByNormalized.find(
                NormalizeBoneName(boneName));
            if (normalizedAlias != sourceByNormalized.end()) {
                animated = sourceGlobalPose.find(normalizedAlias->second);
                if (animated != sourceGlobalPose.end()) ++aliasBones;
            }
        }

        const auto offset = targetModel.boneOffsetMatrices.find(boneName);
        if (animated == sourceGlobalPose.end() ||
            offset == targetModel.boneOffsetMatrices.end()) {
            continue;
        }

        auto sourceRest = animationModel.nodeRestGlobals.find(animated->first);
        if (sourceRest == animationModel.nodeRestGlobals.end()) {
            sourceRest = animationModel.nodeRestGlobals.find(
                StripBoneNamespace(animated->first));
        }
        if (sourceRest == animationModel.nodeRestGlobals.end()) {
            continue;
        }
        ++matchedBones;
        if (sourceChannelNodes.count(animated->first)) {
            matchedChannelNodes.insert(animated->first);
        }

        // Global-space retarget: source global rest -> source global animated
        // delta, mapped as a whole armature pose into target space.
        glm::mat4 delta =
            glm::inverse(sourceRest->second) * animated->second;
        const float translationScale = unitBridge * animationScale;
        if (std::abs(translationScale - 1.0f) > 1e-6f) {
            ScaleMatrixTranslation(delta, translationScale);
        }
        const glm::mat4 sourceAnimatedGlobal = sourceRest->second * delta;
        desiredGlobals[boneName] =
            spaceCorrection * sourceAnimatedGlobal;
        ++retargetedBones;
    }

    // Rebuild target globals hierarchically. Matched bones use desired global
    // poses; unmatched children inherit their animated parent instead of being
    // frozen at bind-space identity. This is what prevents the stretched
    // triangles when a clip animates only part of the target skeleton.
    std::unordered_map<std::string, glm::mat4> targetGlobals;
    targetGlobals.reserve(targetModel.nodeRestGlobals.size());
    BuildRetargetedGlobals(targetModel.rawScene->mRootNode,
                           glm::mat4(1.0f), desiredGlobals, targetGlobals);

    int paletteBones = 0;
    for (const auto& [boneName, targetIndex] : targetModel.boneMap) {
        if (targetIndex < 0 || targetIndex >= 255) continue;
        const auto offset = targetModel.boneOffsetMatrices.find(boneName);
        if (offset == targetModel.boneOffsetMatrices.end()) continue;
        const glm::mat4* global = nullptr;
        const auto animatedGlobal = targetGlobals.find(boneName);
        if (animatedGlobal != targetGlobals.end()) {
            global = &animatedGlobal->second;
        } else {
            const auto restGlobal = targetModel.nodeRestGlobals.find(boneName);
            if (restGlobal != targetModel.nodeRestGlobals.end()) {
                global = &restGlobal->second;
            }
        }
        if (!global) continue;
        outBoneMatrices[static_cast<size_t>(targetIndex)] =
            *global * offset->second;
        ++paletteBones;
    }

    const int restFilledBones = std::max(0, paletteBones - retargetedBones);

    ScaleBonePalette(outBoneMatrices, targetModel.importScale);

    static std::unordered_set<const void*> loggedRetargets;
    if (loggedRetargets.insert(animationModel.rawScene.get()).second) {
        spdlog::info(
            "[Retarget] global-space root bone='{}' prefix='{}' "
            "armature='{}'<-'{}' target-bones={}/{} "
            "animated-channels={}/{} alias={} "
            "retargeted={} rest-filled={} palette={} "
            "scale={:.4f} import-scale={:.4f}/{:.4f}",
            targetModel.rootBoneName, targetModel.boneNamespacePrefix,
            targetModel.armatureRootName, animationModel.armatureRootName,
            matchedBones, targetModel.boneMap.size(),
            matchedChannelNodes.size(),
            animation ? animation->mNumChannels : 0u,
            aliasBones, retargetedBones, restFilledBones, paletteBones,
            animationScale, animationModel.importScale,
            targetModel.importScale);
    }
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
        vert.boneIDs = glm::vec4(255.0f, 0, 0, 0); // Cubes also use the identity dummy bone.
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

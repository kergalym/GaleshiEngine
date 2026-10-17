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
#include <filesystem>
#include <functional>

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
void AssetManager::Clear() { m_models.clear(); m_textures.clear(); }

std::shared_ptr<Texture> AssetManager::LoadTexture(const std::string& path) {
    auto it = m_textures.find(path);
    if (it != m_textures.end()) return it->second;
    Texture* raw = Texture::LoadFromFile(path);
    if (!raw) return m_white;
    std::shared_ptr<Texture> tex(raw);
    m_textures[path] = tex;
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

void ReadNodeHeirarchy(const aiScene* scene, float time, const aiNode* node, const glm::mat4& parentTransform, std::vector<glm::mat4>& boneMatrices, const std::unordered_map<std::string, int>& boneMap) {
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
        ReadNodeHeirarchy(scene, time, node->mChildren[i], globalTransform, boneMatrices, boneMap);
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
    const aiScene* scene = importer->ReadFile(path,
        aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenSmoothNormals);

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

    // === МАТЕРИАЛЫ ===
    out.materials.reserve(scene->mNumMaterials);
    for (unsigned i = 0; i < scene->mNumMaterials; ++i) {
        aiMaterial* aiMat = scene->mMaterials[i];
        auto mat = std::make_shared<Material>();

        aiString name;
        aiMat->Get(AI_MATKEY_NAME, name);
        mat->name = name.C_Str();

        spdlog::info("[AssetManager] Material[{}] '{}':", i, mat->name);

        // Цвет
        aiColor4D col;
        if (aiMat->Get(AI_MATKEY_BASE_COLOR, col) == AI_SUCCESS ||
            aiMat->Get(AI_MATKEY_COLOR_DIFFUSE, col) == AI_SUCCESS) {
            mat->albedoFactor = {col.r, col.g, col.b, col.a};
            spdlog::info("  base color: ({:.2f},{:.2f},{:.2f},{:.2f})",
                         col.r, col.g, col.b, col.a);
        }

        // Все texture slots — для диагностики
        bool albedoAssigned = false;
        for (unsigned t = 0; t < 32; ++t) {
            unsigned cnt = aiMat->GetTextureCount(static_cast<aiTextureType>(t));
            for (unsigned j = 0; j < cnt; ++j) {
                aiString p;
                if (aiMat->GetTexture(static_cast<aiTextureType>(t), j, &p) != AI_SUCCESS)
                    continue;
                std::string ps = p.C_Str();
                spdlog::info("  texture type[{}][{}] = '{}'",
                             t, j, ps);

                // Загружаем первый найденный texture как albedo
                if (!albedoAssigned &&
                    (t == aiTextureType_BASE_COLOR ||
                     t == aiTextureType_DIFFUSE ||
                     t == aiTextureType_UNKNOWN)) {
                    std::shared_ptr<Texture> tex;
                    if (!ps.empty() && ps[0] == '*') {
                        // EMBEDDED
                        try {
                            int idx = std::stoi(ps.substr(1));
                            if (idx >= 0 && idx < static_cast<int>(scene->mNumTextures)) {
                                const aiTexture* at = scene->mTextures[idx];
                                if (at->mHeight == 0) {
                                    // Compressed PNG/JPG
                                    tex = std::shared_ptr<Texture>(Texture::LoadFromMemory(
                                        reinterpret_cast<const unsigned char*>(at->pcData),
                                        static_cast<size_t>(at->mWidth)));
                                } else {
                                    // Raw RGBA (mWidth x mHeight)
                                    tex = std::shared_ptr<Texture>(Texture::LoadFromMemory(
                                        reinterpret_cast<const unsigned char*>(at->pcData),
                                        static_cast<size_t>(at->mWidth * at->mHeight * 4)));
                                }
                                if (tex) {
                                    spdlog::info("  → EMBEDDED #{}: OK", idx);
                                }
                            }
                        } catch (...) {
                            spdlog::warn("  → embedded parse error");
                        }
                    } else if (!ps.empty()) {
                        // EXTERNAL
                        std::filesystem::path pp(ps);
                        std::string full;
                        if (pp.is_absolute()) {
                            if (std::filesystem::exists(pp)) {
                                full = ps;
                            } else {
                                // Если абсолютный путь не найден, ищем файл рядом с FBX
                                std::string local = (std::filesystem::path(dir) / pp.filename()).string();
                                if (std::filesystem::exists(local)) {
                                    full = local;
                                    spdlog::info("  → Исправлен путь к текстуре: {}", full);
                                } else {
                                    full = ps; // оставляем как есть, не загрузится
                                }
                            }
                        } else {
                            full = (std::filesystem::path(dir) / pp).string();
                        }
                        tex = LoadTexture(full);
                        if (tex && tex != m_white) {
                            spdlog::info("  → EXTERNAL: OK ({})", full);
                        }
                    }

                    if (tex && tex != m_white) {
                        mat->albedoMap = tex;
                        mat->albedoFactor = glm::vec4(1, 1, 1, 1);
                        mat->sourcePath = ps;
                        albedoAssigned = true;
                    }
                }
            }
        }

        if (!albedoAssigned) {
            spdlog::warn("  ⚠ No albedo texture found for material '{}'", mat->name);
        }

        out.materials.push_back(mat);
    }

    // === МЕШИ ===
    for (unsigned i = 0; i < scene->mNumMeshes; ++i) {
        const aiMesh* am = scene->mMeshes[i];
        spdlog::info("[AssetManager] Mesh[{}]: {} verts, {} bones, material index {}",
                     i, am->mNumVertices, am->mNumBones, am->mMaterialIndex);

        std::vector<Vertex> verts;
        verts.reserve(am->mNumVertices);
        for (unsigned v = 0; v < am->mNumVertices; ++v) {
            Vertex vert{};
            vert.position = aiToVec3(am->mVertices[v]);
            if (am->HasNormals()) vert.normal = aiToVec3(am->mNormals[v]);
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
            if (v.boneWeights.x + v.boneWeights.y + v.boneWeights.z + v.boneWeights.w < 0.001f) {
                // Вершины без весов привязываем к dummy bone 255 (Identity)
                // Position уже в правильном месте (armature space)
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
        { 1,-1,-1,  1,0,0, 0,0,-1, 0,0}, { 1, 1,-1,  1,0,0, 0,0,-1, 0,1},
        { 1, 1, 1,  1,0,0, 0,0,-1, 1,1}, { 1,-1, 1,  1,0,0, 0,0,-1, 1,0},
        {-1,-1, 1, -1,0,0, 0,0,1, 0,0}, {-1, 1, 1, -1,0,0, 0,0,1, 0,1},
        {-1, 1,-1, -1,0,0, 0,0,1, 1,1}, {-1,-1,-1, -1,0,0, 0,0,1, 1,0},
        {-1, 1,-1,  0,1,0, 1,0,0, 0,0}, {-1, 1, 1,  0,1,0, 1,0,0, 0,1},
        { 1, 1, 1,  0,1,0, 1,0,0, 1,1}, { 1, 1,-1,  0,1,0, 1,0,0, 1,0},
        {-1,-1, 1,  0,-1,0,1,0,0, 0,0}, {-1,-1,-1,  0,-1,0,1,0,0, 0,1},
        { 1,-1,-1,  0,-1,0,1,0,0, 1,1}, { 1,-1, 1,  0,-1,0,1,0,0, 1,0},
        {-1,-1, 1,  0,0,1, 1,0,0, 0,0}, {-1, 1, 1,  0,0,1, 1,0,0, 0,1},
        { 1, 1, 1,  0,0,1, 1,0,0, 1,1}, { 1,-1, 1,  0,0,1, 1,0,0, 1,0},
        { 1,-1,-1,  0,0,-1,-1,0,0, 0,0}, { 1, 1,-1,  0,0,-1,-1,0,0, 0,1},
        {-1, 1,-1,  0,0,-1,-1,0,0, 1,1}, {-1,-1,-1,  0,0,-1,-1,0,0, 1,0},
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
} // namespace Galeshi

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

std::shared_ptr<Material> MakeMaterialFromAssimp(aiMaterial* mat, const std::string& dir) {
    auto m = std::make_shared<Material>();
    aiString name;
    if (mat->Get(AI_MATKEY_NAME, name) == AI_SUCCESS)
        m->name = name.C_Str();

    aiColor4D baseColor;
    if (mat->Get(AI_MATKEY_BASE_COLOR, baseColor) == AI_SUCCESS) {
        m->albedoFactor = {baseColor.r, baseColor.g, baseColor.b, baseColor.a};
    } else {
        aiColor4D diffuse;
        if (mat->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse) == AI_SUCCESS) {
            m->albedoFactor = {diffuse.r, diffuse.g, diffuse.b, diffuse.a};
        }
    }

    float metallic = 0.0f, roughness = 0.5f;
    mat->Get(AI_MATKEY_METALLIC_FACTOR, metallic);
    mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness);
    m->metallicFactor = metallic;
    m->roughnessFactor = roughness;

    aiString texPath;
    if (mat->GetTexture(aiTextureType_BASE_COLOR, 0, &texPath) == AI_SUCCESS ||
        mat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS) {
        std::string full = (std::filesystem::path(dir) / texPath.C_Str()).string();
        m->sourcePath = full;
    }
    return m;
}

// Рекурсивный обход дерева узлов для применения глобальных матриц
void ProcessNode(aiNode* node, const aiScene* scene,
                 const glm::mat4& parentTransform,
                 std::vector<std::shared_ptr<Mesh>>& outMeshes,
                 std::vector<int>& outMeshMatIdx, float modelScale)
{
    const glm::mat4 localTransform  = aiToGlm(node->mTransformation);
    const glm::mat4 globalTransform = parentTransform * localTransform;

    // Нормали нужно трансформировать обратно-транспонированной матрицей
    const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(globalTransform)));

    for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
        aiMesh* am = scene->mMeshes[node->mMeshes[i]];

        std::vector<Vertex> verts;
        verts.reserve(am->mNumVertices);
        for (unsigned int v = 0; v < am->mNumVertices; ++v) {
            Vertex vert{};

            glm::vec4 pos = globalTransform * glm::vec4(aiToVec3(am->mVertices[v]), 1.0f);
            vert.position = glm::vec3(pos);

            if (am->HasNormals()) {
                vert.normal = normalMatrix * aiToVec3(am->mNormals[v]);
            }
            if (am->HasTangentsAndBitangents()) {
                vert.tangent = normalMatrix * aiToVec3(am->mTangents[v]);
            }
            if (am->HasTextureCoords(0)) {
                vert.uv = aiToVec2(am->mTextureCoords[0][v]);
            }
            verts.push_back(vert);
        }

        std::vector<uint32_t> indices;
        indices.reserve(am->mNumFaces * 3);
        for (unsigned int f = 0; f < am->mNumFaces; ++f) {
            const aiFace& face = am->mFaces[f];
            for (unsigned int j = 0; j < face.mNumIndices; ++j)
                indices.push_back(face.mIndices[j]);
        }

        auto mesh = std::make_shared<Mesh>();
        mesh->Upload(verts, indices);
        outMeshes.push_back(mesh);
        outMeshMatIdx.push_back(static_cast<int>(am->mMaterialIndex));
    }

    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        ProcessNode(node->mChildren[i], scene, globalTransform, outMeshes, outMeshMatIdx, modelScale);
    }
}


} // namespace

AssetManager::LoadedModel AssetManager::LoadModel(const std::string& path) {
    auto it = m_models.find(path);
    if (it != m_models.end()) return it->second;

    LoadedModel out;

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path,
        aiProcess_Triangulate        |
        aiProcess_GenSmoothNormals   |
        aiProcess_CalcTangentSpace   |
        aiProcess_FlipUVs);

    if (!scene || !scene->mRootNode) {
        spdlog::error("[AssetManager] Assimp: {}", importer.GetErrorString());
        return out;
    }

    const std::string dir = std::filesystem::path(path).parent_path().string();

    out.materials.reserve(scene->mNumMaterials);
    for (unsigned i = 0; i < scene->mNumMaterials; ++i) {
        auto mat = MakeMaterialFromAssimp(scene->mMaterials[i], dir);
        if (!mat->sourcePath.empty()) {
            mat->albedoMap = LoadTexture(mat->sourcePath);
        }
        out.materials.push_back(mat);
    }

    // Извлекаем реальный масштаб из FBX (UnitScaleFactor)
    float unitScale = 1.0f;
    if (scene->mMetaData) {
        scene->mMetaData->Get("UnitScaleFactor", unitScale);
    }
    // Если FBX в сантиметрах (100), modelScale будет 0.01
    float modelScale = (unitScale > 0.0f) ? (1.0f / unitScale) : 1.0f;
    spdlog::info("[AssetManager] UnitScaleFactor: {}, modelScale: {}", unitScale, modelScale);

    // Обходим иерархию, запекая трансформации в вершины
    ProcessNode(scene->mRootNode, scene, glm::mat4(1.0f), out.meshes, out.meshMaterialIndex, modelScale);

    spdlog::info("[AssetManager] Loaded {} ({} mesh(es), {} material(s))",
                 path, out.meshes.size(), out.materials.size());

    m_models[path] = out;
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

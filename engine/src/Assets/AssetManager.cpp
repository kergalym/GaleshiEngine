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
#include <filesystem>
#include <vector>

namespace Galeshi {

AssetManager::AssetManager() {
    m_white      = std::shared_ptr<Texture>(Texture::CreateWhite());
    m_black      = std::shared_ptr<Texture>(Texture::CreateBlack());
    m_flatNormal = std::shared_ptr<Texture>(Texture::CreateFlatNormal());
}

AssetManager::~AssetManager() = default;

void AssetManager::Clear() {
    m_models.clear();
    m_textures.clear();
}

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

std::shared_ptr<Material> MakeMaterialFromAssimp(aiMaterial* mat, const std::string& dir) {
    auto m = std::make_shared<Material>();
    aiString name;
    if (mat->Get(AI_MATKEY_NAME, name) == AI_SUCCESS) m->name = name.C_Str();

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
    m->metallicFactor  = metallic;
    m->roughnessFactor = roughness;

    aiString texPath;
    if (mat->GetTexture(aiTextureType_BASE_COLOR, 0, &texPath) == AI_SUCCESS ||
        mat->GetTexture(aiTextureType_DIFFUSE,    0, &texPath) == AI_SUCCESS) {
        std::string full = (std::filesystem::path(dir) / texPath.C_Str()).string();
        m->sourcePath = full;
    }
    return m;
}

// Рекурсивный обход дерева узлов для применения глобальных матриц
void ProcessNode(aiNode* node, const aiScene* scene,
                 const glm::mat4& parentTransform,
                 std::vector<std::shared_ptr<Mesh>>& outMeshes,
                 std::vector<int>& outMeshMatIdx)
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
        ProcessNode(node->mChildren[i], scene, globalTransform, outMeshes, outMeshMatIdx);
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

    // Обходим иерархию, запекая трансформации в вершины
    ProcessNode(scene->mRootNode, scene, glm::mat4(1.0f), out.meshes, out.meshMaterialIndex);

    spdlog::info("[AssetManager] Loaded {} ({} mesh(es), {} material(s))",
                 path, out.meshes.size(), out.materials.size());

    m_models[path] = out;
    return out;
}

// ... (CreateCubeMesh остается без изменений) ...
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
    vs.reserve(24);
    for (auto& v : verts) {
        Vertex vert{};
        vert.position = {v.x, v.y, v.z};
        vert.normal   = {v.nx, v.ny, v.nz};
        vert.tangent  = {v.tx, v.ty, v.tz};
        vert.uv       = {v.u, v.v};
        vs.push_back(vert);
    }
    std::vector<uint32_t> is(idx, idx + 36);
    m_cubeMesh = std::make_shared<Mesh>();
    m_cubeMesh->Upload(vs, is);
    return m_cubeMesh;
}

} // namespace Galeshi

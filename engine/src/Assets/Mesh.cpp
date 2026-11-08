#include "GaleshiEngine/Assets/Mesh.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace Galeshi {

namespace {

bgfx::VertexLayout MakeVertexLayout() {
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position,  3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal,    3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Tangent,   3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Indices,   4, bgfx::AttribType::Float) // bone ids
        .add(bgfx::Attrib::Weight,    4, bgfx::AttribType::Float) // bone weights
        .end();
    return layout;
}

struct GridKey {
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;

    bool operator==(const GridKey& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct GridKeyHash {
    std::size_t operator()(const GridKey& key) const {
        std::size_t seed = std::hash<int32_t>{}(key.x);
        seed ^= std::hash<int32_t>{}(key.y) + 0x9e3779b9u + (seed << 6) + (seed >> 2);
        seed ^= std::hash<int32_t>{}(key.z) + 0x9e3779b9u + (seed << 6) + (seed >> 2);
        return seed;
    }
};

struct ClusterAccum {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f};
    glm::vec3 tangent{0.0f};
    glm::vec2 uv{0.0f};
    glm::vec4 boneIDs{0.0f};
    glm::vec4 boneWeights{1.0f, 0.0f, 0.0f, 0.0f};
    float representativeWeight = -1.0f;
    uint32_t samples = 0;
};

glm::vec3 SafeNormalize(const glm::vec3& value, const glm::vec3& fallback) {
    const float length = glm::length(value);
    return length > 1e-6f ? value / length : fallback;
}

} // namespace

Mesh::~Mesh() { Destroy(); }

void Mesh::DestroyLods() {
    for (LodLevel& lod : m_lods) {
        if (bgfx::isValid(lod.vb)) bgfx::destroy(lod.vb);
        if (bgfx::isValid(lod.ib)) bgfx::destroy(lod.ib);
    }
    m_lods.clear();
    m_lodsGenerated = false;
}

void Mesh::Destroy() {
    DestroyLods();
    if (bgfx::isValid(m_vb)) bgfx::destroy(m_vb);
    if (bgfx::isValid(m_ib)) bgfx::destroy(m_ib);
    if (bgfx::isValid(m_wireframeIB)) bgfx::destroy(m_wireframeIB);
    m_vb = BGFX_INVALID_HANDLE;
    m_ib = BGFX_INVALID_HANDLE;
    m_wireframeIB = BGFX_INVALID_HANDLE;
    m_indexCount = 0;
    m_wireframeIndexCount = 0;
    m_vertices.clear();
    m_indices.clear();
    m_lodsGenerated = false;
    m_lodRebuildAfter = {};
}

void Mesh::Upload(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    if (vertices.empty() || indices.empty()) return;

    // Re-uploading a mesh (for example after a landscape rebuild) must not
    // leak the previous GPU buffers or keep stale LOD geometry.
    Destroy();

    // The skinned vertex shader always evaluates a bone palette. Static
    // imported meshes have no joint influences, so give unweighted vertices
    // the identity dummy bone instead of collapsing them to zero. This also
    // keeps a static glTF correct after an AnimationStateMachineComponent adds
    // a SkeletalComponent to its root.
    std::vector<Vertex> weightedVertices;
    const std::vector<Vertex>* uploadVertices = &vertices;
    for (size_t i = 0; i < vertices.size(); ++i) {
        const Vertex& vertex = vertices[i];
        const float weight = vertex.boneWeights.x + vertex.boneWeights.y +
                             vertex.boneWeights.z + vertex.boneWeights.w;
        if (weight > 1e-6f) continue;
        if (weightedVertices.empty()) weightedVertices = vertices;
        weightedVertices[i].boneIDs = glm::vec4(255.0f, 0.0f, 0.0f, 0.0f);
        weightedVertices[i].boneWeights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        uploadVertices = &weightedVertices;
    }

    // Some glTF exporters omit TANGENT. Assimp normally generates it, but if
    // it is still missing the normal map is ignored by the shader. Build a
    // tangent basis from positions/UVs only for vertices that need it.
    std::vector<Vertex> tangentVertices;
    const bool needsTangents = std::any_of(
        uploadVertices->begin(), uploadVertices->end(),
        [](const Vertex& vertex) {
            return glm::dot(vertex.tangent, vertex.tangent) < 1e-8f;
        });
    if (needsTangents) {
        tangentVertices = *uploadVertices;
        std::vector<glm::vec3> tangentSum(tangentVertices.size(), glm::vec3(0.0f));
        std::vector<glm::vec3> bitangentSum(tangentVertices.size(), glm::vec3(0.0f));
        for (size_t i = 0; i + 2 < indices.size(); i += 3) {
            const uint32_t i0 = indices[i];
            const uint32_t i1 = indices[i + 1];
            const uint32_t i2 = indices[i + 2];
            if (i0 >= tangentVertices.size() || i1 >= tangentVertices.size() ||
                i2 >= tangentVertices.size()) {
                continue;
            }
            const Vertex& v0 = tangentVertices[i0];
            const Vertex& v1 = tangentVertices[i1];
            const Vertex& v2 = tangentVertices[i2];
            const glm::vec3 e1 = v1.position - v0.position;
            const glm::vec3 e2 = v2.position - v0.position;
            const glm::vec2 d1 = v1.uv - v0.uv;
            const glm::vec2 d2 = v2.uv - v0.uv;
            const float denominator = d1.x * d2.y - d1.y * d2.x;
            if (std::abs(denominator) < 1e-8f) continue;
            const glm::vec3 tangent =
                (e1 * d2.y - e2 * d1.y) / denominator;
            const glm::vec3 bitangent =
                (e2 * d1.x - e1 * d2.x) / denominator;
            for (uint32_t index : {i0, i1, i2}) {
                tangentSum[index] += tangent;
                bitangentSum[index] += bitangent;
            }
        }
        for (size_t i = 0; i < tangentVertices.size(); ++i) {
            if (glm::dot(tangentVertices[i].tangent,
                         tangentVertices[i].tangent) >= 1e-8f) {
                continue;
            }
            glm::vec3 normal = tangentVertices[i].normal;
            if (glm::dot(normal, normal) < 1e-8f) normal = glm::vec3(0, 1, 0);
            normal = glm::normalize(normal);
            glm::vec3 tangent = tangentSum[i] -
                normal * glm::dot(normal, tangentSum[i]);
            if (glm::dot(tangent, tangent) < 1e-8f) {
                tangent = std::abs(normal.y) < 0.99f
                    ? glm::cross(normal, glm::vec3(0, 1, 0))
                    : glm::cross(normal, glm::vec3(1, 0, 0));
            }
            tangent = glm::normalize(tangent);
            if (glm::dot(glm::cross(normal, tangent), bitangentSum[i]) < 0.0f) {
                tangent = -tangent;
            }
            tangentVertices[i].tangent = tangent;
        }
        uploadVertices = &tangentVertices;
    }

    const bgfx::VertexLayout layout = MakeVertexLayout();
    m_vb = bgfx::createDynamicVertexBuffer(
        bgfx::copy(uploadVertices->data(),
                   static_cast<uint32_t>(uploadVertices->size() * sizeof(Vertex))),
        layout);
    m_ib = bgfx::createIndexBuffer(
        bgfx::copy(indices.data(),
                   static_cast<uint32_t>(indices.size() * sizeof(uint32_t))),
        BGFX_BUFFER_INDEX32);
    m_indexCount = static_cast<uint32_t>(indices.size());
    m_vertices   = *uploadVertices;
    m_indices    = indices;
}

bgfx::IndexBufferHandle Mesh::WireframeIB() {
    if (bgfx::isValid(m_wireframeIB)) return m_wireframeIB;
    if (m_indices.size() < 3) return BGFX_INVALID_HANDLE;

    std::unordered_set<uint64_t> edges;
    std::vector<uint32_t> lines;
    edges.reserve(m_indices.size());
    lines.reserve(m_indices.size() * 2);

    auto addEdge = [&](uint32_t a, uint32_t b) {
        if (a == b) return;
        const uint32_t first = std::min(a, b);
        const uint32_t second = std::max(a, b);
        const uint64_t key =
            (static_cast<uint64_t>(first) << 32) | second;
        if (!edges.insert(key).second) return;
        lines.push_back(a);
        lines.push_back(b);
    };

    for (size_t i = 0; i + 2 < m_indices.size(); i += 3) {
        const uint32_t a = m_indices[i];
        const uint32_t b = m_indices[i + 1];
        const uint32_t c = m_indices[i + 2];
        addEdge(a, b);
        addEdge(b, c);
        addEdge(c, a);
    }

    if (lines.empty()) return BGFX_INVALID_HANDLE;
    m_wireframeIB = bgfx::createIndexBuffer(
        bgfx::copy(lines.data(),
                   static_cast<uint32_t>(lines.size() * sizeof(uint32_t))),
        BGFX_BUFFER_INDEX32);
    m_wireframeIndexCount = static_cast<uint32_t>(lines.size());
    return m_wireframeIB;
}

void Mesh::BuildLods() {
    if (m_vertices.empty() || m_indices.size() < 3 || !Valid()) return;

    glm::vec3 boundsMin, boundsMax;
    LocalAABB(boundsMin, boundsMax);
    const glm::vec3 extent = boundsMax - boundsMin;
    const float maxExtent = std::max({extent.x, extent.y, extent.z});
    if (maxExtent <= 1e-6f) return;

    // Fixed resolutions along the longest AABB axis. Starting coarse enough
    // keeps the generated LODs useful for distant views without exploding
    // the memory budget with near-duplicate levels.
    static constexpr uint32_t kResolutions[] = {64, 32, 16, 12, 8};
    std::size_t previousIndexCount = m_indices.size();

    for (uint32_t resolution : kResolutions) {
        if (m_lods.size() >= 3) break;

        const float cellSize = maxExtent / static_cast<float>(resolution);
        std::unordered_map<GridKey, uint32_t, GridKeyHash> clusterLookup;
        clusterLookup.reserve(m_vertices.size());

        std::vector<uint32_t> remap(m_vertices.size(), 0);
        std::vector<ClusterAccum> clusters;
        clusters.reserve(m_vertices.size() / 4 + 1);

        for (std::size_t i = 0; i < m_vertices.size(); ++i) {
            const Vertex& vertex = m_vertices[i];
            const glm::vec3 local = (vertex.position - boundsMin) / cellSize;
            const GridKey key{
                static_cast<int32_t>(std::floor(local.x)),
                static_cast<int32_t>(std::floor(local.y)),
                static_cast<int32_t>(std::floor(local.z))
            };

            const auto found = clusterLookup.find(key);
            uint32_t clusterIndex = 0;
            if (found == clusterLookup.end()) {
                clusterIndex = static_cast<uint32_t>(clusters.size());
                clusterLookup.emplace(key, clusterIndex);
                clusters.emplace_back();
            } else {
                clusterIndex = found->second;
            }

            ClusterAccum& cluster = clusters[clusterIndex];
            cluster.position += vertex.position;
            cluster.normal += vertex.normal;
            cluster.tangent += vertex.tangent;
            cluster.uv += vertex.uv;
            cluster.samples += 1;

            const float weight = vertex.boneWeights.x + vertex.boneWeights.y +
                                 vertex.boneWeights.z + vertex.boneWeights.w;
            if (weight > cluster.representativeWeight) {
                cluster.representativeWeight = weight;
                cluster.boneIDs = vertex.boneIDs;
                cluster.boneWeights = vertex.boneWeights;
            }

            remap[i] = clusterIndex;
        }

        if (clusters.size() < 3) continue;
        if (clusters.size() >= m_vertices.size()) continue;

        std::vector<Vertex> lodVertices;
        lodVertices.reserve(clusters.size());
        for (const ClusterAccum& cluster : clusters) {
            const float inv = 1.0f / static_cast<float>(std::max(cluster.samples, 1u));
            Vertex vertex{};
            vertex.position = cluster.position * inv;
            vertex.normal = SafeNormalize(cluster.normal, glm::vec3(0.0f, 1.0f, 0.0f));
            vertex.tangent = SafeNormalize(cluster.tangent, glm::vec3(1.0f, 0.0f, 0.0f));
            vertex.uv = cluster.uv * inv;
            vertex.boneIDs = cluster.boneIDs;
            vertex.boneWeights = cluster.boneWeights;
            lodVertices.push_back(vertex);
        }

        std::vector<uint32_t> lodIndices;
        lodIndices.reserve(m_indices.size());
        for (std::size_t i = 0; i + 2 < m_indices.size(); i += 3) {
            const uint32_t a = remap[m_indices[i]];
            const uint32_t b = remap[m_indices[i + 1]];
            const uint32_t c = remap[m_indices[i + 2]];
            if (a == b || b == c || a == c) continue;
            lodIndices.push_back(a);
            lodIndices.push_back(b);
            lodIndices.push_back(c);
        }

        if (lodIndices.size() < 3) continue;
        // Do not waste a level on a negligible reduction; try a coarser grid.
        if (static_cast<double>(lodIndices.size()) >=
            static_cast<double>(previousIndexCount) * 0.85) {
            continue;
        }

        LodLevel lod;
        lod.vb = bgfx::createDynamicVertexBuffer(
            bgfx::copy(lodVertices.data(),
                       static_cast<uint32_t>(lodVertices.size() * sizeof(Vertex))),
            MakeVertexLayout());
        lod.ib = bgfx::createIndexBuffer(
            bgfx::copy(lodIndices.data(),
                       static_cast<uint32_t>(lodIndices.size() * sizeof(uint32_t))),
            BGFX_BUFFER_INDEX32);
        if (!bgfx::isValid(lod.vb) || !bgfx::isValid(lod.ib)) {
            if (bgfx::isValid(lod.vb)) bgfx::destroy(lod.vb);
            if (bgfx::isValid(lod.ib)) bgfx::destroy(lod.ib);
            continue;
        }

        lod.indexCount = static_cast<uint32_t>(lodIndices.size());
        m_lods.push_back(lod);
        previousIndexCount = lodIndices.size();
    }
}

void Mesh::EnsureLods() {
    if (m_lodsGenerated) return;
    if (std::chrono::steady_clock::now() < m_lodRebuildAfter) return;
    m_lodsGenerated = true;

    // Small meshes are not worth the extra buffers. Skinned meshes are kept
    // on the full-detail path by the renderer and never request LODs.
    if (m_indices.size() < 6000) return;
    BuildLods();
}

bool Mesh::LodValid(uint32_t lod) const {
    if (lod == 0) return Valid();
    if (lod > m_lods.size()) return false;
    const LodLevel& level = m_lods[lod - 1];
    return bgfx::isValid(level.vb) && bgfx::isValid(level.ib);
}

bgfx::DynamicVertexBufferHandle Mesh::LodVB(uint32_t lod) const {
    if (lod == 0 || lod > m_lods.size()) return m_vb;
    return m_lods[lod - 1].vb;
}

bgfx::IndexBufferHandle Mesh::LodIB(uint32_t lod) const {
    if (lod == 0 || lod > m_lods.size()) return m_ib;
    return m_lods[lod - 1].ib;
}

uint32_t Mesh::LodIndexCount(uint32_t lod) const {
    if (lod == 0 || lod > m_lods.size()) return m_indexCount;
    return m_lods[lod - 1].indexCount;
}

void Mesh::LocalAABB(glm::vec3& outMin, glm::vec3& outMax) const {
    if (m_vertices.empty()) { outMin = outMax = glm::vec3(0.0f); return; }
    constexpr float INF = std::numeric_limits<float>::max();
    outMin = glm::vec3( INF);
    outMax = glm::vec3(-INF);
    for (const auto& v : m_vertices) {
        outMin = glm::min(outMin, v.position);
        outMax = glm::max(outMax, v.position);
    }
}

void Mesh::UpdateVertexData(const std::vector<Vertex>& vertices) {
    if (vertices.size() != m_vertices.size()) return;
    m_vertices = vertices;
    DestroyLods();
    // Sculpting/cloth updates can happen every frame. Defer regeneration so
    // LODs are rebuilt once the geometry settles instead of every update.
    m_lodRebuildAfter = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(150);
    if (bgfx::isValid(m_vb)) {
        bgfx::update(m_vb, 0,
                     bgfx::copy(m_vertices.data(),
                                static_cast<uint32_t>(m_vertices.size() * sizeof(Vertex))));
    }
}

void Mesh::UpdateVertices(const std::vector<glm::vec3>& newPositions) {
    if (newPositions.size() != m_vertices.size()) return;
    for (size_t i = 0; i < m_vertices.size(); ++i) {
        m_vertices[i].position = newPositions[i];
    }
    for (auto& v : m_vertices) v.normal = glm::vec3(0.0f);
    for (size_t i = 0; i < m_indices.size(); i += 3) {
        uint32_t i0 = m_indices[i], i1 = m_indices[i+1], i2 = m_indices[i+2];
        glm::vec3 v0 = m_vertices[i0].position, v1 = m_vertices[i1].position, v2 = m_vertices[i2].position;
        glm::vec3 n = glm::cross(v1 - v0, v2 - v0);
        m_vertices[i0].normal += n; m_vertices[i1].normal += n; m_vertices[i2].normal += n;
    }
    for (auto& v : m_vertices) {
        const float len = glm::length(v.normal);
        v.normal = len > 1e-8f ? v.normal / len : glm::vec3(0.0f, 1.0f, 0.0f);
    }
    DestroyLods();
    m_lodRebuildAfter = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(150);
    if (bgfx::isValid(m_vb)) {
        bgfx::update(
            m_vb, 0,
            bgfx::copy(m_vertices.data(),
                       static_cast<uint32_t>(m_vertices.size() * sizeof(Vertex))));
    }
}

} // namespace Galeshi

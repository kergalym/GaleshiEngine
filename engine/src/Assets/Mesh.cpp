#include "GaleshiEngine/Assets/Mesh.hpp"
#include <limits>

namespace Galeshi {

Mesh::~Mesh() { Destroy(); }

void Mesh::Destroy() {
    if (bgfx::isValid(m_vb)) bgfx::destroy(m_vb);
    if (bgfx::isValid(m_ib)) bgfx::destroy(m_ib);
    m_vb = BGFX_INVALID_HANDLE;
    m_ib = BGFX_INVALID_HANDLE;
    m_indexCount = 0;
    m_vertices.clear();
    m_indices.clear();
}

void Mesh::Upload(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    if (vertices.empty() || indices.empty()) return;

    // Re-uploading a mesh (for example after a landscape rebuild) must not
    // leak the previous GPU buffers.
    Destroy();

    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position,  3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal,    3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Tangent,   3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Indices,   4, bgfx::AttribType::Float) // ID костей
        .add(bgfx::Attrib::Weight,    4, bgfx::AttribType::Float) // Веса
        .end();

    m_vb = bgfx::createDynamicVertexBuffer(bgfx::copy(vertices.data(), uint32_t(vertices.size() * sizeof(Vertex))), layout);
    m_ib = bgfx::createIndexBuffer(bgfx::copy(indices.data(), uint32_t(indices.size() * sizeof(uint32_t))), BGFX_BUFFER_INDEX32);
    m_indexCount = uint32_t(indices.size());
    m_vertices   = vertices;
    m_indices    = indices;
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
    if (bgfx::isValid(m_vb)) {
        bgfx::update(m_vb, 0, bgfx::copy(m_vertices.data(), m_vertices.size() * sizeof(Vertex)));
    }
}

} // namespace Galeshi

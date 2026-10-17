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

void Mesh::Upload(const std::vector<Vertex>& vertices,
                  const std::vector<uint32_t>& indices)
{
    if (vertices.empty() || indices.empty()) return;

    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position,  3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal,    3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Tangent,   3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();

    m_vb = bgfx::createVertexBuffer(
        bgfx::copy(vertices.data(), static_cast<uint32_t>(vertices.size() * sizeof(Vertex))),
        layout);

    m_ib = bgfx::createIndexBuffer(
        bgfx::copy(indices.data(), static_cast<uint32_t>(indices.size() * sizeof(uint32_t))),
        BGFX_BUFFER_INDEX32);

    m_indexCount = static_cast<uint32_t>(indices.size());
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

} // namespace Galeshi

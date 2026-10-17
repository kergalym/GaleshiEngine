#include "GaleshiEngine/Assets/Mesh.hpp"

namespace Galeshi {

Mesh::~Mesh() { Destroy(); }

void Mesh::Destroy() {
    if (bgfx::isValid(m_vb)) bgfx::destroy(m_vb);
    if (bgfx::isValid(m_ib)) bgfx::destroy(m_ib);
    m_vb = BGFX_INVALID_HANDLE;
    m_ib = BGFX_INVALID_HANDLE;
    m_indexCount = 0;
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
        bgfx::copy(vertices.data(),
                   static_cast<uint32_t>(vertices.size() * sizeof(Vertex))),
        layout);

    m_ib = bgfx::createIndexBuffer(
        bgfx::copy(indices.data(),
                   static_cast<uint32_t>(indices.size() * sizeof(uint32_t))),
        BGFX_BUFFER_INDEX32);

    m_indexCount = static_cast<uint32_t>(indices.size());
}

} // namespace Galeshi

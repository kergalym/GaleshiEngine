#pragma once

#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace Galeshi {

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 tangent;
    glm::vec2 uv;
};

class Mesh {
public:
    Mesh() = default;
    ~Mesh();

    Mesh(const Mesh&)            = delete;
    Mesh& operator=(const Mesh&) = delete;

    // Создаёт статичный меш из CPU-данных
    void Upload(const std::vector<Vertex>& vertices,
                const std::vector<uint32_t>& indices);

    void Destroy();

    bgfx::VertexBufferHandle VB() const { return m_vb; }
    bgfx::IndexBufferHandle  IB() const { return m_ib; }
    uint32_t IndexCount() const { return m_indexCount; }
    bool     Valid() const { return bgfx::isValid(m_vb) && bgfx::isValid(m_ib); }

private:
    bgfx::VertexBufferHandle m_vb = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle  m_ib = BGFX_INVALID_HANDLE;
    uint32_t m_indexCount = 0;
};

} // namespace Galeshi

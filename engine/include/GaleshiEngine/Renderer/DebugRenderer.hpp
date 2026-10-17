#pragma once

#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace Galeshi {

class Camera;

class DebugRenderer {
public:
    DebugRenderer();
    ~DebugRenderer();

    void Init();
    void Shutdown();

    void Begin();
    void AddLine(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color);
    void AddTriangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                     const glm::vec4& color);
    void AddBox(const glm::vec3& center, const glm::vec3& size, const glm::vec4& color);
    void AddGrid(float halfSize, float step, const glm::vec4& color,
                 const glm::vec3& origin = glm::vec3(0.0f));
    void AddAxes(const glm::vec3& origin, float length = 1.0f);

    void Render(const Camera& camera, bgfx::ViewId viewId,
                uint16_t width, uint16_t height);

private:
    struct Vertex {
        glm::vec4 position;
        uint32_t  color;
    };

    void FlushLines(bgfx::ViewId viewId);
    void FlushTris(bgfx::ViewId viewId);

    bgfx::ProgramHandle m_lineProgram = BGFX_INVALID_HANDLE;

    bgfx::VertexLayout  m_layout;

    // Двойная буферизация (чтобы не пересоздавать буфер пока render thread
    // читает предыдущий)
    bgfx::DynamicVertexBufferHandle m_lineVB[2] = { BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE };
    bgfx::IndexBufferHandle         m_lineIB[2] = { BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE };
    uint32_t m_lineVBCapacity[2] = { 0, 0 };

    bgfx::DynamicVertexBufferHandle m_triVB[2] = { BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE };
    bgfx::IndexBufferHandle         m_triIB[2] = { BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE };
    uint32_t m_triVBCapacity[2] = { 0, 0 };

    uint32_t m_frameIdx = 0;

    std::vector<Vertex> m_lines;
    std::vector<Vertex> m_tris;
};

} // namespace Galeshi

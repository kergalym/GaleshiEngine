#include "GaleshiEngine/Renderer/DebugRenderer.hpp"
#include "GaleshiEngine/Renderer/ShaderManager.hpp"
#include "GaleshiEngine/Renderer/Camera.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <spdlog/spdlog.h>
#include <cstring>

namespace Galeshi {

namespace {
uint32_t PackColor(const glm::vec4& c) {
    uint32_t r = static_cast<uint32_t>(c.r * 255.0f) & 0xFF;
    uint32_t g = static_cast<uint32_t>(c.g * 255.0f) & 0xFF;
    uint32_t b = static_cast<uint32_t>(c.b * 255.0f) & 0xFF;
    uint32_t a = static_cast<uint32_t>(c.a * 255.0f) & 0xFF;
    return r | (g << 8) | (b << 16) | (a << 24);
}
}

DebugRenderer::DebugRenderer() = default;
DebugRenderer::~DebugRenderer() { Shutdown(); }

void DebugRenderer::Init() {
    m_layout
        .begin()
        .add(bgfx::Attrib::TexCoord7, 4, bgfx::AttribType::Float)  // position (vec4)
        .add(bgfx::Attrib::Color0,    4, bgfx::AttribType::Uint8, true)
        .end();

    m_lineProgram = ShaderManager::LoadProgram("shaders/vs_line.bin", "shaders/fs_line.bin");
    if (!bgfx::isValid(m_lineProgram)) {
        spdlog::error("[DebugRenderer] Failed to load line shaders");
    }
}

void DebugRenderer::Shutdown() {
    for (int i = 0; i < 2; ++i) {
        if (bgfx::isValid(m_lineVB[i])) { bgfx::destroy(m_lineVB[i]); m_lineVB[i] = BGFX_INVALID_HANDLE; }
        if (bgfx::isValid(m_lineIB[i])) { bgfx::destroy(m_lineIB[i]); m_lineIB[i] = BGFX_INVALID_HANDLE; }
        if (bgfx::isValid(m_triVB[i]))  { bgfx::destroy(m_triVB[i]);  m_triVB[i]  = BGFX_INVALID_HANDLE; }
        if (bgfx::isValid(m_triIB[i]))  { bgfx::destroy(m_triIB[i]);  m_triIB[i]  = BGFX_INVALID_HANDLE; }
        m_lineVBCapacity[i] = 0;
        m_triVBCapacity[i]  = 0;
    }
    if (bgfx::isValid(m_lineProgram)) {
        bgfx::destroy(m_lineProgram);
        m_lineProgram = BGFX_INVALID_HANDLE;
    }
}

void DebugRenderer::Begin() {
    m_lines.clear();
    m_tris.clear();
}

void DebugRenderer::AddLine(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color) {
    const uint32_t c = PackColor(color);
    m_lines.push_back({glm::vec4(a, 1.0f), c});
    m_lines.push_back({glm::vec4(b, 1.0f), c});
}

void DebugRenderer::AddTriangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                                 const glm::vec4& color) {
    const uint32_t col = PackColor(color);
    m_tris.push_back({glm::vec4(a, 1.0f), col});
    m_tris.push_back({glm::vec4(b, 1.0f), col});
    m_tris.push_back({glm::vec4(c, 1.0f), col});
}

void DebugRenderer::AddBox(const glm::vec3& center, const glm::vec3& size, const glm::vec4& color) {
    const glm::vec3 h = size * 0.5f;
    const glm::vec3& c = center;
    glm::vec3 p[8] = {
        {c.x - h.x, c.y - h.y, c.z - h.z}, {c.x + h.x, c.y - h.y, c.z - h.z},
        {c.x + h.x, c.y - h.y, c.z + h.z}, {c.x - h.x, c.y - h.y, c.z + h.z},
        {c.x - h.x, c.y + h.y, c.z - h.z}, {c.x + h.x, c.y + h.y, c.z - h.z},
        {c.x + h.x, c.y + h.y, c.z + h.z}, {c.x - h.x, c.y + h.y, c.z + h.z},
    };
    AddLine(p[0], p[1], color); AddLine(p[1], p[2], color);
    AddLine(p[2], p[3], color); AddLine(p[3], p[0], color);
    AddLine(p[4], p[5], color); AddLine(p[5], p[6], color);
    AddLine(p[6], p[7], color); AddLine(p[7], p[4], color);
    AddLine(p[0], p[4], color); AddLine(p[1], p[5], color);
    AddLine(p[2], p[6], color); AddLine(p[3], p[7], color);
}

void DebugRenderer::AddGrid(float halfSize, float step, const glm::vec4& color,
                             const glm::vec3& origin) {
    for (float i = -halfSize; i <= halfSize + 0.001f; i += step) {
        AddLine({origin.x - halfSize, origin.y, origin.z + i},
                {origin.x + halfSize, origin.y, origin.z + i}, color);
        AddLine({origin.x + i, origin.y, origin.z - halfSize},
                {origin.x + i, origin.y, origin.z + halfSize}, color);
    }
}

void DebugRenderer::AddAxes(const glm::vec3& origin, float length) {
    AddLine(origin, origin + glm::vec3(length, 0, 0), {1, 0, 0, 1});
    AddLine(origin, origin + glm::vec3(0, length, 0), {0, 1, 0, 1});
    AddLine(origin, origin + glm::vec3(0, 0, length), {0, 0, 1, 1});
}

void DebugRenderer::FlushLines(bgfx::ViewId viewId) {
    if (m_lines.empty()) return;

    const uint32_t numVerts = static_cast<uint32_t>(m_lines.size());

    // Двойная буферизация: работаем с текущим слотом
    m_frameIdx = (m_frameIdx + 1) % 2;

    if (m_lineVBCapacity[m_frameIdx] < numVerts) {
        // Уничтожаем этот слот (его использовали 2 кадра назад — render thread
        // уже с ним закончил)
        if (bgfx::isValid(m_lineVB[m_frameIdx])) { bgfx::destroy(m_lineVB[m_frameIdx]); m_lineVB[m_frameIdx] = BGFX_INVALID_HANDLE; }
        if (bgfx::isValid(m_lineIB[m_frameIdx])) { bgfx::destroy(m_lineIB[m_frameIdx]); m_lineIB[m_frameIdx] = BGFX_INVALID_HANDLE; }

        m_lineVB[m_frameIdx] = bgfx::createDynamicVertexBuffer(
            numVerts, m_layout, BGFX_BUFFER_ALLOW_RESIZE);

        std::vector<uint16_t> indices(numVerts);
        for (uint32_t i = 0; i < numVerts; ++i) indices[i] = static_cast<uint16_t>(i);
        m_lineIB[m_frameIdx] = bgfx::createIndexBuffer(
            bgfx::copy(indices.data(), numVerts * sizeof(uint16_t)));

        m_lineVBCapacity[m_frameIdx] = numVerts;
    }

    bgfx::update(m_lineVB[m_frameIdx], 0,
        bgfx::copy(m_lines.data(), numVerts * sizeof(Vertex)));

    bgfx::setVertexBuffer(0, m_lineVB[m_frameIdx]);
    bgfx::setIndexBuffer(m_lineIB[m_frameIdx]);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                   BGFX_STATE_DEPTH_TEST_LEQUAL | BGFX_STATE_PT_LINES);
    bgfx::submit(viewId, m_lineProgram);  // ← viewId, не 0
}

void DebugRenderer::FlushTris(bgfx::ViewId viewId) {
    if (m_tris.empty()) return;

    const uint32_t numVerts = static_cast<uint32_t>(m_tris.size());

    // Обратите внимание: используем тот же m_frameIdx, что и FlushLines
    // (они оба синхронно меняют слот, так как вызываются в одном кадре)
    if (m_triVBCapacity[m_frameIdx] < numVerts) {
        if (bgfx::isValid(m_triVB[m_frameIdx])) { bgfx::destroy(m_triVB[m_frameIdx]); m_triVB[m_frameIdx] = BGFX_INVALID_HANDLE; }
        if (bgfx::isValid(m_triIB[m_frameIdx])) { bgfx::destroy(m_triIB[m_frameIdx]); m_triIB[m_frameIdx] = BGFX_INVALID_HANDLE; }

        m_triVB[m_frameIdx] = bgfx::createDynamicVertexBuffer(
            numVerts, m_layout, BGFX_BUFFER_ALLOW_RESIZE);

        std::vector<uint16_t> indices(numVerts);
        for (uint32_t i = 0; i < numVerts; ++i) indices[i] = static_cast<uint16_t>(i);
        m_triIB[m_frameIdx] = bgfx::createIndexBuffer(
            bgfx::copy(indices.data(), numVerts * sizeof(uint16_t)));

        m_triVBCapacity[m_frameIdx] = numVerts;
    }

    bgfx::update(m_triVB[m_frameIdx], 0,
        bgfx::copy(m_tris.data(), numVerts * sizeof(Vertex)));

    bgfx::setVertexBuffer(0, m_triVB[m_frameIdx]);
    bgfx::setIndexBuffer(m_triIB[m_frameIdx]);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                   BGFX_STATE_DEPTH_TEST_LEQUAL |
                   BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                                          BGFX_STATE_BLEND_INV_SRC_ALPHA));
    bgfx::submit(viewId, m_lineProgram);
}

void DebugRenderer::Render(const Camera& camera, bgfx::ViewId viewId,
                            uint16_t width, uint16_t height) {
    if (!bgfx::isValid(m_lineProgram)) return;

    bgfx::setViewRect(viewId, 0, 0, width, height);
    bgfx::setViewTransform(viewId, glm::value_ptr(camera.View()),
                                    glm::value_ptr(camera.Projection()));

    FlushTris(viewId);
    FlushLines(viewId);
}

} // namespace Galeshi

#include "GaleshiEngine/Renderer/ImGuiBgfx.hpp"
#include "GaleshiEngine/Renderer/ShaderManager.hpp"

#include <imgui.h>
#include <bx/math.h>
#include <spdlog/spdlog.h>
#include <cstring>

namespace Galeshi {

namespace {
bgfx::VertexLayout             g_layout;
bgfx::ProgramHandle            g_program    = BGFX_INVALID_HANDLE;
bgfx::UniformHandle            g_texUniform = BGFX_INVALID_HANDLE;
bgfx::TextureHandle            g_fontTexture = BGFX_INVALID_HANDLE;

bgfx::DynamicVertexBufferHandle g_vb[2] = { bgfx::kInvalidHandle, bgfx::kInvalidHandle };
bgfx::DynamicIndexBufferHandle  g_ib[2] = { bgfx::kInvalidHandle, bgfx::kInvalidHandle };
uint32_t g_vbCapacity[2] = { 0, 0 };
uint32_t g_ibCapacity[2] = { 0, 0 };
uint32_t g_frameIdx   = 0;
} // namespace

void ImGuiBgfx::Init() {
    ImGuiIO& io = ImGui::GetIO();

    // ВАЖНО: НЕ включаем RendererHasTextures, используем старый API
    // io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

    g_layout
        .begin()
        .add(bgfx::Attrib::Position,  2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0,    4, bgfx::AttribType::Uint8, true)
        .end();

    g_texUniform = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);

    // Загружаем шейдеры
    g_program = ShaderManager::LoadProgram("shaders/vs_imgui.bin", "shaders/fs_imgui.bin");
    if (!bgfx::isValid(g_program)) {
        spdlog::error("[ImGuiBgfx] Failed to load imgui shaders.");
    }

    // --- СТАРЫЙ API: вручную создаём текстуру шрифта ---
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    g_fontTexture = bgfx::createTexture2D(
        static_cast<uint16_t>(width),
        static_cast<uint16_t>(height),
        false, 1, bgfx::TextureFormat::RGBA8, 
        BGFX_TEXTURE_NONE, // Линейная фильтрация
        bgfx::copy(pixels, static_cast<uint32_t>(width * height * 4))
    );

    io.Fonts->SetTexID(static_cast<ImTextureID>(g_fontTexture.idx));
    
    spdlog::info("[ImGuiBgfx] Font texture created: {}x{}", width, height);
}

void ImGuiBgfx::Shutdown() {
    for (int i = 0; i < 2; ++i) {
        if (bgfx::isValid(g_vb[i])) bgfx::destroy(g_vb[i]);
        if (bgfx::isValid(g_ib[i])) bgfx::destroy(g_ib[i]);
        g_vbCapacity[i] = 0;
        g_ibCapacity[i] = 0;
    }
    if (bgfx::isValid(g_fontTexture)) bgfx::destroy(g_fontTexture);
    if (bgfx::isValid(g_texUniform))  bgfx::destroy(g_texUniform);
    if (bgfx::isValid(g_program))     bgfx::destroy(g_program);
}

namespace {
void EnsureCapacity(uint32_t numVerts, uint32_t numIdx) {
    if (g_vbCapacity[g_frameIdx] < numVerts) {
        if (bgfx::isValid(g_vb[g_frameIdx])) bgfx::destroy(g_vb[g_frameIdx]);
        g_vb[g_frameIdx] = bgfx::createDynamicVertexBuffer(
            numVerts, g_layout, BGFX_BUFFER_ALLOW_RESIZE);
        g_vbCapacity[g_frameIdx] = numVerts;
    }
    if (g_ibCapacity[g_frameIdx] < numIdx) {
        if (bgfx::isValid(g_ib[g_frameIdx])) bgfx::destroy(g_ib[g_frameIdx]);
        g_ib[g_frameIdx] = bgfx::createDynamicIndexBuffer(
            numIdx, BGFX_BUFFER_ALLOW_RESIZE);
        g_ibCapacity[g_frameIdx] = numIdx;
    }
}
} // namespace

void ImGuiBgfx::Render(bgfx::ViewId viewId) {
    ImDrawData* drawData = ImGui::GetDrawData();
    if (!drawData || !bgfx::isValid(g_program)) return;

    const int fbW = static_cast<int>(drawData->DisplaySize.x * drawData->FramebufferScale.x);
    const int fbH = static_cast<int>(drawData->DisplaySize.y * drawData->FramebufferScale.y);
    if (fbW <= 0 || fbH <= 0) return;

    g_frameIdx = (g_frameIdx + 1) % 2;

    EnsureCapacity(
        static_cast<uint32_t>(drawData->TotalVtxCount),
        static_cast<uint32_t>(drawData->TotalIdxCount));

    const float L = drawData->DisplayPos.x;
    const float R = drawData->DisplayPos.x + drawData->DisplaySize.x;
    const float T = drawData->DisplayPos.y;
    const float B = drawData->DisplayPos.y + drawData->DisplaySize.y;
    const float ortho[16] = {
        2.0f/(R-L),        0.0f,            0.0f,  0.0f,
        0.0f,              2.0f/(T-B),      0.0f,  0.0f,
        0.0f,              0.0f,           -1.0f,  0.0f,
       (R+L)/(L-R),       (T+B)/(B-T),      0.0f,  1.0f,
    };

    bgfx::setViewName(viewId, "ImGui");
    bgfx::setViewMode(viewId, bgfx::ViewMode::Sequential);
    bgfx::setViewRect(viewId, 0, 0, static_cast<uint16_t>(fbW), static_cast<uint16_t>(fbH));
    bgfx::setViewTransform(viewId, nullptr, ortho);

    const uint32_t vertexSize = g_layout.getSize(1);

    uint32_t vtxOffset = 0;
    uint32_t idxOffset = 0;
    for (int i = 0; i < drawData->CmdListsCount; ++i) {
        const ImDrawList* list = drawData->CmdLists[i];
        if (list->VtxBuffer.Size > 0) {
            bgfx::update(g_vb[g_frameIdx], vtxOffset,
                bgfx::copy(list->VtxBuffer.Data, static_cast<uint32_t>(list->VtxBuffer.Size) * vertexSize));
            vtxOffset += static_cast<uint32_t>(list->VtxBuffer.Size);
        }
        if (list->IdxBuffer.Size > 0) {
            bgfx::update(g_ib[g_frameIdx], idxOffset,
                bgfx::copy(list->IdxBuffer.Data, static_cast<uint32_t>(list->IdxBuffer.Size) * sizeof(ImDrawIdx)));
            idxOffset += static_cast<uint32_t>(list->IdxBuffer.Size);
        }
    }

    const ImVec2 clipOff = drawData->DisplayPos;
    uint32_t globalVtx = 0;
    uint32_t globalIdx = 0;

    for (int i = 0; i < drawData->CmdListsCount; ++i) {
        const ImDrawList* list = drawData->CmdLists[i];

        for (int j = 0; j < list->CmdBuffer.Size; ++j) {
            const ImDrawCmd* cmd = &list->CmdBuffer[j];
            if (cmd->UserCallback) { cmd->UserCallback(list, cmd); continue; }

            const float cL = (cmd->ClipRect.x - clipOff.x) * drawData->FramebufferScale.x;
            const float cT = (cmd->ClipRect.y - clipOff.y) * drawData->FramebufferScale.y;
            const float cR = (cmd->ClipRect.z - clipOff.x) * drawData->FramebufferScale.x;
            const float cB = (cmd->ClipRect.w - clipOff.y) * drawData->FramebufferScale.y;
            if (cR <= cL || cB <= cT) continue;

            const uint16_t sx = static_cast<uint16_t>(bx::max(cL, 0.0f));
            const uint16_t sy = static_cast<uint16_t>(bx::max(cT, 0.0f));
            const uint16_t ex = static_cast<uint16_t>(bx::min(cR, static_cast<float>(fbW)));
            const uint16_t ey = static_cast<uint16_t>(bx::min(cB, static_cast<float>(fbH)));
            if (ex <= sx || ey <= sy) continue;

            bgfx::setScissor(sx, sy, static_cast<uint16_t>(ex - sx), static_cast<uint16_t>(ey - sy));

            const bgfx::TextureHandle tex = { static_cast<uint16_t>(cmd->GetTexID()) };
            if (!bgfx::isValid(tex)) continue;
            bgfx::setTexture(0, g_texUniform, tex);

            bgfx::setState(
                BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA));

            bgfx::setVertexBuffer(0, g_vb[g_frameIdx], globalVtx, static_cast<uint32_t>(list->VtxBuffer.Size));
            bgfx::setIndexBuffer(g_ib[g_frameIdx], globalIdx + cmd->IdxOffset, cmd->ElemCount);

            bgfx::submit(viewId, g_program);
        }
        globalVtx += static_cast<uint32_t>(list->VtxBuffer.Size);
        globalIdx += static_cast<uint32_t>(list->IdxBuffer.Size);
    }
}

} // namespace Galeshi

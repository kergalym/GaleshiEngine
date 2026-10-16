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

bgfx::DynamicVertexBufferHandle g_vb[2] = { bgfx::kInvalidHandle, bgfx::kInvalidHandle };
bgfx::DynamicIndexBufferHandle  g_ib[2] = { bgfx::kInvalidHandle, bgfx::kInvalidHandle };

// ВАЖНО: capacity — отдельно для каждого буфера, потому что двойная буферизация.
uint32_t g_vbCapacity[2] = { 0, 0 };
uint32_t g_ibCapacity[2] = { 0, 0 };
uint32_t g_frameIdx      = 0;
} // namespace

void ImGuiBgfx::Init() {
    ImGuiIO& io = ImGui::GetIO();
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

    g_layout
        .begin()
        .add(bgfx::Attrib::Position,  2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0,    4, bgfx::AttribType::Uint8, true)
        .end();

    g_texUniform = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);

    g_program = ShaderManager::LoadProgram(
        "shaders/vs_imgui.bin", "shaders/fs_imgui.bin");

    if (!bgfx::isValid(g_program)) {
        spdlog::error("[ImGuiBgfx] Failed to load imgui shaders. "
                      "Проверьте shaders/vs_imgui.bin и shaders/fs_imgui.bin");
    }
}

void ImGuiBgfx::Shutdown() {
    for (int i = 0; i < 2; ++i) {
        if (bgfx::isValid(g_vb[i])) bgfx::destroy(g_vb[i]);
        if (bgfx::isValid(g_ib[i])) bgfx::destroy(g_ib[i]);
        g_vb[i] = BGFX_INVALID_HANDLE;
        g_ib[i] = BGFX_INVALID_HANDLE;
        g_vbCapacity[i] = 0;
        g_ibCapacity[i] = 0;
    }
    if (bgfx::isValid(g_texUniform)) bgfx::destroy(g_texUniform);
    if (bgfx::isValid(g_program))    bgfx::destroy(g_program);
    g_texUniform = BGFX_INVALID_HANDLE;
    g_program    = BGFX_INVALID_HANDLE;
}

namespace {

// Проверяем capacity именно того буфера, который используется в этом кадре.
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

void ProcessTextures(ImDrawData* drawData) {
    if (drawData->Textures == nullptr) return;

    for (ImTextureData* tex : *drawData->Textures) {
        if (tex == nullptr) continue;

        switch (tex->Status) {
        case ImTextureStatus_WantCreate: {
            if (tex->Width <= 0 || tex->Height <= 0) {
                tex->SetStatus(ImTextureStatus_Destroyed);
                break;
            }
            if (tex->Format != ImTextureFormat_RGBA32) {
                spdlog::warn("[ImGuiBgfx] Неподдерживаемый формат текстуры: {}",
                             static_cast<int>(tex->Format));
                tex->SetStatus(ImTextureStatus_Destroyed);
                break;
            }

            const uint32_t bytes = static_cast<uint32_t>(tex->GetSizeInBytes());
            const void*    data  = tex->GetPixels();
            if (bytes == 0 || data == nullptr) {
                spdlog::error("[ImGuiBgfx] Пустые данные текстуры {}x{} ({} байт)",
                              tex->Width, tex->Height, bytes);
                tex->SetStatus(ImTextureStatus_Destroyed);
                break;
            }

            // Mutable-текстура: mem = nullptr, затем updateTexture2D.
            bgfx::TextureHandle th = bgfx::createTexture2D(
                static_cast<uint16_t>(tex->Width),
                static_cast<uint16_t>(tex->Height),
                false, 1, bgfx::TextureFormat::RGBA8,
                BGFX_TEXTURE_NONE, nullptr);

            if (!bgfx::isValid(th)) {
                tex->SetStatus(ImTextureStatus_Destroyed);
                break;
            }

            bgfx::updateTexture2D(th, 0, 0, 0, 0,
                static_cast<uint16_t>(tex->Width),
                static_cast<uint16_t>(tex->Height),
                bgfx::copy(data, bytes));

            tex->SetTexID(static_cast<ImTextureID>(th.idx));
            tex->SetStatus(ImTextureStatus_OK);
            break;
        }

        case ImTextureStatus_WantUpdates: {
            const bgfx::TextureHandle th = { static_cast<uint16_t>(tex->TexID) };
            if (!bgfx::isValid(th)) {
                tex->SetStatus(ImTextureStatus_Destroyed);
                break;
            }

            const ImTextureRect& r = tex->UpdateRect;
            const int pitch = tex->GetPitch();

            if (r.w <= 0 || r.h <= 0 || pitch <= 0) {
                tex->SetStatus(ImTextureStatus_OK);
                break;
            }

            const uint32_t bytes =
                static_cast<uint32_t>(pitch) * static_cast<uint32_t>(r.h);
            const void* data = tex->GetPixelsAt(r.x, r.y);
            if (bytes == 0 || data == nullptr) {
                tex->SetStatus(ImTextureStatus_OK);
                break;
            }

            bgfx::updateTexture2D(th, 0, 0,
                static_cast<uint16_t>(r.x), static_cast<uint16_t>(r.y),
                static_cast<uint16_t>(r.w), static_cast<uint16_t>(r.h),
                bgfx::copy(data, bytes));

            tex->SetStatus(ImTextureStatus_OK);
            break;
        }

        case ImTextureStatus_WantDestroy: {
            const bgfx::TextureHandle th = { static_cast<uint16_t>(tex->TexID) };
            if (bgfx::isValid(th)) bgfx::destroy(th);
            tex->SetTexID(ImTextureID_Invalid);
            tex->SetStatus(ImTextureStatus_Destroyed);
            break;
        }

        default:
            break;
        }
    }
}

} // namespace

void ImGuiBgfx::Render(bgfx::ViewId viewId) {
    ImDrawData* drawData = ImGui::GetDrawData();
    if (!drawData) return;

    ProcessTextures(drawData);

    if (!bgfx::isValid(g_program)) return;
    if (drawData->TotalVtxCount == 0 || drawData->TotalIdxCount == 0) return;

    const int fbW = static_cast<int>(drawData->DisplaySize.x * drawData->FramebufferScale.x);
    const int fbH = static_cast<int>(drawData->DisplaySize.y * drawData->FramebufferScale.y);
    if (fbW <= 0 || fbH <= 0) return;

    g_frameIdx = (g_frameIdx + 1) % 2;

    EnsureCapacity(
        static_cast<uint32_t>(drawData->TotalVtxCount),
        static_cast<uint32_t>(drawData->TotalIdxCount));

    // Ортопроекция
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
    bgfx::setViewRect(viewId, 0, 0,
        static_cast<uint16_t>(fbW), static_cast<uint16_t>(fbH));
    bgfx::setViewTransform(viewId, nullptr, ortho);

    const uint32_t vertexSize = g_layout.getSize(1);

    uint32_t vtxOffset = 0;
    uint32_t idxOffset = 0;
    for (int i = 0; i < drawData->CmdListsCount; ++i) {
        const ImDrawList* list = drawData->CmdLists[i];
        if (list->VtxBuffer.Size > 0) {
            bgfx::update(g_vb[g_frameIdx], vtxOffset,
                bgfx::copy(list->VtxBuffer.Data,
                    static_cast<uint32_t>(list->VtxBuffer.Size) * vertexSize));
            vtxOffset += static_cast<uint32_t>(list->VtxBuffer.Size);
        }
        if (list->IdxBuffer.Size > 0) {
            bgfx::update(g_ib[g_frameIdx], idxOffset,
                bgfx::copy(list->IdxBuffer.Data,
                    static_cast<uint32_t>(list->IdxBuffer.Size) * sizeof(ImDrawIdx)));
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
            if (cmd->UserCallback) {
                cmd->UserCallback(list, cmd);
                continue;
            }

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

            bgfx::setScissor(sx, sy,
                static_cast<uint16_t>(ex - sx),
                static_cast<uint16_t>(ey - sy));

            const bgfx::TextureHandle tex = {
                static_cast<uint16_t>(cmd->GetTexID())
            };
            if (!bgfx::isValid(tex)) continue;
            bgfx::setTexture(0, g_texUniform, tex);

            bgfx::setState(
                BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                                      BGFX_STATE_BLEND_INV_SRC_ALPHA));

            bgfx::setVertexBuffer(0, g_vb[g_frameIdx], globalVtx,
                static_cast<uint32_t>(list->VtxBuffer.Size));
            bgfx::setIndexBuffer(g_ib[g_frameIdx],
                globalIdx + cmd->IdxOffset, cmd->ElemCount);

            bgfx::submit(viewId, g_program);
        }
        globalVtx += static_cast<uint32_t>(list->VtxBuffer.Size);
        globalIdx += static_cast<uint32_t>(list->IdxBuffer.Size);
    }
}

} // namespace Galeshi

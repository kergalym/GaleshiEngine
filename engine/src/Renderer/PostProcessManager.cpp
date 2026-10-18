#include "GaleshiEngine/Renderer/PostProcessManager.hpp"
#include <spdlog/spdlog.h>
#include <cstdio>

namespace Galeshi {

namespace {

struct PosTexVertex {
    float x, y, z;
    float u, v;
    static bgfx::VertexLayout& Layout() {
        static bgfx::VertexLayout s;
        static bool init = false;
        if (!init) {
            s.begin()
                .add(bgfx::Attrib::Position,  3, bgfx::AttribType::Float)
                .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
                .end();
            init = true;
        }
        return s;
    }
};

bgfx::ShaderHandle LoadBinShader(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        spdlog::error("[PP] shader not found: {}", path);
        return BGFX_INVALID_HANDLE;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    const bgfx::Memory* mem = bgfx::alloc(static_cast<uint32_t>(sz));
    fread(mem->data, 1, static_cast<size_t>(sz), f);
    fclose(f);
    return bgfx::createShader(mem);
}

} // namespace

PostProcessManager::PostProcessManager() = default;
PostProcessManager::~PostProcessManager() { Shutdown(); }

void PostProcessManager::Init() {
    if (bgfx::isValid(m_program)) return;

    bgfx::ShaderHandle vs = LoadBinShader("shaders/vs_fullscreen.bin");
    bgfx::ShaderHandle fs = LoadBinShader("shaders/fs_passthrough.bin");
    if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) {
        spdlog::error("[PP] failed to load passthrough shaders");
        return;
    }
    m_program = bgfx::createProgram(vs, fs, true);
    s_texColor = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);

    // Invert program: same VS, fs_invert
    bgfx::ShaderHandle vs2 = LoadBinShader("shaders/vs_fullscreen.bin");
    bgfx::ShaderHandle fs2 = LoadBinShader("shaders/fs_invert.bin");
    if (bgfx::isValid(vs2) && bgfx::isValid(fs2)) {
        m_invertProg = bgfx::createProgram(vs2, fs2, true);
        spdlog::info("[PP] invert program loaded");
    } else {
        spdlog::warn("[PP] fs_invert.bin missing — inversion disabled");
    }

    // FXAA program (same VS)
    bgfx::ShaderHandle vs3 = LoadBinShader("shaders/vs_fullscreen.bin");
    bgfx::ShaderHandle fs3 = LoadBinShader("shaders/fs_fxaa.bin");
    if (bgfx::isValid(vs3) && bgfx::isValid(fs3)) {
        m_fxaaProg = bgfx::createProgram(vs3, fs3, true);
        u_texelSize = bgfx::createUniform("u_texelSize", bgfx::UniformType::Vec4);
        spdlog::info("[PP] FXAA program loaded");
    } else {
        spdlog::warn("[PP] fs_fxaa.bin missing — FXAA disabled");
    }

    // Lighting program (deferred)
    {
        bgfx::ShaderHandle vsL = LoadBinShader("shaders/vs_fullscreen.bin");
        bgfx::ShaderHandle fsL = LoadBinShader("shaders/fs_lighting.bin");
        if (bgfx::isValid(vsL) && bgfx::isValid(fsL)) {
            m_lightingProg = bgfx::createProgram(vsL, fsL, true);
            u_camPos      = bgfx::createUniform("u_cameraPosition",     bgfx::UniformType::Vec4);
            u_lightDir    = bgfx::createUniform("u_lightDirection",     bgfx::UniformType::Vec4);
            u_lightCol    = bgfx::createUniform("u_lightColorIntensity",bgfx::UniformType::Vec4);
            s_albedoMr    = bgfx::createUniform("s_albedoMr",    bgfx::UniformType::Sampler);
            s_normalRough = bgfx::createUniform("s_normalRough", bgfx::UniformType::Sampler);
            s_worldPos    = bgfx::createUniform("s_worldPos",    bgfx::UniformType::Sampler);
            spdlog::info("[PP] lighting program loaded");
        bgfx::ShaderHandle vsS = LoadBinShader("shaders/vs_fullscreen.bin");
        bgfx::ShaderHandle fsS = LoadBinShader("shaders/fs_ssr.bin");
        if (bgfx::isValid(vsS) && bgfx::isValid(fsS)) {
            m_ssrProg = bgfx::createProgram(vsS, fsS, true);
            spdlog::info("[PP] SSR program loaded");
        }
        } else {
            spdlog::warn("[PP] fs_lighting.bin missing — deferred disabled");
        }
    }

    static const PosTexVertex quad[4] = {
        {-1.0f, -1.0f, 0.0f,  0.0f, 1.0f},
        { 1.0f, -1.0f, 0.0f,  1.0f, 1.0f},
        {-1.0f,  1.0f, 0.0f,  0.0f, 0.0f},
        { 1.0f,  1.0f, 0.0f,  1.0f, 0.0f},
    };
    static const uint16_t indices[6] = {0, 1, 2, 2, 1, 3};
    m_vbh = bgfx::createVertexBuffer(
        bgfx::makeRef(quad, sizeof(quad)), PosTexVertex::Layout());
    m_ibh = bgfx::createIndexBuffer(
        bgfx::makeRef(indices, sizeof(indices)));

    spdlog::info("[PP] PostProcessManager initialized (passthrough)");
}

void PostProcessManager::Shutdown() {
    if (bgfx::isValid(m_program))     { bgfx::destroy(m_program);     m_program     = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(m_invertProg))  { bgfx::destroy(m_invertProg);  m_invertProg  = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(m_fxaaProg))    { bgfx::destroy(m_fxaaProg);    m_fxaaProg    = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(m_lightingProg)){ bgfx::destroy(m_lightingProg);m_lightingProg= BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(u_camPos))      { bgfx::destroy(u_camPos);      u_camPos      = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(u_lightDir))    { bgfx::destroy(u_lightDir);    u_lightDir    = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(u_lightCol))    { bgfx::destroy(u_lightCol);    u_lightCol    = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(s_albedoMr))    { bgfx::destroy(s_albedoMr);    s_albedoMr    = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(s_normalRough)) { bgfx::destroy(s_normalRough); s_normalRough = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(s_worldPos))    { bgfx::destroy(s_worldPos);    s_worldPos    = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(u_texelSize))   { bgfx::destroy(u_texelSize);   u_texelSize   = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(s_texColor))    { bgfx::destroy(s_texColor);    s_texColor    = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(m_vbh))         { bgfx::destroy(m_vbh);         m_vbh         = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(m_ibh))         { bgfx::destroy(m_ibh);         m_ibh         = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(m_outputFbo))   { bgfx::destroy(m_outputFbo);   m_outputFbo   = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(m_outputColor)) { bgfx::destroy(m_outputColor); m_outputColor = BGFX_INVALID_HANDLE; }
    m_width = m_height = 0;
}

void PostProcessManager::EnsureOutputFbo(uint16_t w, uint16_t h) {
    if (bgfx::isValid(m_outputFbo) && m_width == w && m_height == h) return;
    if (bgfx::isValid(m_outputFbo))   bgfx::destroy(m_outputFbo);
    if (bgfx::isValid(m_outputColor)) bgfx::destroy(m_outputColor);
    if (bgfx::isValid(m_outputDepth)) bgfx::destroy(m_outputDepth);

    const uint64_t flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    m_outputColor = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA8, flags);
    m_outputDepth = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::D24S8, flags);
    bgfx::TextureHandle att[] = { m_outputColor, m_outputDepth };
    m_outputFbo   = bgfx::createFrameBuffer(2, att, false);
    m_width = w; m_height = h;
}

bgfx::TextureHandle PostProcessManager::Render(bgfx::ViewId viewId,
                                               bgfx::TextureHandle inputColor,
                                               uint16_t width, uint16_t height) {
    if (!bgfx::isValid(m_program) || width == 0 || height == 0) return BGFX_INVALID_HANDLE;
    EnsureOutputFbo(width, height);
    if (!bgfx::isValid(m_outputFbo)) return BGFX_INVALID_HANDLE;

    bgfx::setViewFrameBuffer(viewId, m_outputFbo);
    bgfx::setViewRect(viewId, 0, 0, width, height);
    bgfx::setViewClear(viewId, BGFX_CLEAR_COLOR, 0x000000ff);

    bgfx::setVertexBuffer(0, m_vbh);
    bgfx::setIndexBuffer(m_ibh);
    bgfx::setTexture(0, s_texColor, inputColor);
    if (bgfx::isValid(u_texelSize)) {
        const float ts[4] = { 1.0f / float(width), 1.0f / float(height), 0.0f, 0.0f };
        bgfx::setUniform(u_texelSize, ts);
    }
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::ProgramHandle use = m_program;
    if (m_fxaaEnabled && bgfx::isValid(m_fxaaProg))          use = m_fxaaProg;
    else if (m_invertEnabled && bgfx::isValid(m_invertProg)) use = m_invertProg;
    bgfx::submit(viewId, use);
    return m_outputColor;
}


bgfx::TextureHandle PostProcessManager::RenderLighting(
    bgfx::TextureHandle albedoMr, bgfx::TextureHandle normalRough, bgfx::TextureHandle worldPos,
    const float camPos[4], const float lightDir[4], const float lightCol[4],
    bgfx::ViewId viewId, uint16_t width, uint16_t height)
{
    if (!bgfx::isValid(m_lightingProg) || width == 0 || height == 0) return BGFX_INVALID_HANDLE;
    EnsureOutputFbo(width, height);
    if (!bgfx::isValid(m_outputFbo)) return BGFX_INVALID_HANDLE;

    bgfx::setViewFrameBuffer(viewId, m_outputFbo);
    bgfx::setViewRect(viewId, 0, 0, width, height);
    bgfx::setViewClear(viewId, BGFX_CLEAR_COLOR, 0x000000ff);

    bgfx::setUniform(u_camPos,   camPos);
    bgfx::setUniform(u_lightDir, lightDir);
    bgfx::setUniform(u_lightCol, lightCol);
    bgfx::setTexture(0, s_albedoMr,    albedoMr);
    bgfx::setTexture(1, s_normalRough, normalRough);
    bgfx::setTexture(2, s_worldPos,    worldPos);

    bgfx::setVertexBuffer(0, m_vbh);
    bgfx::setIndexBuffer(m_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(viewId, m_lightingProg);
    return m_outputColor;
}

bgfx::TextureHandle PostProcessManager::RenderSSR(bgfx::TextureHandle color, bgfx::TextureHandle normal, bgfx::TextureHandle depth, bgfx::ViewId viewId, uint16_t width, uint16_t height) {
    if (!bgfx::isValid(m_ssrProg)) return color;
    EnsureOutputFbo(width, height);
    
    // Use a temporary FBO for SSR? For now, just render to main output.
    bgfx::setViewFrameBuffer(viewId, m_outputFbo);
    bgfx::setViewRect(viewId, 0, 0, width, height);
    
    static bgfx::UniformHandle s_c = bgfx::createUniform("s_color", bgfx::UniformType::Sampler);
    static bgfx::UniformHandle s_n = bgfx::createUniform("s_normalRough", bgfx::UniformType::Sampler);
    static bgfx::UniformHandle s_d = bgfx::createUniform("s_depth", bgfx::UniformType::Sampler);
    
    bgfx::setTexture(0, s_c, color);
    bgfx::setTexture(1, s_n, normal);
    bgfx::setTexture(2, s_d, depth);
    
    bgfx::setVertexBuffer(0, m_vbh);
    bgfx::setIndexBuffer(m_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(viewId, m_ssrProg);
    return m_outputColor;
}
} // namespace Galeshi

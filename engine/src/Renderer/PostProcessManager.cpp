#include "GaleshiEngine/Renderer/PostProcessManager.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <spdlog/spdlog.h>
#include <cstdio>
#include <utility>

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
    if (!f) return BGFX_INVALID_HANDLE;
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

    // Passthrough
    {
        auto vs = LoadBinShader("shaders/vs_fullscreen.bin");
        auto fs = LoadBinShader("shaders/fs_passthrough.bin");
        if (bgfx::isValid(vs) && bgfx::isValid(fs)) {
            m_program = bgfx::createProgram(vs, fs, true);
        }
    }

    // Invert
    {
        auto vs = LoadBinShader("shaders/vs_fullscreen.bin");
        auto fs = LoadBinShader("shaders/fs_invert.bin");
        if (bgfx::isValid(vs) && bgfx::isValid(fs)) {
            m_invertProg = bgfx::createProgram(vs, fs, true);
        }
    }

    // FXAA
    {
        auto vs = LoadBinShader("shaders/vs_fullscreen.bin");
        auto fs = LoadBinShader("shaders/fs_fxaa.bin");
        if (bgfx::isValid(vs) && bgfx::isValid(fs)) {
            m_fxaaProg = bgfx::createProgram(vs, fs, true);
            u_texelSize = bgfx::createUniform("u_texelSize", bgfx::UniformType::Vec4);
        }
    }

    // Lighting
    {
        auto vs = LoadBinShader("shaders/vs_fullscreen.bin");
        auto fs = LoadBinShader("shaders/fs_lighting.bin");
        if (bgfx::isValid(vs) && bgfx::isValid(fs)) {
            m_lightingProg = bgfx::createProgram(vs, fs, true);
            u_camPos      = bgfx::createUniform("u_cameraPosition",     bgfx::UniformType::Vec4);
            u_lightDir    = bgfx::createUniform("u_lightDirection",     bgfx::UniformType::Vec4);
            u_lightCol    = bgfx::createUniform("u_lightColorIntensity",bgfx::UniformType::Vec4);
            s_albedoMr    = bgfx::createUniform("s_albedoMr",    bgfx::UniformType::Sampler);
            s_normalRough = bgfx::createUniform("s_normalRough", bgfx::UniformType::Sampler);
            s_worldPos    = bgfx::createUniform("s_worldPos",    bgfx::UniformType::Sampler);
            s_shadowMap   = bgfx::createUniform("s_shadowMap",   bgfx::UniformType::Sampler);
            u_lightVP     = bgfx::createUniform("u_lightViewProj", bgfx::UniformType::Mat4);
            u_plPos       = bgfx::createUniform("u_pointLightPosRadius", bgfx::UniformType::Vec4, 8);
            spdlog::info("[PP] u_plPos valid: {}", bgfx::isValid(u_plPos));
            u_plCol       = bgfx::createUniform("u_pointLightColorInt",  bgfx::UniformType::Vec4, 8);
            u_plCnt       = bgfx::createUniform("u_pointLightCount",     bgfx::UniformType::Vec4);
            u_slPos       = bgfx::createUniform("u_spotLightPosRadius",  bgfx::UniformType::Vec4, 4);
            u_slDir       = bgfx::createUniform("u_spotLightDirInner",   bgfx::UniformType::Vec4, 4);
            u_slCol       = bgfx::createUniform("u_spotLightColorOuter", bgfx::UniformType::Vec4, 4);
            u_slCnt       = bgfx::createUniform("u_spotLightCount",      bgfx::UniformType::Vec4);
        }
    }

    // SSAO
    {
        auto vs = LoadBinShader("shaders/vs_fullscreen.bin");
        auto fs = LoadBinShader("shaders/fs_ssao.bin");
        if (bgfx::isValid(vs) && bgfx::isValid(fs)) {
            m_ssaoProg = bgfx::createProgram(vs, fs, true);
            u_ssaoProj    = bgfx::createUniform("u_ssaoProj",    bgfx::UniformType::Mat4);
            u_ssaoInvProj = bgfx::createUniform("u_ssaoInvProj", bgfx::UniformType::Mat4);
            u_ssaoParams  = bgfx::createUniform("u_ssaoParams",  bgfx::UniformType::Vec4);
            s_ssaoColor   = bgfx::createUniform("s_color",       bgfx::UniformType::Sampler);
            s_ssaoDepth   = bgfx::createUniform("s_depth",       bgfx::UniformType::Sampler);
            s_ssaoNormal  = bgfx::createUniform("s_normal",      bgfx::UniformType::Sampler);
        }
    }

    // SSR
    {
        auto vs = LoadBinShader("shaders/vs_fullscreen.bin");
        auto fs = LoadBinShader("shaders/fs_ssr.bin");
        if (bgfx::isValid(vs) && bgfx::isValid(fs)) {
            m_ssrProg = bgfx::createProgram(vs, fs, true);
            u_ssrInvProj = bgfx::createUniform("u_ssrInvProj",    bgfx::UniformType::Mat4);
            s_ssrColor   = bgfx::createUniform("s_color",         bgfx::UniformType::Sampler);
            s_ssrNormal  = bgfx::createUniform("s_normalRough",   bgfx::UniformType::Sampler);
            s_ssrDepth   = bgfx::createUniform("s_depth",         bgfx::UniformType::Sampler);
            s_smaaEdges  = bgfx::createUniform("s_edges",         bgfx::UniformType::Sampler);
            s_taaColor   = bgfx::createUniform("s_color",         bgfx::UniformType::Sampler);
        }
    }

    s_texColor = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
    { auto vs = LoadBinShader("shaders/vs_fullscreen.bin");
      auto fs = LoadBinShader("shaders/fs_taa.bin");
      if(bgfx::isValid(vs) && bgfx::isValid(fs)) {
          m_taaProg = bgfx::createProgram(vs, fs, true);
          u_taaPrevVP = bgfx::createUniform("u_prevViewProj", bgfx::UniformType::Mat4);
          u_taaInvVP = bgfx::createUniform("u_invViewProj", bgfx::UniformType::Mat4);
          u_taaParams = bgfx::createUniform("u_taaParams", bgfx::UniformType::Vec4);
          s_taaHistory = bgfx::createUniform("s_history", bgfx::UniformType::Sampler);
          s_taaDepth = bgfx::createUniform("s_depth", bgfx::UniformType::Sampler);
      } }
    { auto vs = LoadBinShader("shaders/vs_fullscreen.bin");
      auto fs = LoadBinShader("shaders/fs_sky.bin");
      if(bgfx::isValid(vs) && bgfx::isValid(fs)) {
          m_skyProg = bgfx::createProgram(vs, fs, true);
          u_skyInvVP = bgfx::createUniform("u_skyInvViewProj", bgfx::UniformType::Mat4);
          u_skySun = bgfx::createUniform("u_skySunDir", bgfx::UniformType::Vec4);
      } }
    { auto vs = LoadBinShader("shaders/vs_fullscreen.bin");
      auto fs = LoadBinShader("shaders/fs_smaa_edge.bin");
      if(bgfx::isValid(vs) && bgfx::isValid(fs)) m_smaaEdgeProg = bgfx::createProgram(vs, fs, true);
      fs = LoadBinShader("shaders/fs_smaa_blend.bin");
      if(bgfx::isValid(vs) && bgfx::isValid(fs)) m_smaaBlendProg = bgfx::createProgram(vs, fs, true); }

    // Fullscreen quad
    static const PosTexVertex quad[4] = {
        {-1.0f, -1.0f, 0.0f,  0.0f, 1.0f},
        { 1.0f, -1.0f, 0.0f,  1.0f, 1.0f},
        {-1.0f,  1.0f, 0.0f,  0.0f, 0.0f},
        { 1.0f,  1.0f, 0.0f,  1.0f, 0.0f},
    };
    static const uint16_t indices[6] = {0, 1, 2, 2, 1, 3};
    m_vbh = bgfx::createVertexBuffer(bgfx::makeRef(quad, sizeof(quad)), PosTexVertex::Layout());
    m_ibh = bgfx::createIndexBuffer(bgfx::makeRef(indices, sizeof(indices)));

    spdlog::info("[PP] === BUILD_MARKER_V2 ===");
    spdlog::info("[PP] PostProcessManager initialized");
}

void PostProcessManager::Shutdown() {
    auto destroyProg = [](bgfx::ProgramHandle& h) { if (bgfx::isValid(h)) { bgfx::destroy(h); h = BGFX_INVALID_HANDLE; } };
    auto destroyUni  = [](bgfx::UniformHandle& h) { if (bgfx::isValid(h)) { bgfx::destroy(h); h = BGFX_INVALID_HANDLE; } };
    auto destroyTex  = [](bgfx::TextureHandle& h) { if (bgfx::isValid(h)) { bgfx::destroy(h); h = BGFX_INVALID_HANDLE; } };
    auto destroyFbo  = [](bgfx::FrameBufferHandle& h) { if (bgfx::isValid(h)) { bgfx::destroy(h); h = BGFX_INVALID_HANDLE; } };

    destroyProg(m_program); destroyProg(m_invertProg); destroyProg(m_fxaaProg);
    destroyProg(m_lightingProg); destroyProg(m_ssaoProg); destroyProg(m_ssrProg); destroyProg(m_taaProg);
    destroyUni(u_taaPrevVP); destroyUni(u_taaInvVP); destroyUni(u_taaParams); destroyUni(s_taaHistory); destroyUni(s_taaDepth);
    if(bgfx::isValid(m_historyColor)) bgfx::destroy(m_historyColor); if(bgfx::isValid(m_historyFbo)) bgfx::destroy(m_historyFbo); destroyProg(m_skyProg);
    destroyUni(u_skyInvVP); destroyUni(u_skySun); destroyProg(m_smaaEdgeProg); destroyProg(m_smaaBlendProg);
    if(bgfx::isValid(m_smaaEdgeTex)) bgfx::destroy(m_smaaEdgeTex);

    destroyUni(s_texColor); destroyUni(u_texelSize);
    destroyUni(u_camPos); destroyUni(u_lightDir); destroyUni(u_lightCol);
    destroyUni(s_albedoMr); destroyUni(s_normalRough); destroyUni(s_worldPos);
    destroyUni(s_shadowMap); destroyUni(u_lightVP); 
    destroyUni(u_plPos); destroyUni(u_plCol); destroyUni(u_plCnt);
    destroyUni(u_slPos); destroyUni(u_slDir); destroyUni(u_slCol); destroyUni(u_slCnt);
    destroyUni(u_ssaoProj); destroyUni(u_ssaoInvProj); destroyUni(u_ssaoParams);
    destroyUni(s_ssaoColor); destroyUni(s_ssaoDepth); destroyUni(s_ssaoNormal);
    destroyUni(u_ssrInvProj); destroyUni(s_ssrColor); destroyUni(s_ssrNormal); destroyUni(s_ssrDepth);
    destroyUni(s_smaaEdges);
    destroyUni(s_taaColor);
    if (bgfx::isValid(m_blitFboA)) { bgfx::destroy(m_blitFboA); m_blitFboA = BGFX_INVALID_HANDLE; }

    if (bgfx::isValid(m_vbh)) { bgfx::destroy(m_vbh); m_vbh = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(m_ibh)) { bgfx::destroy(m_ibh); m_ibh = BGFX_INVALID_HANDLE; }

    destroyTex(m_outputColor); destroyTex(m_outputDepth); destroyFbo(m_outputFbo);
    destroyTex(m_pingColor); destroyFbo(m_pingFbo);
    destroyTex(m_blitDepth);
    m_width = m_height = 0;
}

void PostProcessManager::EnsureOutputFbo(uint16_t w, uint16_t h) {
    if (bgfx::isValid(m_outputFbo) && m_width == w && m_height == h) return;
    if (bgfx::isValid(m_outputFbo))   bgfx::destroy(m_outputFbo);
    if (bgfx::isValid(m_outputColor)) bgfx::destroy(m_outputColor);
    if (bgfx::isValid(m_outputDepth)) bgfx::destroy(m_outputDepth);
    if (bgfx::isValid(m_pingFbo))     bgfx::destroy(m_pingFbo);
    if (bgfx::isValid(m_pingColor))   bgfx::destroy(m_pingColor);
    if (bgfx::isValid(m_blitTexA))    bgfx::destroy(m_blitTexA);
    if (bgfx::isValid(m_blitTexB))    bgfx::destroy(m_blitTexB);
    if (bgfx::isValid(m_blitDepth))   bgfx::destroy(m_blitDepth);

    const uint64_t flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    m_outputColor = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA8, flags);
    m_outputDepth = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::D32F, flags);
    bgfx::TextureHandle att[] = { m_outputColor, m_outputDepth };
    m_outputFbo = bgfx::createFrameBuffer(2, att, false);

    m_blitTexA = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA8, flags | BGFX_TEXTURE_BLIT_DST);
    m_blitTexB = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA16F, flags | BGFX_TEXTURE_BLIT_DST);
    m_blitDepth = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::D32F, flags | BGFX_TEXTURE_BLIT_DST);
    m_pingColor = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA8, flags);
    bgfx::TextureHandle pingAtt[] = { m_pingColor };
    m_pingFbo = bgfx::createFrameBuffer(1, pingAtt, false);

    if(bgfx::isValid(m_historyFbo)) bgfx::destroy(m_historyFbo);
    if(bgfx::isValid(m_historyColor)) bgfx::destroy(m_historyColor);
    m_historyColor = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA8, flags | BGFX_TEXTURE_BLIT_DST);
    m_historyFbo = bgfx::createFrameBuffer(1, &m_historyColor, false);
    m_historyValid = false;
    if (bgfx::isValid(m_blitFboA)) bgfx::destroy(m_blitFboA);
    m_blitFboA = bgfx::createFrameBuffer(1, &m_blitTexA, false);
    m_width = w; m_height = h;
}

void PostProcessManager::SwapFbos() {
    std::swap(m_outputFbo, m_pingFbo);
    std::swap(m_outputColor, m_pingColor);
}

bgfx::TextureHandle PostProcessManager::Render(bgfx::ViewId viewId, bgfx::TextureHandle inputColor, uint16_t width, uint16_t height) {
    if (!bgfx::isValid(m_program) || width == 0 || height == 0) return BGFX_INVALID_HANDLE;
    EnsureOutputFbo(width, height);
    if (!bgfx::isValid(m_outputFbo)) return BGFX_INVALID_HANDLE;

    bgfx::setViewFrameBuffer(viewId, m_outputFbo);
    bgfx::setViewRect(viewId, 0, 0, width, height);
    bgfx::setViewClear(viewId, BGFX_CLEAR_COLOR, 0x000000ff);

    bgfx::setVertexBuffer(0, m_vbh);
    bgfx::setIndexBuffer(m_ibh);
    bgfx::setTexture(0, s_texColor, inputColor);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(viewId, m_program);
    return m_outputColor;
}

bgfx::TextureHandle PostProcessManager::RenderLighting(
    bgfx::TextureHandle albedoMr, bgfx::TextureHandle normalRough, bgfx::TextureHandle worldPos,
    bgfx::TextureHandle shadowMap, const glm::mat4& lightMtx, 
    const float camPos[4], const float lightDir[4], const float lightColorIntensity[4],
    const float pointLightPos[][4], const float pointLightColor[][4], int pointLightCount,
    const float spotLightPosRadius[][4], const float spotLightDirInner[][4], const float spotLightColorOuter[][4], int spotLightCount,
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
    bgfx::setUniform(u_lightCol, lightColorIntensity);
    bgfx::setTexture(0, s_albedoMr,    albedoMr);
    bgfx::setTexture(1, s_normalRough, normalRough);
    bgfx::setTexture(2, s_worldPos,    worldPos);
    bgfx::setTexture(3, s_shadowMap,   shadowMap);
    bgfx::setUniform(u_lightVP, glm::value_ptr(lightMtx));
    

    if (pointLightCount > 0) {
        bgfx::setUniform(u_plPos, pointLightPos, 8);
        bgfx::setUniform(u_plCol, pointLightColor, 8);
    }
    float plArr[4] = { float(pointLightCount), 0, 0, 0 };
    bgfx::setUniform(u_plCnt, plArr);
    if (spotLightCount > 0) {
        bgfx::setUniform(u_slPos, spotLightPosRadius, 4);
        bgfx::setUniform(u_slDir, spotLightDirInner, 4);
        bgfx::setUniform(u_slCol, spotLightColorOuter, 4);
    }
    float slArr[4] = { float(spotLightCount), 0, 0, 0 };
    bgfx::setUniform(u_slCnt, slArr);

    bgfx::setVertexBuffer(0, m_vbh);
    bgfx::setIndexBuffer(m_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(viewId, m_lightingProg);
    return m_outputColor;
}

bgfx::TextureHandle PostProcessManager::RenderSSAO(bgfx::TextureHandle color, bgfx::TextureHandle depth, bgfx::TextureHandle normal, bgfx::FrameBufferHandle targetFbo, const glm::mat4& proj, const glm::mat4& invProj, bgfx::ViewId viewId, uint16_t width, uint16_t height) {
    if (!bgfx::isValid(m_ssaoProg)) return color;
    bgfx::setViewFrameBuffer(viewId, targetFbo);
    bgfx::setViewRect(viewId, 0, 0, width, height);
    BlitTexture(color,  m_blitTexA, viewId);
    BlitTexture(depth,  m_blitDepth, viewId);
    BlitTexture(normal, m_blitTexB, viewId);
    bgfx::setTexture(0, s_ssaoColor,  m_blitTexA);
    bgfx::setTexture(1, s_ssaoDepth,  m_blitDepth);
    bgfx::setTexture(2, s_ssaoNormal, m_blitTexB);
    bgfx::setUniform(u_ssaoProj,    glm::value_ptr(proj));
    bgfx::setUniform(u_ssaoInvProj, glm::value_ptr(invProj));
    float params[4] = { 0.5f, 0.02f, 1.0f, 0.0f };
    bgfx::setUniform(u_ssaoParams, params);
    bgfx::setVertexBuffer(0, m_vbh);
    bgfx::setIndexBuffer(m_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(viewId, m_ssaoProg);
    return m_pingColor;
}

bgfx::TextureHandle PostProcessManager::RenderSSR(bgfx::TextureHandle color, bgfx::TextureHandle normal, bgfx::TextureHandle depth, bgfx::FrameBufferHandle targetFbo, const glm::mat4& invProj, const float camPos[4], bgfx::ViewId viewId, uint16_t width, uint16_t height) {
    if (!bgfx::isValid(m_ssrProg)) return color;
    bgfx::setViewFrameBuffer(viewId, targetFbo);
    bgfx::setViewRect(viewId, 0, 0, width, height);
    BlitTexture(color,  m_blitTexA, viewId);
    BlitTexture(normal, m_blitTexB, viewId);
    BlitTexture(depth,  m_blitDepth, viewId);
    bgfx::setTexture(0, s_ssrColor,  m_blitTexA);
    bgfx::setTexture(1, s_ssrNormal, m_blitTexB);
    bgfx::setTexture(2, s_ssrDepth,  m_blitDepth);
    bgfx::setUniform(u_ssrInvProj, glm::value_ptr(invProj));
    bgfx::setVertexBuffer(0, m_vbh);
    bgfx::setIndexBuffer(m_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(viewId, m_ssrProg);
    return m_pingColor;
}

bgfx::TextureHandle PostProcessManager::RenderFxaa(bgfx::TextureHandle color, bgfx::FrameBufferHandle targetFbo, bgfx::ViewId viewId, uint16_t width, uint16_t height) {
    if (!bgfx::isValid(m_fxaaProg)) return color;
    bgfx::setViewFrameBuffer(viewId, targetFbo);
    bgfx::setViewRect(viewId, 0, 0, width, height);
    bgfx::setTexture(0, s_texColor, color);
    if (bgfx::isValid(u_texelSize)) {
        float ts[4] = { 1.0f / float(width), 1.0f / float(height), 0.0f, 0.0f };
        bgfx::setUniform(u_texelSize, ts);
    }
    bgfx::setVertexBuffer(0, m_vbh);
    bgfx::setIndexBuffer(m_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(viewId, m_fxaaProg);
    return m_pingColor;
}

bgfx::TextureHandle PostProcessManager::RenderInvert(bgfx::TextureHandle color, bgfx::FrameBufferHandle targetFbo, bgfx::ViewId viewId, uint16_t width, uint16_t height) {
    if (!bgfx::isValid(m_invertProg)) return color;
    bgfx::setViewFrameBuffer(viewId, targetFbo);
    bgfx::setViewRect(viewId, 0, 0, width, height);
    bgfx::setTexture(0, s_texColor, color);
    bgfx::setVertexBuffer(0, m_vbh);
    bgfx::setIndexBuffer(m_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(viewId, m_invertProg);
    return m_pingColor;
}


bgfx::TextureHandle PostProcessManager::RenderSmaa(bgfx::TextureHandle color, bgfx::FrameBufferHandle target, bgfx::ViewId viewId, uint16_t w, uint16_t h) {
    if (!bgfx::isValid(m_smaaEdgeProg) || !bgfx::isValid(m_smaaBlendProg)) return color;
    EnsureOutputFbo(w, h);
    if (!bgfx::isValid(m_blitFboA)) return color;

    float ts[4] = { 1.0f / float(w), 1.0f / float(h), 0.0f, 0.0f };

    // Pass 1: Edge detection into m_blitFboA
    bgfx::setViewFrameBuffer(viewId, m_blitFboA);
    bgfx::setViewRect(viewId, 0, 0, w, h);
    bgfx::setTexture(0, s_texColor, color);
    bgfx::setUniform(u_texelSize, ts);
    bgfx::setVertexBuffer(0, m_vbh);
    bgfx::setIndexBuffer(m_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(viewId, m_smaaEdgeProg);

    // Pass 2: Blend into target
    bgfx::setViewFrameBuffer(viewId + 1, target);
    bgfx::setViewRect(viewId + 1, 0, 0, w, h);
    bgfx::setTexture(0, s_texColor, color);
    bgfx::setTexture(1, s_smaaEdges, m_blitTexA);
    bgfx::setUniform(u_texelSize, ts);
    bgfx::setVertexBuffer(0, m_vbh);
    bgfx::setIndexBuffer(m_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(viewId + 1, m_smaaBlendProg);

    return m_pingColor;
}


bgfx::TextureHandle PostProcessManager::RenderSky(const glm::mat4& invProj, const glm::mat4& invView, const float sunDir[4], bgfx::FrameBufferHandle target, bgfx::ViewId viewId, uint16_t w, uint16_t h) {
    if (!bgfx::isValid(m_skyProg)) return BGFX_INVALID_HANDLE;
    bgfx::setViewFrameBuffer(viewId, target);
    bgfx::setViewRect(viewId, 0, 0, w, h);
    glm::mat4 invVP = invView * invProj;
    bgfx::setUniform(u_skyInvVP, glm::value_ptr(invVP));
    bgfx::setUniform(u_skySun, sunDir);
    bgfx::setVertexBuffer(0, m_vbh);
    bgfx::setIndexBuffer(m_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_ALWAYS);
    bgfx::submit(viewId, m_skyProg);
    return m_pingColor;
}


bgfx::TextureHandle PostProcessManager::RenderTaa(bgfx::TextureHandle color, bgfx::TextureHandle depth, const glm::mat4& prevVP, const glm::mat4& invVP, bgfx::FrameBufferHandle target, bgfx::ViewId viewId, uint16_t w, uint16_t h) {
    if (!bgfx::isValid(m_taaProg)) return color;
    EnsureOutputFbo(w, h);
    if (!bgfx::isValid(m_historyColor)) return color;

    if (!m_historyValid) {
        BlitTexture(color, m_historyColor, viewId);
        bgfx::setViewFrameBuffer(viewId, target);
        bgfx::setViewRect(viewId, 0, 0, w, h);
        bgfx::setTexture(0, s_texColor, color);
        bgfx::setVertexBuffer(0, m_vbh);
        bgfx::setIndexBuffer(m_ibh);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::submit(viewId, m_program);
        m_historyValid = true;
        return m_pingColor;
    }

    bgfx::setViewFrameBuffer(viewId, target);
    bgfx::setViewRect(viewId, 0, 0, w, h);
    bgfx::setTexture(0, s_taaColor, color);
    bgfx::setTexture(1, s_taaHistory, m_historyColor);
    bgfx::setTexture(2, s_taaDepth, depth);
    bgfx::setUniform(u_taaPrevVP, glm::value_ptr(prevVP));
    bgfx::setUniform(u_taaInvVP, glm::value_ptr(invVP));
    float params[4] = { 0.1f, 0, 0, 0 };
    bgfx::setUniform(u_taaParams, params);
    bgfx::setVertexBuffer(0, m_vbh);
    bgfx::setIndexBuffer(m_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(viewId, m_taaProg);

    BlitTexture(m_pingColor, m_historyColor, viewId + 1);

    return m_pingColor;
}

void PostProcessManager::BlitTexture(bgfx::TextureHandle src, bgfx::TextureHandle dst, bgfx::ViewId viewId) {
    if (!bgfx::isValid(src) || !bgfx::isValid(dst)) return;
    if ((bgfx::getCaps()->supported & BGFX_CAPS_TEXTURE_BLIT) == 0) {
        spdlog::warn("[PP] Texture blit not supported on this backend");
        return;
    }
    bgfx::blit(viewId, dst, 0, 0, src);
}
} // namespace Galeshi

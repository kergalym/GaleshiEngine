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
            u_cascadeSplits = bgfx::createUniform("u_cascadeSplits", bgfx::UniformType::Vec4);
            u_lightDir    = bgfx::createUniform("u_lightDirection",     bgfx::UniformType::Vec4);
            u_lightCol    = bgfx::createUniform("u_lightColorIntensity",bgfx::UniformType::Vec4);
            s_albedoMr    = bgfx::createUniform("s_albedoMr",    bgfx::UniformType::Sampler);
            s_normalRough = bgfx::createUniform("s_normalRough", bgfx::UniformType::Sampler);
            s_worldPos    = bgfx::createUniform("s_worldPos",    bgfx::UniformType::Sampler);
            s_shadowMap0  = bgfx::createUniform("s_shadowMap0",  bgfx::UniformType::Sampler);
            s_shadowMap1  = bgfx::createUniform("s_shadowMap1",  bgfx::UniformType::Sampler);
            s_shadowMap2  = bgfx::createUniform("s_shadowMap2",  bgfx::UniformType::Sampler);
            u_lightVP0    = bgfx::createUniform("u_lightViewProj0", bgfx::UniformType::Mat4);
            u_lightVP1    = bgfx::createUniform("u_lightViewProj1", bgfx::UniformType::Mat4);
            u_lightVP2    = bgfx::createUniform("u_lightViewProj2", bgfx::UniformType::Mat4);
            u_plPos       = bgfx::createUniform("u_pointLightPosRadius", bgfx::UniformType::Vec4, 8);
            spdlog::info("[PP] u_plPos valid: {}", bgfx::isValid(u_plPos));
            u_plCol       = bgfx::createUniform("u_pointLightColorInt",  bgfx::UniformType::Vec4, 8);
            u_plCnt       = bgfx::createUniform("u_pointLightCount",     bgfx::UniformType::Vec4);
            u_slPos       = bgfx::createUniform("u_spotLightPosRadius",  bgfx::UniformType::Vec4, 4);
            u_slDir       = bgfx::createUniform("u_spotLightDirInner",   bgfx::UniformType::Vec4, 4);
            u_slCol       = bgfx::createUniform("u_spotLightColorOuter", bgfx::UniformType::Vec4, 4);
            u_slCnt       = bgfx::createUniform("u_spotLightCount",      bgfx::UniformType::Vec4);
            
            s_spotShadowMap0 = bgfx::createUniform("s_spotShadowMap0", bgfx::UniformType::Sampler);
            s_spotShadowMap1 = bgfx::createUniform("s_spotShadowMap1", bgfx::UniformType::Sampler);
            s_spotShadowMap2 = bgfx::createUniform("s_spotShadowMap2", bgfx::UniformType::Sampler);
            s_spotShadowMap3 = bgfx::createUniform("s_spotShadowMap3", bgfx::UniformType::Sampler);
            u_spotLightVP0    = bgfx::createUniform("u_spotLightVP0",    bgfx::UniformType::Mat4);
            u_spotLightVP1    = bgfx::createUniform("u_spotLightVP1",    bgfx::UniformType::Mat4);
            u_spotLightVP2    = bgfx::createUniform("u_spotLightVP2",    bgfx::UniformType::Mat4);
            u_spotLightVP3    = bgfx::createUniform("u_spotLightVP3",    bgfx::UniformType::Mat4);

            for (int i = 0; i < 4; ++i) {
                m_spotShadowMaps[i] = bgfx::createTexture2D(1024, 1024, false, 1, bgfx::TextureFormat::D32F, BGFX_TEXTURE_RT);
                m_spotShadowFbos[i] = bgfx::createFrameBuffer(1, &m_spotShadowMaps[i], false);
            }
            s_pointShadowMap = bgfx::createUniform("s_pointShadowMap", bgfx::UniformType::Sampler);
            u_pointLightFarPlane = bgfx::createUniform("u_pointLightFarPlane", bgfx::UniformType::Vec4);
            
            uint64_t cubeFlags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;
            m_pointShadowMap = bgfx::createTextureCube(1024, false, 1, bgfx::TextureFormat::R32F, cubeFlags);
            for (int i = 0; i < 6; ++i) {
                bgfx::Attachment att;
                att.init(m_pointShadowMap, bgfx::Access::Write, i);
                m_pointShadowFbos[i] = bgfx::createFrameBuffer(1, &att, false);
            }
            u_pointLightFarPlane = bgfx::createUniform("u_pointLightFarPlane", bgfx::UniformType::Vec4);
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
          u_taaInvVP = bgfx::createUniform("u_taaInvViewProj", bgfx::UniformType::Mat4);
          u_taaParams = bgfx::createUniform("u_taaParams", bgfx::UniformType::Vec4);
          u_taaTexelSize = bgfx::createUniform("u_taaTexelSize", bgfx::UniformType::Vec4);
          s_taaHistory = bgfx::createUniform("s_history", bgfx::UniformType::Sampler);
          s_taaDepth = bgfx::createUniform("s_depth", bgfx::UniformType::Sampler);
      } }
    { auto vs = LoadBinShader("shaders/vs_fullscreen.bin");
      auto fs = LoadBinShader("shaders/fs_sky.bin");
      if(bgfx::isValid(vs) && bgfx::isValid(fs)) {
          m_skyProg = bgfx::createProgram(vs, fs, true);
          u_skyInvVP = bgfx::createUniform("u_skyCustomInvVP", bgfx::UniformType::Mat4);
          u_skySun = bgfx::createUniform("u_skySunDir", bgfx::UniformType::Vec4);
          spdlog::info("[PP] Sky shader loaded successfully");
      } else {
          spdlog::error("[PP] Failed to load sky shader (fs_sky.bin missing?)");
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
    
    // Tonemap & Bloom
    { auto vs = LoadBinShader("shaders/vs_fullscreen.bin");
      auto fs = LoadBinShader("shaders/fs_tonemap.bin");
      if(bgfx::isValid(vs) && bgfx::isValid(fs)) {
          m_tonemapProg = bgfx::createProgram(vs, fs, true);
          u_colorParams = bgfx::createUniform("u_colorParams", bgfx::UniformType::Vec4);
      }
      fs = LoadBinShader("shaders/fs_bloom_extract.bin");
      if(bgfx::isValid(vs) && bgfx::isValid(fs)) {
          m_bloomExtractProg = bgfx::createProgram(vs, fs, true);
          u_bloomThreshold = bgfx::createUniform("u_threshold", bgfx::UniformType::Vec4);
          u_bloomTexelSize = bgfx::createUniform("u_bloomTexelSize", bgfx::UniformType::Vec4);
          u_bloomDir = bgfx::createUniform("u_bloomDir", bgfx::UniformType::Vec4);
      }
      fs = LoadBinShader("shaders/fs_bloom_blur.bin");
      if(bgfx::isValid(vs) && bgfx::isValid(fs)) {
          m_bloomBlurProg = bgfx::createProgram(vs, fs, true);
      }
      fs = LoadBinShader("shaders/fs_bloom_combine.bin");
      if(bgfx::isValid(vs) && bgfx::isValid(fs)) {
          m_bloomCombineProg = bgfx::createProgram(vs, fs, true);
          u_bloomIntensity = bgfx::createUniform("u_bloomIntensity", bgfx::UniformType::Vec4);
      }
    }

        // IBL
    {
        auto vs = LoadBinShader("shaders/vs_fullscreen.bin");
        auto fs = LoadBinShader("shaders/fs_ibl_gen.bin");
        if (bgfx::isValid(vs) && bgfx::isValid(fs)) {
            m_iblGenProg = bgfx::createProgram(vs, fs, true);
            u_iblParams = bgfx::createUniform("u_iblParams", bgfx::UniformType::Vec4);
            u_iblLightDir = bgfx::createUniform("u_lightDirection", bgfx::UniformType::Vec4);
        }
        fs = LoadBinShader("shaders/fs_brdf.bin");
        if (bgfx::isValid(vs) && bgfx::isValid(fs)) m_brdfProg = bgfx::createProgram(vs, fs, true);

        m_envMap = bgfx::createTexture2D(1024, 512, false, 1, bgfx::TextureFormat::RGBA16F, BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        m_irradianceMap = bgfx::createTexture2D(64, 32, false, 1, bgfx::TextureFormat::RGBA16F, BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        m_brdfLut = bgfx::createTexture2D(256, 256, false, 1, bgfx::TextureFormat::RG16F, BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);

        m_envFbo = bgfx::createFrameBuffer(1, &m_envMap, false);
        m_irradianceFbo = bgfx::createFrameBuffer(1, &m_irradianceMap, false);
        m_brdfFbo = bgfx::createFrameBuffer(1, &m_brdfLut, false);

        s_iblIrradiance = bgfx::createUniform("s_irradianceMap", bgfx::UniformType::Sampler);
        s_iblEnv        = bgfx::createUniform("s_envMap",        bgfx::UniformType::Sampler);
        s_iblBrdf       = bgfx::createUniform("s_brdfLUT",       bgfx::UniformType::Sampler);
        spdlog::info("[PP] IBL uniforms: irr={} env={} brdf={}",
            bgfx::isValid(s_iblIrradiance), bgfx::isValid(s_iblEnv), bgfx::isValid(s_iblBrdf));
    }

    spdlog::info("[PP] PostProcessManager initialized");
}

void PostProcessManager::GenerateIBL(const float sunDir[4]) {
    if (!bgfx::isValid(m_iblGenProg) || !bgfx::isValid(m_brdfProg)) return;

    bgfx::setUniform(u_iblLightDir, sunDir);

    // Env Map
    bgfx::setViewFrameBuffer(30, m_envFbo);
    bgfx::setViewRect(30, 0, 0, 1024, 512);
    float p0[4] = {0.0f, 0, 0, 0};
    bgfx::setUniform(u_iblParams, p0);
    bgfx::setVertexBuffer(0, m_vbh); bgfx::setIndexBuffer(m_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(30, m_iblGenProg);

    // Irradiance Map
    bgfx::setViewFrameBuffer(31, m_irradianceFbo);
    bgfx::setViewRect(31, 0, 0, 64, 32);
    float p1[4] = {1.0f, 0, 0, 0};
    bgfx::setUniform(u_iblParams, p1);
    bgfx::setVertexBuffer(0, m_vbh); bgfx::setIndexBuffer(m_ibh);
    bgfx::submit(31, m_iblGenProg);

    // BRDF LUT
    bgfx::setViewFrameBuffer(32, m_brdfFbo);
    bgfx::setViewRect(32, 0, 0, 256, 256);
    bgfx::setVertexBuffer(0, m_vbh); bgfx::setIndexBuffer(m_ibh);
    bgfx::submit(32, m_brdfProg);
    spdlog::info("[PP] IBL generation submitted");
}

void PostProcessManager::Shutdown() {
    auto destroyProg = [](bgfx::ProgramHandle& h) { if (bgfx::isValid(h)) { bgfx::destroy(h); h = BGFX_INVALID_HANDLE; } };
    auto destroyUni  = [](bgfx::UniformHandle& h) { if (bgfx::isValid(h)) { bgfx::destroy(h); h = BGFX_INVALID_HANDLE; } };
    auto destroyTex  = [](bgfx::TextureHandle& h) { if (bgfx::isValid(h)) { bgfx::destroy(h); h = BGFX_INVALID_HANDLE; } };
    auto destroyFbo  = [](bgfx::FrameBufferHandle& h) { if (bgfx::isValid(h)) { bgfx::destroy(h); h = BGFX_INVALID_HANDLE; } };

    destroyProg(m_program); destroyProg(m_invertProg); destroyProg(m_fxaaProg);
    destroyProg(m_lightingProg); destroyProg(m_ssaoProg); destroyProg(m_ssrProg); destroyProg(m_taaProg);
    destroyUni(u_taaPrevVP); destroyUni(u_taaInvVP); destroyUni(u_taaParams); destroyUni(u_taaTexelSize); destroyUni(s_taaHistory); destroyUni(s_taaDepth);
    if (bgfx::isValid(m_historyFbo)) bgfx::destroy(m_historyFbo);
    if(bgfx::isValid(m_historyColor)) bgfx::destroy(m_historyColor);
    destroyProg(m_skyProg); destroyProg(m_iblGenProg); destroyProg(m_brdfProg);
    if (bgfx::isValid(s_iblIrradiance)) bgfx::destroy(s_iblIrradiance);
    if (bgfx::isValid(s_iblEnv))        bgfx::destroy(s_iblEnv);
    if (bgfx::isValid(s_iblBrdf))       bgfx::destroy(s_iblBrdf);
    if (bgfx::isValid(m_envFbo)) bgfx::destroy(m_envFbo); if (bgfx::isValid(m_envMap)) bgfx::destroy(m_envMap);
    if (bgfx::isValid(m_irradianceFbo)) bgfx::destroy(m_irradianceFbo); if (bgfx::isValid(m_irradianceMap)) bgfx::destroy(m_irradianceMap);
    if (bgfx::isValid(m_brdfFbo)) bgfx::destroy(m_brdfFbo); if (bgfx::isValid(m_brdfLut)) bgfx::destroy(m_brdfLut);
    destroyUni(u_skyInvVP); destroyUni(u_skySun); destroyUni(u_iblParams); destroyUni(u_iblLightDir); destroyProg(m_smaaEdgeProg); destroyProg(m_smaaBlendProg);
    

    destroyUni(s_texColor); destroyUni(u_texelSize);
    destroyUni(u_camPos); destroyUni(u_cascadeSplits); destroyUni(u_lightDir); destroyUni(u_lightCol);
    destroyUni(s_albedoMr); destroyUni(s_normalRough); destroyUni(s_worldPos);
    destroyUni(s_shadowMap0); destroyUni(s_shadowMap1); destroyUni(s_shadowMap2); destroyUni(u_lightVP0); destroyUni(u_lightVP1); destroyUni(u_lightVP2); 
    destroyUni(u_plPos); destroyUni(u_plCol); destroyUni(u_plCnt);
    destroyUni(u_slPos); destroyUni(u_slDir); destroyUni(u_slCol); destroyUni(u_slCnt); if (bgfx::isValid(m_pointShadowFbos[0])) {
        for(int i=0; i<6; ++i) { bgfx::destroy(m_pointShadowFbos[i]); }
    }
    for(int i=0; i<4; ++i) { 
        if(bgfx::isValid(m_spotShadowFbos[i])) bgfx::destroy(m_spotShadowFbos[i]); 
        if(bgfx::isValid(m_spotShadowMaps[i])) bgfx::destroy(m_spotShadowMaps[i]); 
    }
    if (bgfx::isValid(s_spotShadowMap0)) bgfx::destroy(s_spotShadowMap0);
    if (bgfx::isValid(s_spotShadowMap1)) bgfx::destroy(s_spotShadowMap1);
    if (bgfx::isValid(s_spotShadowMap2)) bgfx::destroy(s_spotShadowMap2);
    if (bgfx::isValid(s_spotShadowMap3)) bgfx::destroy(s_spotShadowMap3);
    if (bgfx::isValid(u_spotLightVP0)) bgfx::destroy(u_spotLightVP0);
    if (bgfx::isValid(u_spotLightVP1)) bgfx::destroy(u_spotLightVP1);
    if (bgfx::isValid(u_spotLightVP2)) bgfx::destroy(u_spotLightVP2);
    if (bgfx::isValid(u_spotLightVP3)) bgfx::destroy(u_spotLightVP3);
    if (bgfx::isValid(m_pointShadowMap)) bgfx::destroy(m_pointShadowMap);
    destroyUni(s_pointShadowMap); destroyUni(u_pointLightFarPlane);
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
    if (bgfx::isValid(m_smaaColorTex)) bgfx::destroy(m_smaaColorTex);
    if (bgfx::isValid(m_blitTexB))    bgfx::destroy(m_blitTexB);
    if (bgfx::isValid(m_blitDepth))   bgfx::destroy(m_blitDepth);

    const uint64_t flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    m_outputColor = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA8, flags);
    m_outputDepth = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::D32F, flags);
    bgfx::TextureHandle att[] = { m_outputColor, m_outputDepth };
    m_outputFbo = bgfx::createFrameBuffer(2, att, false);

    m_blitTexA = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA8, flags | BGFX_TEXTURE_BLIT_DST);
        m_smaaColorTex = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA8, flags | BGFX_TEXTURE_BLIT_DST);
    m_blitTexB = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA16F, flags | BGFX_TEXTURE_BLIT_DST);
    m_blitDepth = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::D32F, BGFX_TEXTURE_BLIT_DST | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    m_pingColor = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA8, flags);
    bgfx::TextureHandle pingAtt[] = { m_pingColor };
    m_pingFbo = bgfx::createFrameBuffer(1, pingAtt, false);

        if(bgfx::isValid(m_historyFbo)) bgfx::destroy(m_historyFbo);
    if(bgfx::isValid(m_historyColor)) bgfx::destroy(m_historyColor);
    m_historyColor = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA8, flags);
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
    bgfx::TextureHandle shadowMap0, bgfx::TextureHandle shadowMap1, bgfx::TextureHandle shadowMap2,
    const glm::mat4& lightMtx0, const glm::mat4& lightMtx1, const glm::mat4& lightMtx2,
    const glm::mat4& invViewProj, 
    const float cascadeSplits[4],
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
    bgfx::setUniform(u_cascadeSplits, cascadeSplits);
    bgfx::setUniform(u_lightDir, lightDir);
    bgfx::setUniform(u_lightCol, lightColorIntensity);
    bgfx::setTexture(0, s_albedoMr,    albedoMr);
    bgfx::setTexture(1, s_normalRough, normalRough);
    bgfx::setTexture(2, s_worldPos,    worldPos);
    bgfx::setTexture(3, s_shadowMap0,  shadowMap0);
    bgfx::setTexture(4, s_shadowMap1,  shadowMap1);
    bgfx::setTexture(5, s_shadowMap2,  shadowMap2);
    glm::mat4 lightMtcs[3] = { lightMtx0, lightMtx1, lightMtx2 };
    bgfx::setUniform(u_lightVP0, glm::value_ptr(lightMtx0));
    bgfx::setUniform(u_lightVP1, glm::value_ptr(lightMtx1));
    bgfx::setUniform(u_lightVP2, glm::value_ptr(lightMtx2));
    bgfx::setUniform(u_skyInvVP, glm::value_ptr(invViewProj));
    
    if (bgfx::isValid(m_irradianceMap)) bgfx::setTexture(6, s_iblIrradiance, m_irradianceMap);
    if (bgfx::isValid(m_envMap))        bgfx::setTexture(7, s_iblEnv,        m_envMap);
    if (bgfx::isValid(m_brdfLut))       bgfx::setTexture(8, s_iblBrdf,       m_brdfLut);
    

    if (pointLightCount > 0) {
        bgfx::setUniform(u_plPos, pointLightPos, 8);
        bgfx::setUniform(u_plCol, pointLightColor, 8);
    }
    float plArr[4] = { float(pointLightCount), 0, 0, 0 };
    bgfx::setUniform(u_plCnt, plArr);
    if (bgfx::isValid(m_pointShadowMap)) {
        bgfx::setTexture(9, s_pointShadowMap, m_pointShadowMap);
        float farPlane[4] = { 50.0f, 0, 0, 0 };
        bgfx::setUniform(u_pointLightFarPlane, farPlane);
    }
    if (spotLightCount > 0) {
        bgfx::setTexture(10, s_spotShadowMap0, m_spotShadowMaps[0]);
        bgfx::setTexture(11, s_spotShadowMap1, m_spotShadowMaps[1]);
        bgfx::setTexture(12, s_spotShadowMap2, m_spotShadowMaps[2]);
        bgfx::setTexture(13, s_spotShadowMap3, m_spotShadowMaps[3]);
        // VP matrices are passed in main.cpp via u_spotLightVP
    }
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
    if (!bgfx::isValid(m_ssaoProg) || !bgfx::isValid(u_ssaoParams)) return color;
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
    if (!bgfx::isValid(m_ssrProg) || !bgfx::isValid(u_ssrInvProj)) return color;
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


bgfx::TextureHandle PostProcessManager::RenderBloom(bgfx::TextureHandle color, bgfx::FrameBufferHandle target, bgfx::ViewId viewId, uint16_t w, uint16_t h, float threshold, float intensity) {
    if (!bgfx::isValid(m_bloomExtractProg) || !bgfx::isValid(m_bloomBlurProg) || !bgfx::isValid(m_bloomCombineProg)) return color;
    EnsureOutputFbo(w, h);

    // Safety check: if uniform handles are invalid, we still shouldn't call bgfx::submit with invalid programs
    if (!bgfx::isValid(m_bloomExtractProg)) return color;
    if (!bgfx::isValid(m_bloomBlurProg)) return color;
    if (!bgfx::isValid(m_bloomCombineProg)) return color;

    float ts[4] = { 1.0f/w, 1.0f/h, 0.0f, 0.0f };
    float th[4] = { threshold, 0, 0, 0 };
    float dirH[4] = { 1.0f, 0.0f, 0, 0 };
    float dirV[4] = { 0.0f, 1.0f, 0, 0 };

    // Extract
    bgfx::setViewFrameBuffer(viewId, m_blitFboA);
    bgfx::setViewRect(viewId, 0, 0, w, h);
    bgfx::setTexture(0, s_texColor, color);
    bgfx::setUniform(u_bloomThreshold, th);
    bgfx::setVertexBuffer(0, m_vbh); bgfx::setIndexBuffer(m_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(viewId, m_bloomExtractProg);

    // Blur H
    bgfx::setViewFrameBuffer(viewId+1, m_outputFbo);
    bgfx::setViewRect(viewId+1, 0, 0, w, h);
    bgfx::setTexture(0, s_texColor, m_blitTexA);
    bgfx::setUniform(u_bloomTexelSize, ts);
    bgfx::setUniform(u_bloomDir, dirH); // Note: reusing u_texelSize for dir in shader
    bgfx::setVertexBuffer(0, m_vbh); bgfx::setIndexBuffer(m_ibh);
    bgfx::submit(viewId+1, m_bloomBlurProg);

    // Blur V
    bgfx::setViewFrameBuffer(viewId+2, m_blitFboA);
    bgfx::setViewRect(viewId+2, 0, 0, w, h);
    bgfx::setTexture(0, s_texColor, m_outputColor);
    bgfx::setUniform(u_bloomTexelSize, ts);
    bgfx::setUniform(u_bloomDir, dirV);
    bgfx::setVertexBuffer(0, m_vbh); bgfx::setIndexBuffer(m_ibh);
    bgfx::submit(viewId+2, m_bloomBlurProg);

    // Combine
    bgfx::setViewFrameBuffer(viewId+3, target);
    bgfx::setViewRect(viewId+3, 0, 0, w, h);
    bgfx::setTexture(0, s_texColor, color);
    bgfx::setTexture(1, s_smaaEdges, m_blitTexA); // Reusing s_smaaEdges as s_bloom
    float bi[4] = { intensity, 0, 0, 0 };
    bgfx::setUniform(u_bloomIntensity, bi);
    bgfx::setVertexBuffer(0, m_vbh); bgfx::setIndexBuffer(m_ibh);
    bgfx::submit(viewId+3, m_bloomCombineProg);

    return m_pingColor;
}

bgfx::TextureHandle PostProcessManager::RenderTonemap(bgfx::TextureHandle color, bgfx::FrameBufferHandle target, bgfx::ViewId viewId, uint16_t w, uint16_t h, float exposure, float contrast, float saturation) {
    if (!bgfx::isValid(m_tonemapProg) || !bgfx::isValid(u_colorParams)) return color;
    bgfx::setViewFrameBuffer(viewId, target);
    bgfx::setViewRect(viewId, 0, 0, w, h);
    bgfx::setTexture(0, s_texColor, color);
    float params[4] = { exposure, contrast, saturation, 0 };
    bgfx::setUniform(u_colorParams, params);
    bgfx::setVertexBuffer(0, m_vbh); bgfx::setIndexBuffer(m_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(viewId, m_tonemapProg);
    return m_pingColor;
}

bgfx::TextureHandle PostProcessManager::RenderFxaa(bgfx::TextureHandle color, bgfx::FrameBufferHandle targetFbo, bgfx::ViewId viewId, uint16_t width, uint16_t height) {
    if (!bgfx::isValid(m_fxaaProg) || !bgfx::isValid(u_texelSize)) return color;
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
    if (!bgfx::isValid(m_smaaEdgeProg) || !bgfx::isValid(m_smaaBlendProg) || !bgfx::isValid(s_smaaEdges)) return color;
    EnsureOutputFbo(w, h);

    // View [viewId]: Edge Detection (рендерим в m_blitFboA)
    bgfx::setViewFrameBuffer(viewId, m_blitFboA);
    bgfx::setViewRect(viewId, 0, 0, w, h);
    bgfx::setTexture(0, s_texColor, color); // Сэмплируем напрямую
    bgfx::setVertexBuffer(0, m_vbh);
    bgfx::setIndexBuffer(m_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(viewId, m_smaaEdgeProg);

    // View [viewId + 1]: Blend
    bgfx::setViewFrameBuffer(viewId + 1, target);
    bgfx::setViewRect(viewId + 1, 0, 0, w, h);
    bgfx::setTexture(0, s_texColor, color); // Сэмплируем напрямую
    bgfx::setTexture(1, s_smaaEdges, m_blitTexA);
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
    if (!bgfx::isValid(m_taaProg) || !bgfx::isValid(u_taaParams)) return color;
    EnsureOutputFbo(w, h);

    if (!m_historyValid) {
        // View [viewId]: Копируем цвет в историю
        bgfx::setViewFrameBuffer(viewId, m_historyFbo);
        bgfx::setViewRect(viewId, 0, 0, w, h);
        bgfx::setTexture(0, s_texColor, color);
        bgfx::setVertexBuffer(0, m_vbh);
        bgfx::setIndexBuffer(m_ibh);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::submit(viewId, m_program);

        // View [viewId + 1]: Passthrough в target
        bgfx::setViewFrameBuffer(viewId + 1, target);
        bgfx::setViewRect(viewId + 1, 0, 0, w, h);
        bgfx::setTexture(0, s_texColor, color);
        bgfx::setVertexBuffer(0, m_vbh);
        bgfx::setIndexBuffer(m_ibh);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::submit(viewId + 1, m_program);
        
        m_historyValid = true;
        return m_pingColor;
    }

    // View [viewId]: TAA Pass
    bgfx::setViewFrameBuffer(viewId, target);
    bgfx::setViewRect(viewId, 0, 0, w, h);
    bgfx::setTexture(0, s_taaColor, color); // Сэмплируем напрямую
    bgfx::setTexture(1, s_taaHistory, m_historyColor);
    bgfx::setTexture(2, s_taaDepth, depth); // Сэмплируем глубину напрямую
    bgfx::setUniform(u_taaPrevVP, glm::value_ptr(prevVP));
    bgfx::setUniform(u_taaInvVP, glm::value_ptr(invVP));
    float params[4] = { 0.05f, 0, 0, 0 };
    bgfx::setUniform(u_taaParams, params);
    bgfx::setVertexBuffer(0, m_vbh);
    bgfx::setIndexBuffer(m_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(viewId, m_taaProg);

    // View [viewId + 1]: Копируем результат в историю
    bgfx::setViewFrameBuffer(viewId + 1, m_historyFbo);
    bgfx::setViewRect(viewId + 1, 0, 0, w, h);
    bgfx::setTexture(0, s_texColor, m_pingColor);
    bgfx::setVertexBuffer(0, m_vbh);
    bgfx::setIndexBuffer(m_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(viewId + 1, m_program);

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

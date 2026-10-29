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

    // Each post-process program needs its own vertex shader handle: bgfx
    // destroys the handles passed to createProgram() when destroyShaders is
    // true, so reusing one handle silently produces invalid programs.
    auto createFsProg = [](const char* fsPath) -> bgfx::ProgramHandle {
        auto vs = LoadBinShader("shaders/vs_fullscreen.bin");
        auto fs = LoadBinShader(fsPath);
        if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) {
            if (bgfx::isValid(vs)) bgfx::destroy(vs);
            if (bgfx::isValid(fs)) bgfx::destroy(fs);
            spdlog::error("[PP] Failed to load post-process program: {}", fsPath);
            return BGFX_INVALID_HANDLE;
        }
        return bgfx::createProgram(vs, fs, true);
    };

    // Uniforms shared by several fullscreen passes. Create the two generic
    // samplers before any program so every effect links against the same
    // handles instead of accumulating duplicate s_color/s_depth uniforms.
    s_texColor     = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
    s_texDepth     = bgfx::createUniform("s_texDepth", bgfx::UniformType::Sampler);
    u_viewportSize = bgfx::createUniform("u_viewportSize", bgfx::UniformType::Vec4);
    u_texelSize    = bgfx::createUniform("u_texelSize",    bgfx::UniformType::Vec4);

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
        }
    }

    // Lighting
    {
        auto vs = LoadBinShader("shaders/vs_fullscreen.bin");
        auto fs = LoadBinShader("shaders/fs_lighting.bin");
        if (bgfx::isValid(vs) && bgfx::isValid(fs)) {
            m_lightingProg = bgfx::createProgram(vs, fs, true);
            spdlog::info("[PP] lighting program valid: {}", bgfx::isValid(m_lightingProg));
            u_camPos      = bgfx::createUniform("u_cameraPosition",     bgfx::UniformType::Vec4);
        u_cullParams = bgfx::createUniform("u_cullParams", bgfx::UniformType::Vec4);
        u_cullViewProj = bgfx::createUniform("u_cullViewProj", bgfx::UniformType::Mat4);
        u_cullPointLightPosRadius = bgfx::createUniform("u_cullPointLightPosRadius", bgfx::UniformType::Vec4, 64);
        u_cullPointLightCount = bgfx::createUniform("u_cullPointLightCount", bgfx::UniformType::Vec4);
        u_tileParams = bgfx::createUniform("u_tileParams", bgfx::UniformType::Vec4);
        auto fsCull = LoadBinShader("shaders/cs_light_culling.bin");
        if (bgfx::isValid(fsCull)) m_lightCullingProg = bgfx::createProgram(fsCull, true);
            u_cascadeSplits = bgfx::createUniform("u_cascadeSplits", bgfx::UniformType::Vec4);
            u_lightDir    = bgfx::createUniform("u_lightDirection",     bgfx::UniformType::Vec4);
            u_lightCol    = bgfx::createUniform("u_lightColorIntensity",bgfx::UniformType::Vec4);
            s_albedoMr    = bgfx::createUniform("s_albedoMr",    bgfx::UniformType::Sampler);
            s_normalRough = bgfx::createUniform("s_normalRough", bgfx::UniformType::Sampler);
            s_worldPos    = bgfx::createUniform("s_worldPos",    bgfx::UniformType::Sampler);
            s_materialTex = bgfx::createUniform("s_materialTex", bgfx::UniformType::Sampler);
            s_shadowMap0  = bgfx::createUniform("s_shadowMap0",  bgfx::UniformType::Sampler);
            s_shadowMap1  = bgfx::createUniform("s_shadowMap1",  bgfx::UniformType::Sampler);
            s_shadowMap2  = bgfx::createUniform("s_shadowMap2",  bgfx::UniformType::Sampler);
            u_lightVP0    = bgfx::createUniform("u_lightViewProj0", bgfx::UniformType::Mat4);
            u_lightVP1    = bgfx::createUniform("u_lightViewProj1", bgfx::UniformType::Mat4);
            u_lightVP2    = bgfx::createUniform("u_lightViewProj2", bgfx::UniformType::Mat4);
            u_plPos       = bgfx::createUniform("u_pointLightPosRadius", bgfx::UniformType::Vec4, 64);
            spdlog::info("[PP] u_plPos valid: {}", bgfx::isValid(u_plPos));
            u_plCol       = bgfx::createUniform("u_pointLightColorInt",  bgfx::UniformType::Vec4, 64);
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
        }
    }

    // Ambient occlusion (ported from tobspr/RenderPipeline)
    {
        auto makeProg = [](const char* fsName) -> bgfx::ProgramHandle {
            auto vs = LoadBinShader("shaders/vs_fullscreen.bin");
            auto fs = LoadBinShader(fsName);
            if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) {
                if (bgfx::isValid(vs)) bgfx::destroy(vs);
                if (bgfx::isValid(fs)) bgfx::destroy(fs);
                return BGFX_INVALID_HANDLE;
            }
            return bgfx::createProgram(vs, fs, true);
        };

        m_aoSampleProg   = makeProg("shaders/fs_ao_sample.bin");
        m_hbaoProg       = makeProg("shaders/fs_hbao.bin");
        m_gtaoProg       = makeProg("shaders/fs_gtao.bin");
        m_aoUpsampleProg = makeProg("shaders/fs_ao_upsample.bin");
        m_aoDetailProg   = makeProg("shaders/fs_ao_detail.bin");
        m_aoBlurProg     = makeProg("shaders/fs_ao_blur.bin");

        u_aoView       = bgfx::createUniform("u_aoView",       bgfx::UniformType::Mat4);
        u_aoProj       = bgfx::createUniform("u_aoProj",       bgfx::UniformType::Mat4);
        u_aoParams     = bgfx::createUniform("u_aoParams",     bgfx::UniformType::Vec4);
        u_aoParams2    = bgfx::createUniform("u_aoParams2",    bgfx::UniformType::Vec4);
        u_aoViewport   = bgfx::createUniform("u_aoViewport",   bgfx::UniformType::Vec4);
        u_aoBlurParams = bgfx::createUniform("u_aoBlurParams", bgfx::UniformType::Vec4);
        u_aoBlurDir    = bgfx::createUniform("u_aoBlurDir",    bgfx::UniformType::Vec4);
        s_aoSource     = bgfx::createUniform("s_aoSource",     bgfx::UniformType::Sampler);
        s_aoTex        = bgfx::createUniform("s_aoTex",        bgfx::UniformType::Sampler);

        spdlog::info("[PP] AO programs: ssao={} hbao={} gtao={} upsample={} detail={} blur={}",
            bgfx::isValid(m_aoSampleProg), bgfx::isValid(m_hbaoProg),
            bgfx::isValid(m_gtaoProg), bgfx::isValid(m_aoUpsampleProg),
            bgfx::isValid(m_aoDetailProg), bgfx::isValid(m_aoBlurProg));
    }

    // SSR
    {
        auto vs = LoadBinShader("shaders/vs_fullscreen.bin");
        auto fs = LoadBinShader("shaders/fs_ssr.bin");
        if (bgfx::isValid(vs) && bgfx::isValid(fs)) {
            m_ssrProg = bgfx::createProgram(vs, fs, true);
            u_ssrInvProj = bgfx::createUniform("u_ssrInvProj", bgfx::UniformType::Mat4);
            u_ssrProj = bgfx::createUniform("u_ssrProj", bgfx::UniformType::Mat4);
            u_ssrParams = bgfx::createUniform("u_ssrParams", bgfx::UniformType::Vec4);
            s_ssrNormal  = bgfx::createUniform("s_ssrNormal",     bgfx::UniformType::Sampler);
            s_smaaEdges  = bgfx::createUniform("s_edges",         bgfx::UniformType::Sampler);
        }
    }

    { auto vs = LoadBinShader("shaders/vs_fullscreen.bin");
      auto fs = LoadBinShader("shaders/fs_taa.bin");
      if(bgfx::isValid(vs) && bgfx::isValid(fs)) {
          m_taaProg = bgfx::createProgram(vs, fs, true);
          u_taaPrevVP = bgfx::createUniform("u_prevViewProj", bgfx::UniformType::Mat4);
          u_taaInvVP = bgfx::createUniform("u_taaInvViewProj", bgfx::UniformType::Mat4);
          u_taaParams = bgfx::createUniform("u_taaParams", bgfx::UniformType::Vec4);
          u_taaTexelSize = bgfx::createUniform("u_taaTexelSize", bgfx::UniformType::Vec4);
          s_taaHistory = bgfx::createUniform("s_history", bgfx::UniformType::Sampler);
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
      m_smaaBlendProg = createFsProg("shaders/fs_smaa_blend.bin"); }

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
    
    // Tonemap, color grading and bloom all share the fullscreen vertex
    // layout, but each program owns its own shader handles.
    {
        m_tonemapProg         = createFsProg("shaders/fs_tonemap.bin");
        m_colorCorrectionProg = createFsProg("shaders/fs_color_correction.bin");
        m_vignetteProg        = createFsProg("shaders/fs_vignette.bin");
        m_chromaticProg       = createFsProg("shaders/fs_chromatic.bin");

        u_colorParams           = bgfx::createUniform("u_colorParams", bgfx::UniformType::Vec4);
        u_colorCorrectionParams = bgfx::createUniform("u_colorCorrectionParams", bgfx::UniformType::Vec4);
        u_vignetteParams        = bgfx::createUniform("u_vignetteParams", bgfx::UniformType::Vec4);
        u_chromaParams          = bgfx::createUniform("u_chromaParams", bgfx::UniformType::Vec4);

        m_bloomExtractProg = createFsProg("shaders/fs_bloom_extract.bin");
        u_bloomThreshold   = bgfx::createUniform("u_threshold", bgfx::UniformType::Vec4);
        u_bloomTexelSize   = bgfx::createUniform("u_bloomTexelSize", bgfx::UniformType::Vec4);
        u_bloomDir         = bgfx::createUniform("u_bloomDir", bgfx::UniformType::Vec4);

        m_bloomBlurProg    = createFsProg("shaders/fs_bloom_blur.bin");
        m_bloomCombineProg = createFsProg("shaders/fs_bloom_combine.bin");
        u_bloomIntensity   = bgfx::createUniform("u_bloomIntensity", bgfx::UniformType::Vec4);
        s_bloomTex         = bgfx::createUniform("s_bloom", bgfx::UniformType::Sampler);
    }

        // IBL
    {
        auto vs = LoadBinShader("shaders/vs_fullscreen.bin");
        auto fs = LoadBinShader("shaders/fs_ibl_gen.bin");
        if (bgfx::isValid(vs) && bgfx::isValid(fs)) {
            m_iblGenProg = bgfx::createProgram(vs, fs, true);
        { auto vs2 = LoadBinShader("shaders/vs_fullscreen.bin");
          auto fs2 = LoadBinShader("shaders/fs_irradiance_conv.bin");
          if (bgfx::isValid(vs2) && bgfx::isValid(fs2)) m_irradianceConvProg = bgfx::createProgram(vs2, fs2, true); }
        { auto vs3 = LoadBinShader("shaders/vs_fullscreen.bin");
          auto fs3 = LoadBinShader("shaders/fs_prefilter.bin");
          if (bgfx::isValid(vs3) && bgfx::isValid(fs3)) m_prefilterProg = bgfx::createProgram(vs3, fs3, true); }
            u_iblParams = bgfx::createUniform("u_iblParams", bgfx::UniformType::Vec4);
        u_faceParams = bgfx::createUniform("u_faceParams", bgfx::UniformType::Vec4);
        u_prefilterParams = bgfx::createUniform("u_prefilterParams", bgfx::UniformType::Vec4);
        s_envCube = bgfx::createUniform("s_envCube", bgfx::UniformType::Sampler);
            u_iblLightDir = bgfx::createUniform("u_lightDirection", bgfx::UniformType::Vec4);
        }
        m_brdfProg = createFsProg("shaders/fs_brdf.bin");

        m_envMap = bgfx::createTexture2D(1024, 512, false, 1, bgfx::TextureFormat::RGBA16F, BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        // Single mip: the sky is smooth and the prefilter samples LOD 0 explicitly,
        // so there is no undefined mip data to read.
        m_envCubeMap = bgfx::createTextureCube(256, false, 1, bgfx::TextureFormat::RGBA16F,
            BGFX_TEXTURE_BLIT_DST | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP);
        // One 2D atlas with six vertical 256x256 slots. Every face is rendered
        // into its slot and then copied into the corresponding cube face.
        m_envScratch = bgfx::createTexture2D(256, 1536, false, 1, bgfx::TextureFormat::RGBA16F,
            BGFX_TEXTURE_RT | BGFX_TEXTURE_BLIT_DST | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        m_envScratchFbo = bgfx::createFrameBuffer(1, &m_envScratch, false);
        m_irradianceCube = bgfx::createTextureCube(32, false, 1, bgfx::TextureFormat::RGBA16F,
            BGFX_TEXTURE_BLIT_DST | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP);
        m_prefilteredCube = bgfx::createTextureCube(128, true, 1, bgfx::TextureFormat::RGBA16F,
            BGFX_TEXTURE_BLIT_DST | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP);
        m_irradianceMap = bgfx::createTexture2D(64, 32, false, 1, bgfx::TextureFormat::RGBA16F, BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        m_brdfLut = bgfx::createTexture2D(256, 256, false, 1, bgfx::TextureFormat::RG16F, BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);

        m_envFbo = bgfx::createFrameBuffer(1, &m_envMap, false);
        m_irradianceFbo = bgfx::createFrameBuffer(1, &m_irradianceMap, false);
        m_brdfFbo = bgfx::createFrameBuffer(1, &m_brdfLut, false);

        s_iblIrradiance = bgfx::createUniform("s_irradianceCube", bgfx::UniformType::Sampler);
        s_iblEnv        = bgfx::createUniform("s_prefilteredCube", bgfx::UniformType::Sampler);
        s_iblBrdf       = bgfx::createUniform("s_brdfLUT",       bgfx::UniformType::Sampler);

        // Neutral fallbacks used until the first IBL generation has executed.
        // Sampling newly created textures would read undefined GPU memory.
        {
            const uint64_t zero2D[1] = { 0 };
            const uint64_t zeroCube[6] = { 0, 0, 0, 0, 0, 0 };
            m_blackTex2D = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA16F,
                BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, bgfx::copy(zero2D, sizeof(zero2D)));
            m_blackCube = bgfx::createTextureCube(1, false, 1, bgfx::TextureFormat::RGBA16F,
                BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP,
                bgfx::copy(zeroCube, sizeof(zeroCube)));

            const uint32_t whitePixel = 0xffffffffu;
            m_whiteTex2D = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8,
                BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
                bgfx::copy(&whitePixel, sizeof(whitePixel)));
        }

        // --- DoF uniforms ---
        u_dofParams    = bgfx::createUniform("u_dofParams",    bgfx::UniformType::Vec4);
        u_motionBlurParams = bgfx::createUniform("u_motionBlurParams", bgfx::UniformType::Vec4);
        u_mbVelocityParams = bgfx::createUniform("u_mbVelocityParams", bgfx::UniformType::Vec4);
        u_mbPrevVP = bgfx::createUniform("u_mbPrevVP", bgfx::UniformType::Mat4);
        u_mbInvVP  = bgfx::createUniform("u_mbInvVP",  bgfx::UniformType::Mat4);
        s_mbVelocity = bgfx::createUniform("s_mbVelocity", bgfx::UniformType::Sampler);
        u_atmoInvVP = bgfx::createUniform("u_atmoInvVP", bgfx::UniformType::Mat4);
        u_atmoSunDir = bgfx::createUniform("u_atmoSunDir", bgfx::UniformType::Vec4);
        u_atmoParams = bgfx::createUniform("u_atmoParams", bgfx::UniformType::Vec4);
        u_sunScreenPos = bgfx::createUniform("u_sunScreenPos", bgfx::UniformType::Vec4);
        u_godraysParams = bgfx::createUniform("u_godraysParams", bgfx::UniformType::Vec4);
        u_dofTexelSize = bgfx::createUniform("u_dofTexelSize", bgfx::UniformType::Vec4);
        u_dofDir       = bgfx::createUniform("u_dofDir",       bgfx::UniformType::Vec4);
        s_dofColor     = bgfx::createUniform("s_dofColor",     bgfx::UniformType::Sampler);
        s_dofDepth     = bgfx::createUniform("s_dofDepth",     bgfx::UniformType::Sampler);
        s_dofBlur      = bgfx::createUniform("s_dofBlur",      bgfx::UniformType::Sampler);
        s_dofCoc       = bgfx::createUniform("s_dofCoc",       bgfx::UniformType::Sampler);
        spdlog::info("[PP] IBL uniforms: irr={} env={} brdf={}",
            bgfx::isValid(s_iblIrradiance), bgfx::isValid(s_iblEnv), bgfx::isValid(s_iblBrdf));
    }

    // --- DoF and screen-space effects ---
    {
        m_dofCocProg       = createFsProg("shaders/fs_dof_coc.bin");
        m_dofBlurProg      = createFsProg("shaders/fs_dof_blur.bin");
        m_dofCompositeProg = createFsProg("shaders/fs_dof_composite.bin");
        m_motionBlurProg   = createFsProg("shaders/fs_motion_blur.bin");
        m_mbVelocityProg   = createFsProg("shaders/fs_motion_blur_velocity.bin");
        m_atmosphereProg   = createFsProg("shaders/fs_atmosphere.bin");
        m_godraysProg      = createFsProg("shaders/fs_godrays.bin");

        if (!bgfx::isValid(m_dofCocProg) || !bgfx::isValid(m_dofBlurProg) || !bgfx::isValid(m_dofCompositeProg))
            spdlog::warn("[PP] DoF shaders not all loaded — DoF disabled");
        spdlog::info("[PP] effects: dof={}/{}/{} mb={}/{} atmo={} godrays={}",
            bgfx::isValid(m_dofCocProg), bgfx::isValid(m_dofBlurProg), bgfx::isValid(m_dofCompositeProg),
            bgfx::isValid(m_motionBlurProg), bgfx::isValid(m_mbVelocityProg),
            bgfx::isValid(m_atmosphereProg), bgfx::isValid(m_godraysProg));
    }

    spdlog::info("[PP] programs: ssr={} smaa={}/{} bloom={}/{}/{} tonemap={} color={} vig={} chroma={} fxaa={} taa={} sky={} ibl={}/{}/{} brdf={}",
        bgfx::isValid(m_ssrProg), bgfx::isValid(m_smaaEdgeProg), bgfx::isValid(m_smaaBlendProg),
        bgfx::isValid(m_bloomExtractProg), bgfx::isValid(m_bloomBlurProg), bgfx::isValid(m_bloomCombineProg),
        bgfx::isValid(m_tonemapProg), bgfx::isValid(m_colorCorrectionProg), bgfx::isValid(m_vignetteProg),
        bgfx::isValid(m_chromaticProg), bgfx::isValid(m_fxaaProg), bgfx::isValid(m_taaProg),
        bgfx::isValid(m_skyProg), bgfx::isValid(m_iblGenProg), bgfx::isValid(m_irradianceConvProg),
        bgfx::isValid(m_prefilterProg), bgfx::isValid(m_brdfProg));

    spdlog::info("[PP] PostProcessManager initialized");
}

void PostProcessManager::GenerateIBL(const float sunDir[4]) {
    if (!bgfx::isValid(m_envCubeMap) || !bgfx::isValid(m_envScratchFbo)) return;
    if (!bgfx::isValid(m_iblGenProg)) {
        spdlog::error("[PP] GenerateIBL: fs_ibl_gen is missing");
        return;
    }

    const float zero4[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    // --- BRDF LUT (view-independent, generated once) -----------------------
    if (!m_brdfLutGenerated && bgfx::isValid(m_brdfProg) && bgfx::isValid(m_brdfFbo)) {
        const bgfx::ViewId v = 190;
        bgfx::setViewFrameBuffer(v, m_brdfFbo);
        bgfx::setViewRect(v, 0, 0, 256, 256);
        float vpSize[4] = { 256.0f, 256.0f, 1.0f / 256.0f, 1.0f / 256.0f };
        bgfx::setUniform(u_viewportSize, vpSize);
        bgfx::setVertexBuffer(0, m_vbh);
        bgfx::setIndexBuffer(m_ibh);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::submit(v, m_brdfProg);
        m_brdfLutGenerated = true;
    }

    // --- Procedural environment cubemap -----------------------------------
    // 6 faces are rendered into the 2D atlas (one 256x256 slot per face), so a
    // texture is never sampled in the same view where it is an attachment.
    for (int face = 0; face < 6; ++face) {
        const bgfx::ViewId v = (bgfx::ViewId)(200 + face);
        bgfx::setViewFrameBuffer(v, m_envScratchFbo);
        bgfx::setViewRect(v, 0, (uint16_t)(face * 256), 256, 256);
        float fp[4] = { 256.0f, (float)face, 0.0f, 0.0f };
        bgfx::setUniform(u_faceParams, fp);
        bgfx::setUniform(u_iblParams, zero4);
        bgfx::setUniform(u_iblLightDir, sunDir);
        bgfx::setVertexBuffer(0, m_vbh);
        bgfx::setIndexBuffer(m_ibh);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::submit(v, m_iblGenProg);
    }
    // All six faces are ready before view 206, therefore all copies can share
    // a single blit view (blits are executed before that view's draws).
    for (int face = 0; face < 6; ++face) {
        bgfx::blit(206, m_envCubeMap, 0, 0, 0, (uint16_t)face,
                   m_envScratch, 0, 0, (uint16_t)(face * 256), 0, 256, 256, 1);
    }

    // --- Diffuse irradiance ------------------------------------------------
    if (bgfx::isValid(m_irradianceConvProg) && bgfx::isValid(m_irradianceCube)) {
        for (int face = 0; face < 6; ++face) {
            const bgfx::ViewId v = (bgfx::ViewId)(210 + face);
            bgfx::setViewFrameBuffer(v, m_envScratchFbo);
            bgfx::setViewRect(v, 0, (uint16_t)(face * 256), 32, 32);
            bgfx::setTexture(0, s_envCube, m_envCubeMap);
            float fp[4] = { 32.0f, (float)face, 0.0f, 0.0f };
            bgfx::setUniform(u_faceParams, fp);
            bgfx::setVertexBuffer(0, m_vbh);
            bgfx::setIndexBuffer(m_ibh);
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
            bgfx::submit(v, m_irradianceConvProg);
        }
        for (int face = 0; face < 6; ++face) {
            bgfx::blit(216, m_irradianceCube, 0, 0, 0, (uint16_t)face,
                       m_envScratch, 0, 0, (uint16_t)(face * 256), 0, 32, 32, 1);
        }
    } else {
        spdlog::error("[PP] GenerateIBL: irradiance convolution program is missing");
    }

    // --- GGX prefiltered specular chain -----------------------------------
    if (bgfx::isValid(m_prefilterProg) && bgfx::isValid(m_prefilteredCube)) {
        for (int mip = 0; mip < 5; ++mip) {            const uint16_t mipSize = (uint16_t)(128 >> mip);
            const float roughness = (float)mip / 4.0f;
            const bgfx::ViewId firstRender = (bgfx::ViewId)(220 + mip * 7);
            const bgfx::ViewId blitView    = (bgfx::ViewId)(firstRender + 6);

            for (int face = 0; face < 6; ++face) {
                const bgfx::ViewId v = (bgfx::ViewId)(firstRender + face);
                bgfx::setViewFrameBuffer(v, m_envScratchFbo);
                bgfx::setViewRect(v, 0, (uint16_t)(face * 256), mipSize, mipSize);
                bgfx::setTexture(0, s_envCube, m_envCubeMap);
                float fp[4] = { (float)mipSize, (float)face, 0.0f, 0.0f };
                float pp[4] = { roughness, 128.0f, 0.0f, 0.0f };
                bgfx::setUniform(u_faceParams, fp);
                bgfx::setUniform(u_prefilterParams, pp);
                bgfx::setVertexBuffer(0, m_vbh);
                bgfx::setIndexBuffer(m_ibh);
                bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
                bgfx::submit(v, m_prefilterProg);
            }
            for (int face = 0; face < 6; ++face) {
                bgfx::blit(blitView, m_prefilteredCube, (uint8_t)mip, 0, 0, (uint16_t)face,
                           m_envScratch, 0, 0, (uint16_t)(face * 256), 0, mipSize, mipSize, 1);
            }
        }
    } else {
        spdlog::error("[PP] GenerateIBL: prefilter program is missing");
    }

    m_iblValid = bgfx::isValid(m_irradianceConvProg) && bgfx::isValid(m_prefilterProg);
}

void PostProcessManager::Shutdown() {
    auto destroyProg = [](bgfx::ProgramHandle& h) { if (bgfx::isValid(h)) { bgfx::destroy(h); h = BGFX_INVALID_HANDLE; } };
    auto destroyUni  = [](bgfx::UniformHandle& h) { if (bgfx::isValid(h)) { bgfx::destroy(h); h = BGFX_INVALID_HANDLE; } };
    auto destroyTex  = [](bgfx::TextureHandle& h) { if (bgfx::isValid(h)) { bgfx::destroy(h); h = BGFX_INVALID_HANDLE; } };
    auto destroyFbo  = [](bgfx::FrameBufferHandle& h) { if (bgfx::isValid(h)) { bgfx::destroy(h); h = BGFX_INVALID_HANDLE; } };

    destroyProg(m_program); destroyProg(m_invertProg); destroyProg(m_fxaaProg);
    destroyProg(m_lightingProg);
    destroyProg(m_lightCullingProg);
    if (bgfx::isValid(m_tileLightCountBuffer)) bgfx::destroy(m_tileLightCountBuffer);
    if (bgfx::isValid(m_tileLightIndexBuffer)) bgfx::destroy(m_tileLightIndexBuffer);
    destroyUni(u_cullParams); destroyUni(u_cullViewProj);
    destroyUni(u_cullPointLightPosRadius); destroyUni(u_cullPointLightCount); destroyUni(u_tileParams);
    destroyProg(m_aoSampleProg); destroyProg(m_hbaoProg); destroyProg(m_gtaoProg);
    destroyProg(m_aoUpsampleProg);
    destroyProg(m_aoDetailProg); destroyProg(m_aoBlurProg);
    destroyProg(m_ssrProg); destroyProg(m_taaProg);
    destroyProg(m_dofCocProg); destroyProg(m_dofBlurProg); destroyProg(m_dofCompositeProg);
    destroyProg(m_motionBlurProg);
    destroyProg(m_mbVelocityProg);
    destroyProg(m_atmosphereProg);
    destroyProg(m_godraysProg);
    destroyUni(u_sunScreenPos); destroyUni(u_godraysParams);
    destroyUni(u_atmoInvVP); destroyUni(u_atmoSunDir); destroyUni(u_atmoParams);
    destroyUni(u_mbPrevVP); destroyUni(u_mbInvVP);
    destroyUni(u_motionBlurParams); destroyUni(u_mbVelocityParams); destroyUni(s_mbVelocity);
    if (bgfx::isValid(m_mbVelocityFbo)) bgfx::destroy(m_mbVelocityFbo);
    if (bgfx::isValid(m_mbVelocityTex)) bgfx::destroy(m_mbVelocityTex);
    destroyUni(u_dofParams); destroyUni(u_dofTexelSize); destroyUni(u_dofDir);
    destroyUni(s_dofColor); destroyUni(s_dofDepth); destroyUni(s_dofBlur); destroyUni(s_dofCoc);
    if (bgfx::isValid(m_dofCocFbo))   bgfx::destroy(m_dofCocFbo);
    if (bgfx::isValid(m_dofBlurFboA)) bgfx::destroy(m_dofBlurFboA);
    if (bgfx::isValid(m_dofBlurFboB)) bgfx::destroy(m_dofBlurFboB);
    if (bgfx::isValid(m_dofCocTex))   bgfx::destroy(m_dofCocTex);
    if (bgfx::isValid(m_dofBlurTexA)) bgfx::destroy(m_dofBlurTexA);
    if (bgfx::isValid(m_dofBlurTexB)) bgfx::destroy(m_dofBlurTexB);
    destroyUni(u_taaPrevVP); destroyUni(u_taaInvVP); destroyUni(u_taaParams); destroyUni(u_taaTexelSize); destroyUni(s_taaHistory);
    if (bgfx::isValid(m_historyFbo)) bgfx::destroy(m_historyFbo);
    if(bgfx::isValid(m_historyColor)) bgfx::destroy(m_historyColor);
    destroyProg(m_skyProg); destroyProg(m_iblGenProg); destroyProg(m_brdfProg);
    destroyProg(m_irradianceConvProg); destroyProg(m_prefilterProg);
    destroyProg(m_tonemapProg); destroyProg(m_bloomExtractProg);
    destroyProg(m_bloomBlurProg); destroyProg(m_bloomCombineProg);
    destroyProg(m_colorCorrectionProg); destroyProg(m_vignetteProg); destroyProg(m_chromaticProg);
    destroyUni(u_colorParams); destroyUni(u_colorCorrectionParams);
    destroyUni(u_vignetteParams); destroyUni(u_chromaParams);
    destroyUni(u_bloomThreshold); destroyUni(u_bloomTexelSize); destroyUni(u_bloomDir);
    destroyUni(u_bloomIntensity); destroyUni(s_bloomTex);
    destroyTex(m_bloomTexA); destroyTex(m_bloomTexB);
    destroyFbo(m_bloomFboA); destroyFbo(m_bloomFboB);
    if (bgfx::isValid(s_iblIrradiance)) bgfx::destroy(s_iblIrradiance);
    if (bgfx::isValid(s_iblEnv))        bgfx::destroy(s_iblEnv);
    if (bgfx::isValid(s_iblBrdf))       bgfx::destroy(s_iblBrdf);
    if (bgfx::isValid(m_envFbo)) bgfx::destroy(m_envFbo); if (bgfx::isValid(m_envMap)) bgfx::destroy(m_envMap);
    if (bgfx::isValid(m_irradianceFbo)) bgfx::destroy(m_irradianceFbo); if (bgfx::isValid(m_irradianceMap)) bgfx::destroy(m_irradianceMap);
    if (bgfx::isValid(m_brdfFbo)) bgfx::destroy(m_brdfFbo); if (bgfx::isValid(m_brdfLut)) bgfx::destroy(m_brdfLut);
    destroyTex(m_envCubeMap); destroyTex(m_irradianceCube); destroyTex(m_prefilteredCube);
    destroyTex(m_blackCube); destroyTex(m_blackTex2D); destroyTex(m_whiteTex2D);
    destroyTex(m_envScratch); destroyFbo(m_envScratchFbo);
    destroyTex(m_irradianceScratch); destroyFbo(m_irradianceScratchFbo);
    destroyTex(m_prefilterScratch); destroyFbo(m_prefilterScratchFbo);
    destroyUni(u_skyInvVP); destroyUni(u_skySun); destroyUni(u_iblParams); destroyUni(u_iblLightDir);
    destroyUni(u_faceParams); destroyUni(u_prefilterParams); destroyUni(s_envCube);
    destroyUni(u_viewportSize);
    destroyProg(m_smaaEdgeProg); destroyProg(m_smaaBlendProg);
    

    destroyUni(s_texColor); destroyUni(s_texDepth); destroyUni(u_texelSize);
    destroyUni(u_camPos); destroyUni(u_cascadeSplits); destroyUni(u_lightDir); destroyUni(u_lightCol);
    destroyUni(s_albedoMr); destroyUni(s_normalRough); destroyUni(s_worldPos); destroyUni(s_materialTex);
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
    destroyUni(u_aoView); destroyUni(u_aoProj); destroyUni(u_aoParams);
    destroyUni(u_aoParams2);
    destroyUni(u_aoViewport); destroyUni(u_aoBlurParams); destroyUni(u_aoBlurDir);
    destroyUni(s_aoSource); destroyUni(s_aoTex);
    destroyTex(m_aoHalfTex); destroyTex(m_aoFullTexA); destroyTex(m_aoFullTexB);
    destroyFbo(m_aoHalfFbo); destroyFbo(m_aoFullFboA); destroyFbo(m_aoFullFboB);
    destroyUni(u_ssrInvProj); destroyUni(u_ssrProj); destroyUni(u_ssrParams); destroyUni(s_ssrNormal);
    destroyUni(s_smaaEdges);
    if (bgfx::isValid(m_blitFboA)) { bgfx::destroy(m_blitFboA); m_blitFboA = BGFX_INVALID_HANDLE; }
    destroyTex(m_blitTexA); destroyTex(m_blitTexB);
    destroyTex(m_smaaColorTex); destroyFbo(m_smaaColorFbo);
    destroyTex(m_smaaEdgeTex); destroyFbo(m_smaaEdgeFbo);

    if (bgfx::isValid(m_vbh)) { bgfx::destroy(m_vbh); m_vbh = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(m_ibh)) { bgfx::destroy(m_ibh); m_ibh = BGFX_INVALID_HANDLE; }

    destroyTex(m_outputColor); destroyTex(m_outputDepth); destroyFbo(m_outputFbo);
    destroyTex(m_pingColor); destroyFbo(m_pingFbo);
    destroyTex(m_blitDepth);
    m_width = m_height = 0;
}

void PostProcessManager::EnsureAOFbo(uint16_t w, uint16_t h) {
    if (bgfx::isValid(m_aoHalfFbo) && m_aoWidth == w && m_aoHeight == h) return;

    // The AO FBO that was bound in the previous frame is about to be
    // destroyed. Reset bgfx's current framebuffer to the backbuffer first so
    // a recycled handle index cannot suppress the next layout transition.
    bgfx::setViewFrameBuffer(0, BGFX_INVALID_HANDLE);
    bgfx::setViewRect(0, 0, 0, 1, 1);
    bgfx::setViewClear(0, BGFX_CLEAR_NONE);
    bgfx::touch(0);

    if (bgfx::isValid(m_aoHalfFbo))   bgfx::destroy(m_aoHalfFbo);
    if (bgfx::isValid(m_aoFullFboA))  bgfx::destroy(m_aoFullFboA);
    if (bgfx::isValid(m_aoFullFboB))  bgfx::destroy(m_aoFullFboB);
    if (bgfx::isValid(m_aoHalfTex))   bgfx::destroy(m_aoHalfTex);
    if (bgfx::isValid(m_aoFullTexA))  bgfx::destroy(m_aoFullTexA);
    if (bgfx::isValid(m_aoFullTexB))  bgfx::destroy(m_aoFullTexB);

    m_aoHalfFbo = m_aoFullFboA = m_aoFullFboB = BGFX_INVALID_HANDLE;
    m_aoHalfTex = m_aoFullTexA = m_aoFullTexB = BGFX_INVALID_HANDLE;
    m_aoValid = false;

    const uint16_t halfW = w > 2 ? (uint16_t)(w / 2) : 1;
    const uint16_t halfH = h > 2 ? (uint16_t)(h / 2) : 1;
    const uint64_t flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

    m_aoHalfTex  = bgfx::createTexture2D(halfW, halfH, false, 1, bgfx::TextureFormat::RGBA8, flags);
    m_aoFullTexA = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA8, flags);
    m_aoFullTexB = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA8, flags);

    if (bgfx::isValid(m_aoHalfTex))  m_aoHalfFbo  = bgfx::createFrameBuffer(1, &m_aoHalfTex, false);
    if (bgfx::isValid(m_aoFullTexA)) m_aoFullFboA = bgfx::createFrameBuffer(1, &m_aoFullTexA, false);
    if (bgfx::isValid(m_aoFullTexB)) m_aoFullFboB = bgfx::createFrameBuffer(1, &m_aoFullTexB, false);

    m_aoWidth = w;
    m_aoHeight = h;

    spdlog::info("[AO] buffers {}x{} (half {}x{}), fbos half={} fullA={} fullB={}",
        w, h, halfW, halfH,
        bgfx::isValid(m_aoHalfFbo), bgfx::isValid(m_aoFullFboA), bgfx::isValid(m_aoFullFboB));
}

void PostProcessManager::EnsureOutputFbo(uint16_t w, uint16_t h) {
    if (bgfx::isValid(m_outputFbo) && m_width == w && m_height == h) return;
    if (bgfx::isValid(m_outputFbo))   bgfx::destroy(m_outputFbo);
    if (bgfx::isValid(m_outputColor)) bgfx::destroy(m_outputColor);
    if (bgfx::isValid(m_outputDepth)) bgfx::destroy(m_outputDepth);
    if (bgfx::isValid(m_pingFbo))     bgfx::destroy(m_pingFbo);
    if (bgfx::isValid(m_pingColor))   bgfx::destroy(m_pingColor);
    if (bgfx::isValid(m_mbVelocityFbo)) bgfx::destroy(m_mbVelocityFbo);
    if (bgfx::isValid(m_mbVelocityTex)) bgfx::destroy(m_mbVelocityTex);
        if (bgfx::isValid(m_blitTexA))    bgfx::destroy(m_blitTexA);
    if (bgfx::isValid(m_smaaColorTex)) bgfx::destroy(m_smaaColorTex);
    if (bgfx::isValid(m_blitTexB))    bgfx::destroy(m_blitTexB);
    if (bgfx::isValid(m_blitDepth))   bgfx::destroy(m_blitDepth);

    const uint64_t flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    // The whole post-processing chain works in linear HDR; tonemapping and
    // gamma conversion happen at the very end.
    // Both swap-chain color textures need BGFX_TEXTURE_BLIT_DST: SwapFbos()
    // exchanges them, and either one can become the overlay copy destination.
    m_outputColor = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA16F, flags | BGFX_TEXTURE_BLIT_DST);
    m_outputDepth = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::D32F, flags);
    bgfx::TextureHandle att[] = { m_outputColor, m_outputDepth };
    m_outputFbo = bgfx::createFrameBuffer(2, att, false);

    m_blitTexA = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA16F, flags | BGFX_TEXTURE_BLIT_DST);
        m_smaaColorTex = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA16F, flags | BGFX_TEXTURE_BLIT_DST);
    m_blitTexB = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA16F, flags | BGFX_TEXTURE_BLIT_DST);
    m_blitDepth = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::D32F, BGFX_TEXTURE_BLIT_DST | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    // The ping buffer also serves as the transparent/debug overlay target at
    // the end of the frame; BGFX_TEXTURE_BLIT_DST is required for the copy of
    // the base image into it.
    m_pingColor = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA16F, flags | BGFX_TEXTURE_BLIT_DST);
    bgfx::TextureHandle pingAtt[] = { m_pingColor };
    m_pingFbo = bgfx::createFrameBuffer(1, pingAtt, false);

    // --- Motion blur velocity (full resolution) ---
    m_mbVelocityTex = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA16F,
        flags | BGFX_TEXTURE_BLIT_DST);
    m_mbVelocityFbo = bgfx::createFrameBuffer(1, &m_mbVelocityTex, false);

    // --- Bloom (half resolution) ---
    const uint16_t bw = (uint16_t)glm::max(1u, (unsigned)(w / 2));
    const uint16_t bh = (uint16_t)glm::max(1u, (unsigned)(h / 2));
    if (bgfx::isValid(m_bloomFboA)) bgfx::destroy(m_bloomFboA);
    if (bgfx::isValid(m_bloomFboB)) bgfx::destroy(m_bloomFboB);
    if (bgfx::isValid(m_bloomTexA)) bgfx::destroy(m_bloomTexA);
    if (bgfx::isValid(m_bloomTexB)) bgfx::destroy(m_bloomTexB);
    m_bloomTexA = bgfx::createTexture2D(bw, bh, false, 1, bgfx::TextureFormat::RGBA16F, flags);
    m_bloomTexB = bgfx::createTexture2D(bw, bh, false, 1, bgfx::TextureFormat::RGBA16F, flags);
    m_bloomFboA = bgfx::createFrameBuffer(1, &m_bloomTexA, false);
    m_bloomFboB = bgfx::createFrameBuffer(1, &m_bloomTexB, false);

    // --- DoF textures / FBOs (пересоздаём при resize) ---
    if (bgfx::isValid(m_dofCocFbo))    bgfx::destroy(m_dofCocFbo);
    if (bgfx::isValid(m_dofBlurFboA))  bgfx::destroy(m_dofBlurFboA);
    if (bgfx::isValid(m_dofBlurFboB))  bgfx::destroy(m_dofBlurFboB);
    if (bgfx::isValid(m_dofCocTex))    bgfx::destroy(m_dofCocTex);
    if (bgfx::isValid(m_dofBlurTexA))  bgfx::destroy(m_dofBlurTexA);
    if (bgfx::isValid(m_dofBlurTexB))  bgfx::destroy(m_dofBlurTexB);
    m_dofCocTex   = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::R16F,
        BGFX_TEXTURE_RT | BGFX_TEXTURE_BLIT_DST | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    m_dofBlurTexA = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA16F,
        BGFX_TEXTURE_RT | BGFX_TEXTURE_BLIT_DST | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    m_dofBlurTexB = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA16F,
        BGFX_TEXTURE_RT | BGFX_TEXTURE_BLIT_DST | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    m_dofCocFbo   = bgfx::createFrameBuffer(1, &m_dofCocTex,   false);
    m_dofBlurFboA = bgfx::createFrameBuffer(1, &m_dofBlurTexA, false);
    m_dofBlurFboB = bgfx::createFrameBuffer(1, &m_dofBlurTexB, false);

        if(bgfx::isValid(m_historyFbo)) bgfx::destroy(m_historyFbo);
    if(bgfx::isValid(m_historyColor)) bgfx::destroy(m_historyColor);
    // History is accumulated in HDR; using RGBA8 here clipped bright pixels
    // and made TAA darken highlights over time.
    m_historyColor = bgfx::createTexture2D(
        w, h, false, 1, bgfx::TextureFormat::RGBA16F, flags);
    m_historyFbo = bgfx::createFrameBuffer(1, &m_historyColor, false);
        m_historyValid = false;
    if (bgfx::isValid(m_blitFboA)) bgfx::destroy(m_blitFboA);
    m_blitFboA = bgfx::createFrameBuffer(1, &m_blitTexA, false);
    m_width = w;
    m_height = h;
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

void PostProcessManager::RenderLightCulling(int pointLightCount, const float pointLightPosRadius[][4], const glm::mat4& viewProj, uint16_t width, uint16_t height) {
    if (!bgfx::isValid(m_lightCullingProg)) return;

    uint16_t tilesX = (width + 15) / 16;
    uint16_t tilesY = (height + 15) / 16;
    uint32_t totalTiles = tilesX * tilesY;

    if (!bgfx::isValid(m_tileLightCountBuffer) || totalTiles != m_numCullTiles) {
        if (bgfx::isValid(m_tileLightCountBuffer)) bgfx::destroy(m_tileLightCountBuffer);
        if (bgfx::isValid(m_tileLightIndexBuffer)) bgfx::destroy(m_tileLightIndexBuffer);
        m_tileLightCountBuffer = bgfx::createDynamicIndexBuffer(totalTiles, BGFX_BUFFER_INDEX32 | BGFX_BUFFER_COMPUTE_READ_WRITE);
        m_tileLightIndexBuffer = bgfx::createDynamicIndexBuffer(totalTiles * 64, BGFX_BUFFER_INDEX32 | BGFX_BUFFER_COMPUTE_READ_WRITE);
        m_numCullTiles = totalTiles;
    }

    float params[4] = { (float)tilesX, (float)tilesY, (float)width, (float)height };
    bgfx::setUniform(u_cullParams, params);
    bgfx::setUniform(u_cullViewProj, glm::value_ptr(viewProj));
    float countArr[4] = { (float)pointLightCount, 0, 0, 0 };
    bgfx::setUniform(u_cullPointLightCount, countArr);
    bgfx::setUniform(u_cullPointLightPosRadius, pointLightPosRadius, 64);

    bgfx::setBuffer(0, m_tileLightCountBuffer, bgfx::Access::Write);
    bgfx::setBuffer(1, m_tileLightIndexBuffer, bgfx::Access::Write);

    bgfx::dispatch(19, m_lightCullingProg, tilesX, tilesY, 1);
}

bgfx::TextureHandle PostProcessManager::RenderLighting(
    bgfx::TextureHandle albedoMr, bgfx::TextureHandle normalRough, bgfx::TextureHandle worldPos,
    bgfx::TextureHandle materialData,
    bgfx::TextureHandle shadowMap0, bgfx::TextureHandle shadowMap1, bgfx::TextureHandle shadowMap2,
    const glm::mat4& lightMtx0, const glm::mat4& lightMtx1, const glm::mat4& lightMtx2,
    const glm::mat4& invViewProj, 
    const float cascadeSplits[4],
    const float camPos[4], const float lightDir[4], const float lightColorIntensity[4],
    const float pointLightPos[][4], const float pointLightColor[][4], int pointLightCount,
    const float spotLightPosRadius[][4], const float spotLightDirInner[][4], const float spotLightColorOuter[][4], int spotLightCount,
    bgfx::ViewId viewId, uint16_t width, uint16_t height)
{
    if (!bgfx::isValid(m_lightingProg) || width == 0 || height == 0) {
        static bool s_logged = false;
        if (!s_logged) {
            spdlog::error("[PP] RenderLighting skipped: prog={} width={} height={}",
                bgfx::isValid(m_lightingProg), width, height);
            s_logged = true;
        }
        return BGFX_INVALID_HANDLE;
    }
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
    bgfx::setUniform(u_lightVP0, glm::value_ptr(lightMtcs[0]));
    bgfx::setUniform(u_lightVP1, glm::value_ptr(lightMtcs[1]));
    bgfx::setUniform(u_lightVP2, glm::value_ptr(lightMtcs[2]));
    bgfx::setUniform(u_skyInvVP, glm::value_ptr(invViewProj));
    
    const bgfx::TextureHandle irrTex  = m_iblValid ? m_irradianceCube  : m_blackCube;
    const bgfx::TextureHandle envTex  = m_iblValid ? m_prefilteredCube : m_blackCube;
    if (bgfx::isValid(irrTex))  bgfx::setTexture(6, s_iblIrradiance, irrTex);
    if (bgfx::isValid(envTex))  bgfx::setTexture(7, s_iblEnv,        envTex);

    // Ambient occlusion at slot 8. The split-sum BRDF LUT used this slot
    // before; it is now evaluated analytically, because AO cannot share the
    // tile-light SSBO binding at slot 14.
    const bgfx::TextureHandle aoTex = AOTexture();
    if (bgfx::isValid(s_aoTex) && bgfx::isValid(aoTex)) {
        bgfx::setTexture(8, s_aoTex, aoTex);
    }

    // Material G-buffer: emissive.rgb + AO, plus specular mask in worldPos.w.
    if (bgfx::isValid(s_materialTex) && bgfx::isValid(materialData)) {
        bgfx::setTexture(9, s_materialTex, materialData);
    }
    

    if (pointLightCount > 0) {
        bgfx::setUniform(u_plPos, pointLightPos, 64);
        bgfx::setUniform(u_plCol, pointLightColor, 64);
    }
    float plArr[4] = { float(pointLightCount), 0, 0, 0 };
    bgfx::setUniform(u_plCnt, plArr);
    if (bgfx::isValid(m_tileLightCountBuffer)) {
        bgfx::setBuffer(14, m_tileLightCountBuffer, bgfx::Access::Read);
        bgfx::setBuffer(15, m_tileLightIndexBuffer, bgfx::Access::Read);
        float tp[4] = { (float)((width + 15) / 16), (float)((height + 15) / 16), (float)width, (float)height };
        bgfx::setUniform(u_tileParams, tp);
    }
    // Point shadows are not sampled by fs_lighting yet; slot 9 is used by the
    // material G-buffer texture. The resource is kept alive for later.
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

void PostProcessManager::RenderAmbientOcclusion(bgfx::TextureHandle worldPos,
                                                bgfx::TextureHandle normal,
                                                const glm::mat4& view,
                                                const glm::mat4& proj,
                                                bgfx::ViewId viewId,
                                                uint16_t width, uint16_t height,
                                                const AmbientOcclusionParams& params)
{
    if (!params.enabled) {
        m_aoValid = false;
        return;
    }

    int mode = params.mode;
    if (mode < 0 || mode > 2) mode = 0;

    bgfx::ProgramHandle sampleProg = m_aoSampleProg;
    if (mode == 1) sampleProg = m_hbaoProg;
    else if (mode == 2) sampleProg = m_gtaoProg;
    // Keep ambient occlusion functional if an optional shader is missing.
    if (!bgfx::isValid(sampleProg)) sampleProg = m_aoSampleProg;

    if (!bgfx::isValid(sampleProg) || !bgfx::isValid(m_aoUpsampleProg) ||
        !bgfx::isValid(m_aoBlurProg) || width == 0 || height == 0) {
        m_aoValid = false;
        return;
    }

    EnsureAOFbo(width, height);
    if (!bgfx::isValid(m_aoHalfFbo) || !bgfx::isValid(m_aoFullFboA) || !bgfx::isValid(m_aoFullFboB)) {
        m_aoValid = false;
        return;
    }

    const uint16_t halfW = width > 2 ? (uint16_t)(width / 2) : 1;
    const uint16_t halfH = height > 2 ? (uint16_t)(height / 2) : 1;

    int directions = params.directions < 1 ? 1 : params.directions;
    if (directions > 16) directions = 16;
    int steps = params.steps < 1 ? 1 : params.steps;
    if (steps > 16) steps = 16;

    float viewport[4] = { (float)width, (float)height, 1.0f / width, 1.0f / height };
    float aoParams[4] = { std::max(params.radius, 0.0f), std::max(params.bias, 0.0f),
                          std::max(params.maxDistance, 0.0f), std::max(params.strength, 0.0f) };
    // x = directions/slices, y = steps, z = GTAO falloff start,
    // w = GTAO thickness (view-space depth rejection).
    float aoParams2[4] = { (float)directions, (float)steps, 0.75f,
                           std::max(params.thickness, 0.001f) };
    float upsampleParams[4] = { 0.5f, 32.0f, 0.0f, 0.0f };
    float blurParams[4] = { 1.0f, 0.2f, 0.0f, 0.0f };
    float blurDirH[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
    float blurDirV[4] = { 0.0f, 1.0f, 0.0f, 0.0f };

    // View viewId + 0: half-resolution ambient occlusion (SSAO/HBAO/GTAO).
    bgfx::setViewFrameBuffer(viewId, m_aoHalfFbo);
    bgfx::setViewRect(viewId, 0, 0, halfW, halfH);
    bgfx::setViewClear(viewId, BGFX_CLEAR_COLOR, 0xffffffff);
    bgfx::setTexture(0, s_worldPos, worldPos);
    bgfx::setTexture(1, s_normalRough, normal);
    bgfx::setUniform(u_aoView, glm::value_ptr(view));
    bgfx::setUniform(u_aoProj, glm::value_ptr(proj));
    bgfx::setUniform(u_aoViewport, viewport);
    bgfx::setUniform(u_aoParams, aoParams);
    if (bgfx::isValid(u_aoParams2)) bgfx::setUniform(u_aoParams2, aoParams2);
    bgfx::setVertexBuffer(0, m_vbh);
    bgfx::setIndexBuffer(m_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(viewId, sampleProg);

    // View viewId + 1: bilateral upsample to full resolution.
    bgfx::setViewFrameBuffer(viewId + 1, m_aoFullFboA);
    bgfx::setViewRect(viewId + 1, 0, 0, width, height);
    bgfx::setViewClear(viewId + 1, BGFX_CLEAR_COLOR, 0xffffffff);
    bgfx::setTexture(0, s_aoSource, m_aoHalfTex);
    bgfx::setTexture(1, s_worldPos, worldPos);
    bgfx::setTexture(2, s_normalRough, normal);
    bgfx::setUniform(u_aoView, glm::value_ptr(view));
    bgfx::setUniform(u_aoViewport, viewport);
    bgfx::setUniform(u_aoBlurParams, upsampleParams);
    bgfx::setVertexBuffer(0, m_vbh);
    bgfx::setIndexBuffer(m_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(viewId + 1, m_aoUpsampleProg);

    // View viewId + 2: separable bilateral blur, horizontal pass A -> B.
    bgfx::setViewFrameBuffer(viewId + 2, m_aoFullFboB);
    bgfx::setViewRect(viewId + 2, 0, 0, width, height);
    bgfx::setViewClear(viewId + 2, BGFX_CLEAR_COLOR, 0xffffffff);
    bgfx::setTexture(0, s_aoSource, m_aoFullTexA);
    bgfx::setTexture(1, s_worldPos, worldPos);
    bgfx::setTexture(2, s_normalRough, normal);
    bgfx::setUniform(u_aoView, glm::value_ptr(view));
    bgfx::setUniform(u_aoViewport, viewport);
    bgfx::setUniform(u_aoBlurParams, blurParams);
    bgfx::setUniform(u_aoBlurDir, blurDirH);
    bgfx::setVertexBuffer(0, m_vbh);
    bgfx::setIndexBuffer(m_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(viewId + 2, m_aoBlurProg);

    // View viewId + 3: separable bilateral blur, vertical pass B -> A.
    bgfx::setViewFrameBuffer(viewId + 3, m_aoFullFboA);
    bgfx::setViewRect(viewId + 3, 0, 0, width, height);
    bgfx::setViewClear(viewId + 3, BGFX_CLEAR_COLOR, 0xffffffff);
    bgfx::setTexture(0, s_aoSource, m_aoFullTexB);
    bgfx::setTexture(1, s_worldPos, worldPos);
    bgfx::setTexture(2, s_normalRough, normal);
    bgfx::setUniform(u_aoView, glm::value_ptr(view));
    bgfx::setUniform(u_aoViewport, viewport);
    bgfx::setUniform(u_aoBlurParams, blurParams);
    bgfx::setUniform(u_aoBlurDir, blurDirV);
    bgfx::setVertexBuffer(0, m_vbh);
    bgfx::setIndexBuffer(m_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(viewId + 3, m_aoBlurProg);

    m_aoValid = true;
}

bgfx::TextureHandle PostProcessManager::RenderSSR(bgfx::TextureHandle color, bgfx::TextureHandle normal, bgfx::TextureHandle depth, bgfx::FrameBufferHandle targetFbo, const glm::mat4& proj, const glm::mat4& invProj, const float camPos[4], bgfx::ViewId viewId, uint16_t width, uint16_t height, float intensity) {
    if (!bgfx::isValid(m_ssrProg) || !bgfx::isValid(u_ssrInvProj)) return BGFX_INVALID_HANDLE;
    bgfx::setViewFrameBuffer(viewId, targetFbo);
    bgfx::setViewRect(viewId, 0, 0, width, height);
    bgfx::setTexture(0, s_texColor,  color);
    bgfx::setTexture(1, s_ssrNormal, normal);
    bgfx::setTexture(2, s_texDepth,  depth);
    bgfx::setUniform(u_ssrInvProj, glm::value_ptr(invProj));
    bgfx::setUniform(u_ssrProj, glm::value_ptr(proj));
    float ssrParams[4] = { std::max(intensity, 0.0f), 0.0f, 0.0f, 0.0f };
    bgfx::setUniform(u_ssrParams, ssrParams);
    bgfx::setVertexBuffer(0, m_vbh);
    bgfx::setIndexBuffer(m_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(viewId, m_ssrProg);
    return m_pingColor;
}

bgfx::TextureHandle PostProcessManager::RenderBloom(bgfx::TextureHandle color, bgfx::FrameBufferHandle target, bgfx::ViewId viewId, uint16_t w, uint16_t h, float threshold, float intensity, float radius) {
    if (!bgfx::isValid(m_bloomExtractProg) || !bgfx::isValid(m_bloomBlurProg) || !bgfx::isValid(m_bloomCombineProg)) return BGFX_INVALID_HANDLE;
    if (!bgfx::isValid(s_bloomTex) || !bgfx::isValid(m_bloomFboA) || !bgfx::isValid(m_bloomFboB)) return BGFX_INVALID_HANDLE;
    // FBOs are recreated once per frame in RenderLighting. Recreating them
    // here after views have already been submitted leaves dangling handles.
    if (m_width != w || m_height != h) return BGFX_INVALID_HANDLE;

    const uint16_t bw = (uint16_t)glm::max(1u, (unsigned)(w / 2));
    const uint16_t bh = (uint16_t)glm::max(1u, (unsigned)(h / 2));
    const float radiusScale = std::max(radius, 0.01f);
    float ts[4]  = { radiusScale / (float)bw, radiusScale / (float)bh, 0.0f, 0.0f };
    float th[4]  = { threshold, 0, 0, 0 };
    float dirH[4] = { 1.0f, 0.0f, 0, 0 };
    float dirV[4] = { 0.0f, 1.0f, 0, 0 };

    // Extract bright pixels into the half-res ping texture. The input color is
    // never used as a render target, so the original frame stays intact.
    bgfx::setViewFrameBuffer(viewId, m_bloomFboA);
    bgfx::setViewRect(viewId, 0, 0, bw, bh);
    bgfx::setTexture(0, s_texColor, color);
    bgfx::setUniform(u_bloomThreshold, th);
    bgfx::setVertexBuffer(0, m_vbh); bgfx::setIndexBuffer(m_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(viewId, m_bloomExtractProg);

    // Separable Gaussian blur: A -> B (horizontal), B -> A (vertical).
    bgfx::setViewFrameBuffer(viewId + 1, m_bloomFboB);
    bgfx::setViewRect(viewId + 1, 0, 0, bw, bh);
    bgfx::setTexture(0, s_texColor, m_bloomTexA);
    bgfx::setUniform(u_bloomTexelSize, ts);
    bgfx::setUniform(u_bloomDir, dirH);
    bgfx::setVertexBuffer(0, m_vbh); bgfx::setIndexBuffer(m_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(viewId + 1, m_bloomBlurProg);

    bgfx::setViewFrameBuffer(viewId + 2, m_bloomFboA);
    bgfx::setViewRect(viewId + 2, 0, 0, bw, bh);
    bgfx::setTexture(0, s_texColor, m_bloomTexB);
    bgfx::setUniform(u_bloomTexelSize, ts);
    bgfx::setUniform(u_bloomDir, dirV);
    bgfx::setVertexBuffer(0, m_vbh); bgfx::setIndexBuffer(m_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(viewId + 2, m_bloomBlurProg);

    // Add bloom on top of the untouched input color.
    bgfx::setViewFrameBuffer(viewId + 3, target);
    bgfx::setViewRect(viewId + 3, 0, 0, w, h);
    bgfx::setTexture(0, s_texColor, color);
    bgfx::setTexture(1, s_bloomTex, m_bloomTexA);
    float bi[4] = { intensity, 0, 0, 0 };
    bgfx::setUniform(u_bloomIntensity, bi);
    bgfx::setVertexBuffer(0, m_vbh); bgfx::setIndexBuffer(m_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(viewId + 3, m_bloomCombineProg);

    return m_pingColor;
}


bgfx::TextureHandle PostProcessManager::RenderColorCorrection(bgfx::TextureHandle color, bgfx::FrameBufferHandle targetFbo, bgfx::ViewId viewId, uint16_t width, uint16_t height, float exposure, float contrast, float saturation) {
    if (!bgfx::isValid(m_colorCorrectionProg) || !bgfx::isValid(u_colorCorrectionParams)) return color;
    bgfx::setViewFrameBuffer(viewId, targetFbo);
    bgfx::setViewRect(viewId, 0, 0, width, height);
    bgfx::setTexture(0, s_texColor, color);
    float params[4] = { exposure, contrast, saturation, 0.0f };
    bgfx::setUniform(u_colorCorrectionParams, params);
    bgfx::setVertexBuffer(0, m_vbh);
    bgfx::setIndexBuffer(m_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(viewId, m_colorCorrectionProg);
    return m_pingColor;
}

bgfx::TextureHandle PostProcessManager::RenderVignette(bgfx::TextureHandle color, bgfx::FrameBufferHandle targetFbo, bgfx::ViewId viewId, uint16_t width, uint16_t height, float intensity, float smoothness) {
    if (!bgfx::isValid(m_vignetteProg) || !bgfx::isValid(u_vignetteParams)) return color;
    bgfx::setViewFrameBuffer(viewId, targetFbo);
    bgfx::setViewRect(viewId, 0, 0, width, height);
    bgfx::setTexture(0, s_texColor, color);
    float params[4] = { intensity, smoothness, 0.0f, 0.0f };
    bgfx::setUniform(u_vignetteParams, params);
    bgfx::setVertexBuffer(0, m_vbh);
    bgfx::setIndexBuffer(m_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(viewId, m_vignetteProg);
    return m_pingColor;
}

bgfx::TextureHandle PostProcessManager::RenderChromatic(bgfx::TextureHandle color, bgfx::FrameBufferHandle targetFbo, bgfx::ViewId viewId, uint16_t width, uint16_t height, float intensity) {
    if (!bgfx::isValid(m_chromaticProg) || !bgfx::isValid(u_chromaParams)) return color;
    bgfx::setViewFrameBuffer(viewId, targetFbo);
    bgfx::setViewRect(viewId, 0, 0, width, height);
    bgfx::setTexture(0, s_texColor, color);
    float params[4] = { intensity, 0.0f, 0.0f, 0.0f };
    bgfx::setUniform(u_chromaParams, params);
    bgfx::setVertexBuffer(0, m_vbh);
    bgfx::setIndexBuffer(m_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(viewId, m_chromaticProg);
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
    if (!bgfx::isValid(m_fxaaProg) || !bgfx::isValid(u_texelSize)) return BGFX_INVALID_HANDLE;
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
    if (!bgfx::isValid(m_invertProg)) return BGFX_INVALID_HANDLE;
    bgfx::setViewFrameBuffer(viewId, targetFbo);
    bgfx::setViewRect(viewId, 0, 0, width, height);
    bgfx::setTexture(0, s_texColor, color);
    bgfx::setVertexBuffer(0, m_vbh);
    bgfx::setIndexBuffer(m_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(viewId, m_invertProg);
    return m_pingColor;
}


bgfx::TextureHandle PostProcessManager::RenderSmaa(bgfx::TextureHandle color, bgfx::ViewId viewId, uint16_t w, uint16_t h) {
    if (!bgfx::isValid(m_smaaEdgeProg) || !bgfx::isValid(m_smaaBlendProg) || !bgfx::isValid(s_smaaEdges)) return BGFX_INVALID_HANDLE;
    if (m_width != w || m_height != h) return BGFX_INVALID_HANDLE;

    // View [viewId]: Edge Detection (рендерим в m_blitFboA)
    bgfx::setViewFrameBuffer(viewId, m_blitFboA);
    bgfx::setViewRect(viewId, 0, 0, w, h);
    bgfx::setTexture(0, s_texColor, color); // Сэмплируем напрямую
    float ts[4] = { 1.0f / (float)w, 1.0f / (float)h, 0.0f, 0.0f };
    bgfx::setUniform(u_texelSize, ts);
    bgfx::setVertexBuffer(0, m_vbh);
    bgfx::setIndexBuffer(m_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(viewId, m_smaaEdgeProg);

    // View [viewId + 1]: Blend
    bgfx::setViewFrameBuffer(viewId + 1, m_pingFbo);
    bgfx::setViewRect(viewId + 1, 0, 0, w, h);
    bgfx::setTexture(0, s_texColor, color); // Сэмплируем напрямую
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


bgfx::TextureHandle PostProcessManager::RenderTaa(bgfx::TextureHandle color, bgfx::TextureHandle depth, const glm::mat4& prevVP, const glm::mat4& invVP, bgfx::FrameBufferHandle target, bgfx::ViewId viewId, uint16_t w, uint16_t h, float blend) {
    if (!bgfx::isValid(m_taaProg) || !bgfx::isValid(u_taaParams)) return BGFX_INVALID_HANDLE;
    if (m_width != w || m_height != h) return BGFX_INVALID_HANDLE;

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
    bgfx::setTexture(0, s_texColor, color); // Сэмплируем напрямую
    bgfx::setTexture(1, s_taaHistory, m_historyColor);
    bgfx::setTexture(2, s_texDepth, depth); // Сэмплируем глубину напрямую
    bgfx::setUniform(u_taaPrevVP, glm::value_ptr(prevVP));
    bgfx::setUniform(u_taaInvVP, glm::value_ptr(invVP));
    float params[4] = { std::clamp(blend, 0.0f, 1.0f), 0, 0, 0 };
    bgfx::setUniform(u_taaParams, params);
    float ts[4] = { 1.0f / (float)w, 1.0f / (float)h, 0.0f, 0.0f };
    bgfx::setUniform(u_taaTexelSize, ts);
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

bgfx::TextureHandle PostProcessManager::RenderGodRays(bgfx::TextureHandle color, bgfx::TextureHandle depth,
                                                     const glm::mat4& viewProj, const float camPos[4], const float sunDir[4],
                                                     bgfx::FrameBufferHandle target,
                                                     bgfx::ViewId viewId, uint16_t w, uint16_t h,
                                                     float intensity, float decay, float density, float weight) {
    if (!bgfx::isValid(m_godraysProg)) return BGFX_INVALID_HANDLE;

    // Project a point far along the sun direction. The previous code
    // multiplied a direction (w=0) by the inverse view-projection and then
    // divided by a zero w, which produced NaN and broke the whole pass.
    glm::vec3 sunWorld = glm::vec3(camPos[0], camPos[1], camPos[2])
                       + glm::vec3(sunDir[0], sunDir[1], sunDir[2]) * 10000.0f;
    glm::vec4 sunClip = viewProj * glm::vec4(sunWorld, 1.0f);
    if (sunClip.w <= 0.0001f) return BGFX_INVALID_HANDLE;
    glm::vec3 sunNdc = glm::vec3(sunClip) / sunClip.w;
    glm::vec2 sunScreen = glm::vec2(sunNdc.x * 0.5f + 0.5f,
                                    0.5f - sunNdc.y * 0.5f);

    // Clamp the projected light source just outside the viewport. The
    // direction toward an off-screen sun still defines the shaft pattern,
    // and clamping keeps the radial samples on screen instead of making the
    // effect disappear completely.
    sunScreen = glm::clamp(sunScreen, glm::vec2(-0.20f), glm::vec2(1.20f));

    bgfx::setViewFrameBuffer(viewId, target);
    bgfx::setViewRect(viewId, 0, 0, w, h);
    bgfx::setViewClear(viewId, BGFX_CLEAR_COLOR, 0x000000ff);
    bgfx::setTexture(0, s_texColor, color);
    bgfx::setTexture(1, s_texDepth, depth);

    float sunPos[4] = { sunScreen.x, sunScreen.y, std::max(intensity, 0.0f),
                        (float)w / (float)h };
    bgfx::setUniform(u_sunScreenPos, sunPos);
    // Density is a fraction of the distance to the light source. Clamping it
    // prevents values above 1 from making every radial sample step off-screen.
    float params[4] = { std::clamp(decay, 0.0f, 1.0f),
                        std::clamp(density, 0.0f, 1.0f),
                        std::max(weight, 0.0f), 1.0f };
    bgfx::setUniform(u_godraysParams, params);
    bgfx::setVertexBuffer(0, m_vbh);
    bgfx::setIndexBuffer(m_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(viewId, m_godraysProg);
    return m_pingColor;
}

bgfx::TextureHandle PostProcessManager::RenderAtmosphere(bgfx::TextureHandle color, bgfx::TextureHandle depth,
                                                          const glm::mat4& invVP, const float camPos[4],
                                                          const float sunDir[4],
                                                          bgfx::FrameBufferHandle target,
                                                          bgfx::ViewId viewId, uint16_t w, uint16_t h,
                                                          float intensity) {
    if (!bgfx::isValid(m_atmosphereProg)) return BGFX_INVALID_HANDLE;
    bgfx::setViewFrameBuffer(viewId, target);
    bgfx::setViewRect(viewId, 0, 0, w, h);
    bgfx::setViewClear(viewId, BGFX_CLEAR_COLOR, 0x000000ff);
    bgfx::setTexture(0, s_texColor, color);
    bgfx::setTexture(1, s_texDepth, depth);
    bgfx::setUniform(u_atmoInvVP, glm::value_ptr(invVP));
    bgfx::setUniform(u_camPos, camPos);
    float sun[4] = { sunDir[0], sunDir[1], sunDir[2], intensity };
    bgfx::setUniform(u_atmoSunDir, sun);
    float params[4] = { 6371e3f, 6471e3f, 8000.0f, 1200.0f };
    bgfx::setUniform(u_atmoParams, params);
    bgfx::setVertexBuffer(0, m_vbh);
    bgfx::setIndexBuffer(m_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(viewId, m_atmosphereProg);
    return m_pingColor;
}

bgfx::TextureHandle PostProcessManager::RenderMotionBlur(bgfx::TextureHandle color, bgfx::TextureHandle worldPos,
                                                         const glm::mat4& prevVP, const glm::mat4& invVP,
                                                         bgfx::FrameBufferHandle target,
                                                         bgfx::ViewId velocityViewId, bgfx::ViewId blurViewId,
                                                         uint16_t w, uint16_t h, float frameDelta,
                                                         float amount, float maxVelocityPx) {
    if (!bgfx::isValid(m_motionBlurProg) || !bgfx::isValid(m_mbVelocityProg) ||
        !bgfx::isValid(m_mbVelocityFbo) || !bgfx::isValid(m_mbVelocityTex)) {
        return BGFX_INVALID_HANDLE;
    }

    // Reference: RenderPipeline motion_blur plugin. The camera velocity is
    // computed first into an RGBA16F target, then the color is blurred along
    // that velocity. This keeps per-sample velocity lookups cheap and avoids
    // reconstructing world positions for every blur tap.
    float dt = frameDelta;
    if (dt < (1.0f / 1000.0f)) dt = (1.0f / 1000.0f);
    if (dt > 0.1f) dt = 0.1f;
    // RenderPipeline scales velocity by (1/60) / frame_delta and by a
    // user-facing camera blur factor.
    float velocityScale = std::max(amount, 0.0f) * (1.0f / 60.0f) / dt;

    // View [velocityViewId]: camera velocity in UV space.
    bgfx::setViewFrameBuffer(velocityViewId, m_mbVelocityFbo);
    bgfx::setViewRect(velocityViewId, 0, 0, w, h);
    bgfx::setViewClear(velocityViewId, BGFX_CLEAR_COLOR, 0x00000000);
    bgfx::setTexture(0, s_worldPos, worldPos);
    bgfx::setUniform(u_mbPrevVP, glm::value_ptr(prevVP));
    bgfx::setUniform(u_mbInvVP, glm::value_ptr(invVP));
    float velParams[4] = { velocityScale, 0.0f, 0.0f, 0.0f };
    bgfx::setUniform(u_mbVelocityParams, velParams);
    bgfx::setVertexBuffer(0, m_vbh);
    bgfx::setIndexBuffer(m_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(velocityViewId, m_mbVelocityProg);

    // View [blurViewId]: jittered camera blur. maxVelocity/minVelocity are
    // expressed in UV units, matching RenderPipeline's 70px and 0.5px limits.
    bgfx::setViewFrameBuffer(blurViewId, target);
    bgfx::setViewRect(blurViewId, 0, 0, w, h);
    bgfx::setViewClear(blurViewId, BGFX_CLEAR_COLOR, 0x000000ff);
    bgfx::setTexture(0, s_texColor, color);
    bgfx::setTexture(1, s_mbVelocity, m_mbVelocityTex);
    float blurParams[4] = { std::max(maxVelocityPx, 0.0f) / (float)w,
                            0.5f / (float)w, (float)w, 0.0f };
    bgfx::setUniform(u_motionBlurParams, blurParams);
    bgfx::setVertexBuffer(0, m_vbh);
    bgfx::setIndexBuffer(m_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(blurViewId, m_motionBlurProg);

    return m_pingColor;
}

bgfx::TextureHandle PostProcessManager::RenderDof(bgfx::TextureHandle color, bgfx::TextureHandle depth,
                                                    const glm::mat4& proj, const glm::mat4& invProj,
                                                    bgfx::FrameBufferHandle target,
                                                    bgfx::ViewId viewId, uint16_t w, uint16_t h,
                                                    float focusDistance, float blurAmount) {
    (void)invProj;
    if (!bgfx::isValid(m_dofCocProg) || !bgfx::isValid(m_dofBlurProg) || !bgfx::isValid(m_dofCompositeProg))
        return BGFX_INVALID_HANDLE;
    if (!bgfx::isValid(m_dofCocFbo) || !bgfx::isValid(m_dofBlurFboA) || !bgfx::isValid(m_dofBlurFboB))
        return BGFX_INVALID_HANDLE;

    float texelX = 1.0f / (float)w;
    float texelY = 1.0f / (float)h;

    // View [viewId]: CoC
    bgfx::setViewFrameBuffer(viewId, m_dofCocFbo);
    bgfx::setViewRect(viewId, 0, 0, w, h);
    bgfx::setViewClear(viewId, BGFX_CLEAR_COLOR, 0x000000ff);
    bgfx::setTexture(0, s_dofDepth, depth);
    // Извлекаем near/far из proj (Vulkan, RH):
    //   m22 = far/(near-far)   m32 = near*far/(near-far)
    float nearV = proj[3][2] / proj[2][2];
    float farV  = proj[3][2] / (proj[2][2] + 1.0f);
    if (nearV <= 0.0f)  nearV = 0.1f;
    if (farV  <= nearV) farV  = 1000.0f;
    const float maxCoC = std::max(blurAmount, 0.0f);
    float dofParams[4] = { focusDistance, 1.0f/nearV, 1.0f/farV, maxCoC };
    bgfx::setUniform(u_dofParams, dofParams);
    bgfx::setVertexBuffer(0, m_vbh);
    bgfx::setIndexBuffer(m_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(viewId, m_dofCocProg);

    // View [viewId+1]: H blur
    bgfx::setViewFrameBuffer(viewId + 1, m_dofBlurFboA);
    bgfx::setViewRect(viewId + 1, 0, 0, w, h);
    bgfx::setViewClear(viewId + 1, BGFX_CLEAR_COLOR, 0x000000ff);
    bgfx::setTexture(0, s_dofColor, color);
    bgfx::setTexture(1, s_dofCoc, m_dofCocTex);
    float texel[4] = { texelX, texelY, maxCoC, 0.0f };
    bgfx::setUniform(u_dofTexelSize, texel);
    float dirH[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
    bgfx::setUniform(u_dofDir, dirH);
    bgfx::setVertexBuffer(0, m_vbh);
    bgfx::setIndexBuffer(m_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(viewId + 1, m_dofBlurProg);

    // View [viewId+2]: V blur
    bgfx::setViewFrameBuffer(viewId + 2, m_dofBlurFboB);
    bgfx::setViewRect(viewId + 2, 0, 0, w, h);
    bgfx::setViewClear(viewId + 2, BGFX_CLEAR_COLOR, 0x000000ff);
    bgfx::setTexture(0, s_dofColor, m_dofBlurTexA);
    bgfx::setTexture(1, s_dofCoc, m_dofCocTex);
    bgfx::setUniform(u_dofTexelSize, texel);
    float dirV[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
    bgfx::setUniform(u_dofDir, dirV);
    bgfx::setVertexBuffer(0, m_vbh);
    bgfx::setIndexBuffer(m_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(viewId + 2, m_dofBlurProg);

    // View [viewId+3]: composite
    bgfx::setViewFrameBuffer(viewId + 3, target);
    bgfx::setViewRect(viewId + 3, 0, 0, w, h);
    bgfx::setViewClear(viewId + 3, BGFX_CLEAR_COLOR, 0x000000ff);
    bgfx::setTexture(0, s_texColor, color);
    bgfx::setTexture(1, s_dofCoc, m_dofCocTex);
    bgfx::setTexture(2, s_dofBlur, m_dofBlurTexB);
    bgfx::setVertexBuffer(0, m_vbh);
    bgfx::setIndexBuffer(m_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(viewId + 3, m_dofCompositeProg);

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

#pragma once

#include <bgfx/bgfx.h>
#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <random>
#include <unordered_map>
#include <vector>

namespace Galeshi {

class Scene;
class Camera;
struct ParticleEmitterComponent;

// CPU particle simulation and billboard renderer.
//
// Particles are simulated in world space and rendered as soft, camera-facing
// quads with alpha blending.  Update() must be called once per frame from the
// game/editor loop; Render() submits a dedicated view after the opaque scene
// has been lit so the particles go through the post-processing chain.
class ParticleSystem {
public:
    ParticleSystem();
    ~ParticleSystem();

    void Init();
    void Shutdown();

    // Advances the simulation for every emitter in the scene.
    void Update(Scene& scene, float dt);

    // Draws live particles into viewId using target as the color attachment.
    // The view is configured here (rect, transform, framebuffer, no clear).
    void Render(Scene& scene, const Camera& camera, bgfx::ViewId viewId,
                bgfx::FrameBufferHandle target, uint16_t width, uint16_t height);

    // Drops every live particle (scene reset/reload).
    void Reset();

    size_t ParticleCount() const { return m_totalParticles; }
    size_t ParticleCount(entt::entity emitter) const;

    // Drops live particles and runtime state for one deleted emitter.
    void RemoveEmitter(entt::entity emitter);

    // One-shot water splash. Particles are simulated/rendered separately from
    // scene emitters and are not serialized.
    void EmitSplash(const glm::vec3& position, int count, float speed,
                    float sizeScale = 1.0f,
                    const glm::vec3& waterColor = glm::vec3(1.0f));

private:
    struct Particle {
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        float     age      = 0.0f;
        float     lifetime = 1.0f;
        // Per-particle scale and tint used by splash droplets; emitters leave
        // them at 1.0.
        float     sizeScale = 1.0f;
        glm::vec4 tint{1.0f};
        // Splash droplets are removed when they fall back through the water
        // surface. Emitters use -FLT_MAX (no kill plane).
        float     killY = -3.402823466e+38F;
        // Per-particle splash dynamics. Emitters use their component values.
        float     gravityScale = 1.0f;
        float     drag = 0.35f;
    };

    struct EmitterRuntime {
        std::vector<Particle> particles;
        float spawnAccumulator = 0.0f;
    };

    struct Vertex {
        glm::vec3 position;
        glm::vec2 uv;
        uint32_t  color;
    };

    void UpdateEmitter(Scene& scene, entt::entity entity,
                       const ParticleEmitterComponent& emitter,
                       EmitterRuntime& runtime, float dt);
    void PruneRuntimes(Scene& scene);

    bgfx::ProgramHandle m_program = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout  m_layout;
    std::unordered_map<entt::entity, EmitterRuntime> m_emitters;
    // Splash layers are rendered with dedicated procedural sprites so the
    // effect reads as droplets, foam and mist instead of soft blobs.
    std::vector<Particle> m_splashDroplets;
    std::vector<Particle> m_splashFoam;
    std::vector<Particle> m_splashMist;
    std::mt19937        m_rng{0x9E3779B9u};
    size_t              m_totalParticles = 0;

    // Texture resources: 1x1 white fallback used when an emitter has no
    // texture, plus the sampler/parameter uniforms shared by the shader.
    bgfx::TextureHandle m_whiteTex     = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_splashDropletTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_splashFoamTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_splashMistTex = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_texUniform   = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_paramsUniform = BGFX_INVALID_HANDLE;
};

} // namespace Galeshi

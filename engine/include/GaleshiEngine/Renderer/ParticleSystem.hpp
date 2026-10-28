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

private:
    struct Particle {
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        float     age      = 0.0f;
        float     lifetime = 1.0f;
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
    std::mt19937        m_rng{0x9E3779B9u};
    size_t              m_totalParticles = 0;
};

} // namespace Galeshi

#include "GaleshiEngine/Renderer/ParticleSystem.hpp"
#include "GaleshiEngine/Renderer/Camera.hpp"
#include "GaleshiEngine/Renderer/ShaderManager.hpp"
#include "GaleshiEngine/Scene/Scene.hpp"
#include "GaleshiEngine/Scene/Components.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Galeshi {

namespace {

// World matrix computed from the entity's parent chain, mirroring the
// helper used by RenderSystem.
glm::mat4 EntityWorldMatrix(Scene& scene, entt::entity handle) {
    glm::mat4 model(1.0f);
    entt::entity current = handle;
    std::vector<entt::entity> chain;
    int depth = 0;
    while (current != entt::null && depth++ < 128) {
        chain.push_back(current);
        if (scene.Registry().all_of<ParentComponent>(current)) {
            current = scene.Registry().get<ParentComponent>(current).parent;
        } else {
            break;
        }
    }
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        if (scene.Registry().all_of<TransformComponent>(*it)) {
            model = model * scene.Registry().get<TransformComponent>(*it).LocalMatrix();
        }
    }
    return model;
}

uint32_t PackColor(const glm::vec4& c) {
    auto toByte = [](float v) {
        return static_cast<uint32_t>(glm::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f) & 0xFFu;
    };
    const uint32_t r = toByte(c.r);
    const uint32_t g = toByte(c.g);
    const uint32_t b = toByte(c.b);
    const uint32_t a = toByte(c.a);
    return r | (g << 8) | (b << 16) | (a << 24);
}

glm::vec3 RandomUnitVector(std::mt19937& rng) {
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (int attempt = 0; attempt < 16; ++attempt) {
        glm::vec3 v{dist(rng), dist(rng), dist(rng)};
        const float len2 = glm::dot(v, v);
        if (len2 > 1e-6f && len2 <= 1.0f) {
            return v / std::sqrt(len2);
        }
    }
    return glm::vec3(0.0f, 1.0f, 0.0f);
}

} // namespace

ParticleSystem::ParticleSystem() = default;
ParticleSystem::~ParticleSystem() { Shutdown(); }

void ParticleSystem::Init() {
    m_layout.begin()
        .add(bgfx::Attrib::Position,  3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0,    4, bgfx::AttribType::Uint8, true)
        .end();

    m_program = ShaderManager::LoadProgram("shaders/vs_particles.bin",
                                           "shaders/fs_particles.bin");
    if (!bgfx::isValid(m_program)) {
        spdlog::error("[ParticleSystem] Failed to load particle shaders");
    } else {
        spdlog::info("[ParticleSystem] Particle program loaded");
    }
}

void ParticleSystem::Shutdown() {
    if (bgfx::isValid(m_program)) {
        bgfx::destroy(m_program);
        m_program = BGFX_INVALID_HANDLE;
    }
    m_emitters.clear();
    m_totalParticles = 0;
}

void ParticleSystem::Reset() {
    m_emitters.clear();
    m_totalParticles = 0;
}

size_t ParticleSystem::ParticleCount(entt::entity emitter) const {
    auto it = m_emitters.find(emitter);
    return it != m_emitters.end() ? it->second.particles.size() : 0;
}

void ParticleSystem::PruneRuntimes(Scene& scene) {
    auto& reg = scene.Registry();
    for (auto it = m_emitters.begin(); it != m_emitters.end();) {
        if (!reg.valid(it->first) || !reg.all_of<ParticleEmitterComponent>(it->first)) {
            it = m_emitters.erase(it);
        } else {
            ++it;
        }
    }
}

void ParticleSystem::Update(Scene& scene, float dt) {
    m_totalParticles = 0;
    if (dt <= 0.0f) {
        return;
    }
    // A long hitch must not turn into a giant simulation step.
    dt = std::min(dt, 0.1f);

    PruneRuntimes(scene);

    auto& reg = scene.Registry();
    auto view = reg.view<ParticleEmitterComponent>();
    for (auto entity : view) {
        auto& emitter = view.get<ParticleEmitterComponent>(entity);
        auto& runtime = m_emitters[entity];
        // Disabled emitters stop spawning but their live particles keep
        // simulating and fade out naturally.
        UpdateEmitter(scene, entity, emitter, runtime, dt);
        m_totalParticles += runtime.particles.size();
    }
}

void ParticleSystem::UpdateEmitter(Scene& scene, entt::entity entity,
                                   const ParticleEmitterComponent& emitter,
                                   EmitterRuntime& runtime, float dt) {
    const glm::mat4 world = EntityWorldMatrix(scene, entity);
    const glm::vec3 origin(world[3]);
    const glm::vec3 baseVelocity =
        glm::vec3(world * glm::vec4(emitter.velocity, 0.0f));

    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const uint32_t maxParticles = std::max<uint32_t>(emitter.maxParticles, 1u);
    const float emitRate = emitter.enabled ? std::max(emitter.emitRate, 0.0f) : 0.0f;

    runtime.spawnAccumulator += emitRate * dt;
    // Never keep more than a frame's worth of pending spawns if we hit the cap.
    if (runtime.spawnAccumulator > emitRate * 0.25f + 1.0f) {
        runtime.spawnAccumulator = emitRate * 0.25f + 1.0f;
    }

    while (runtime.spawnAccumulator >= 1.0f &&
           runtime.particles.size() < maxParticles) {
        runtime.spawnAccumulator -= 1.0f;

        Particle p;
        // Uniform point inside the spawn sphere.
        const glm::vec3 dir = RandomUnitVector(m_rng);
        const float radius = emitter.spawnRadius * std::cbrt(unit(m_rng));
        p.position = origin + dir * radius;

        p.velocity = baseVelocity +
                     RandomUnitVector(m_rng) * (emitter.velocityVariance * unit(m_rng));

        const float variance = emitter.lifetimeVariance * (unit(m_rng) * 2.0f - 1.0f);
        p.lifetime = std::max(0.05f, emitter.lifetime + variance);
        p.age = 0.0f;
        runtime.particles.push_back(p);
    }

    if (runtime.particles.size() >= maxParticles) {
        runtime.spawnAccumulator = 0.0f;
    }

    for (size_t i = 0; i < runtime.particles.size();) {
        Particle& p = runtime.particles[i];
        p.age += dt;
        if (p.age >= p.lifetime) {
            p = runtime.particles.back();
            runtime.particles.pop_back();
            continue;
        }
        p.velocity += emitter.gravity * dt;
        if (emitter.drag > 0.0f) {
            p.velocity *= std::max(0.0f, 1.0f - emitter.drag * dt);
        }
        p.position += p.velocity * dt;
        ++i;
    }
}

void ParticleSystem::Render(Scene& scene, const Camera& camera,
                            bgfx::ViewId viewId, bgfx::FrameBufferHandle target,
                            uint16_t width, uint16_t height) {
    if (!bgfx::isValid(m_program) || !bgfx::isValid(target) || width == 0 || height == 0) {
        return;
    }

    auto& reg = scene.Registry();
    auto view = reg.view<ParticleEmitterComponent>();

    size_t quadCount = 0;
    for (auto entity : view) {
        auto it = m_emitters.find(entity);
        if (it != m_emitters.end()) {
            quadCount += it->second.particles.size();
        }
    }
    if (quadCount == 0) {
        return;
    }

    const glm::mat4& v = camera.View();
    const glm::vec3 right = glm::normalize(glm::vec3(v[0][0], v[1][0], v[2][0]));
    const glm::vec3 up    = glm::normalize(glm::vec3(v[0][1], v[1][1], v[2][1]));

    uint32_t quads = static_cast<uint32_t>(std::min<size_t>(quadCount, 1u << 20));

    // Clamp to what the transient buffers can currently hold.
    const uint32_t availVerts = bgfx::getAvailTransientVertexBuffer(quads * 4, m_layout);
    const uint32_t availIndices = bgfx::getAvailTransientIndexBuffer(quads * 6, true);
    quads = std::min(quads, std::min(availVerts / 4, availIndices / 6));
    if (quads == 0) {
        return;
    }

    bgfx::TransientVertexBuffer tvb;
    bgfx::TransientIndexBuffer tib;
    bgfx::allocTransientVertexBuffer(&tvb, quads * 4, m_layout);
    bgfx::allocTransientIndexBuffer(&tib, quads * 6, true);

    auto* vertices = reinterpret_cast<Vertex*>(tvb.data);
    auto* indices = reinterpret_cast<uint32_t*>(tib.data);

    uint32_t q = 0;
    for (auto entity : view) {
        if (q >= quads) break;
        auto it = m_emitters.find(entity);
        if (it == m_emitters.end()) continue;
        const auto& emitter = view.get<ParticleEmitterComponent>(entity);
        for (const auto& p : it->second.particles) {
            if (q >= quads) break;

            const float t = glm::clamp(p.age / p.lifetime, 0.0f, 1.0f);
            const float size = glm::mix(emitter.startSize, emitter.endSize, t);
            const glm::vec4 color = glm::mix(emitter.startColor, emitter.endColor, t);
            const uint32_t packed = PackColor(color);

            const glm::vec3 r = right * size;
            const glm::vec3 u = up * size;
            const glm::vec3 c = p.position;

            Vertex* vtx = vertices + static_cast<size_t>(q) * 4;
            vtx[0] = {c - r - u, {0.0f, 0.0f}, packed};
            vtx[1] = {c + r - u, {1.0f, 0.0f}, packed};
            vtx[2] = {c + r + u, {1.0f, 1.0f}, packed};
            vtx[3] = {c - r + u, {0.0f, 1.0f}, packed};

            uint32_t* idx = indices + static_cast<size_t>(q) * 6;
            const uint32_t base = q * 4;
            idx[0] = base + 0; idx[1] = base + 1; idx[2] = base + 2;
            idx[3] = base + 0; idx[4] = base + 2; idx[5] = base + 3;
            ++q;
        }
    }

    bgfx::setViewFrameBuffer(viewId, target);
    bgfx::setViewRect(viewId, 0, 0, width, height);
    bgfx::setViewClear(viewId, BGFX_CLEAR_NONE);
    bgfx::setViewTransform(viewId, glm::value_ptr(camera.View()),
                           glm::value_ptr(camera.Projection()));

    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setIndexBuffer(&tib);
    // The output FBO depth attachment is not the scene depth, so particles are
    // drawn without depth testing.  Alpha blending keeps them soft.
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                   BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                                         BGFX_STATE_BLEND_INV_SRC_ALPHA) |
                   BGFX_STATE_MSAA);
    bgfx::submit(viewId, m_program);
}

} // namespace Galeshi

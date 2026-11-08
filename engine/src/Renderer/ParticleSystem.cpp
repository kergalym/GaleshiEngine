#include "GaleshiEngine/Renderer/ParticleSystem.hpp"
#include "GaleshiEngine/Renderer/Camera.hpp"
#include "GaleshiEngine/Renderer/ShaderManager.hpp"
#include "GaleshiEngine/Scene/Scene.hpp"
#include "GaleshiEngine/Scene/Components.hpp"
#include "GaleshiEngine/Assets/Texture.hpp"

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

float Hash01(uint32_t n) {
    n = (n ^ 61u) ^ (n >> 16);
    n *= 9u;
    n ^= n >> 4;
    n *= 0x27d4eb2du;
    n ^= n >> 15;
    return static_cast<float>(n & 0x00FFFFFFu) /
           static_cast<float>(0x00FFFFFFu);
}

float ValueNoise2D(float x, float y) {
    const int ix = static_cast<int>(std::floor(x));
    const int iz = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(ix);
    const float fz = y - static_cast<float>(iz);
    const float sx = fx * fx * (3.0f - 2.0f * fx);
    const float sz = fz * fz * (3.0f - 2.0f * fz);
    auto at = [](int x, int y) {
        return Hash01(static_cast<uint32_t>(x) * 374761393u +
                      static_cast<uint32_t>(y) * 668265263u);
    };
    const float a = at(ix, iz);
    const float b = at(ix + 1, iz);
    const float c = at(ix, iz + 1);
    const float d = at(ix + 1, iz + 1);
    return glm::mix(glm::mix(a, b, sx), glm::mix(c, d, sx), sz);
}

float SmoothStep(float edge0, float edge1, float x) {
    const float t = glm::clamp((x - edge0) / (edge1 - edge0),
                               0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// 0 = droplet, 1 = foam bubbles, 2 = mist puff, 3 = air bubble.
bgfx::TextureHandle CreateSplashTexture(int size, int kind) {
    std::vector<uint32_t> pixels(
        static_cast<size_t>(size) * static_cast<size_t>(size));
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float u = (x + 0.5f) / static_cast<float>(size);
            const float v = (y + 0.5f) / static_cast<float>(size);
            float alpha = 0.0f;
            if (kind == 0) {
                // Teardrop-ish droplet with a bright core.
                const float dx = (u - 0.5f) / 0.23f;
                const float dy = (v - 0.55f) / 0.42f;
                const float d = std::sqrt(dx * dx + dy * dy);
                alpha = 1.0f - SmoothStep(0.45f, 1.0f, d);
                alpha = std::max(alpha,
                    0.55f * (1.0f - SmoothStep(0.0f, 0.45f, d)));
            } else if (kind == 1) {
                // Overlapping foam bubbles with soft rims.
                struct Bubble { float x, y, r; };
                static constexpr Bubble kBubbles[] = {
                    {0.28f, 0.32f, 0.13f}, {0.62f, 0.27f, 0.09f},
                    {0.55f, 0.60f, 0.16f}, {0.30f, 0.68f, 0.08f},
                    {0.78f, 0.56f, 0.07f}, {0.46f, 0.44f, 0.06f},
                    {0.20f, 0.50f, 0.05f}, {0.72f, 0.78f, 0.05f},
                };
                for (const Bubble& b : kBubbles) {
                    const float d = std::sqrt(
                        (u - b.x) * (u - b.x) + (v - b.y) * (v - b.y));
                    const float rim = 1.0f -
                        SmoothStep(b.r * 0.45f, b.r, d);
                    alpha = std::max(alpha, rim * 0.95f);
                }
                const float center = 1.0f - SmoothStep(
                    0.05f, 0.42f,
                    std::sqrt((u - 0.5f) * (u - 0.5f) +
                              (v - 0.5f) * (v - 0.5f)));
                alpha = std::max(alpha, center * 0.55f);
            } else if (kind == 2) {
                const float d = std::sqrt(
                    (u - 0.5f) * (u - 0.5f) * 1.7f +
                    (v - 0.5f) * (v - 0.5f) * 1.7f);
                const float cloud = 1.0f - SmoothStep(0.15f, 0.62f, d);
                float n = ValueNoise2D(u * 5.0f, v * 5.0f);
                n = 0.65f * n +
                    0.35f * ValueNoise2D(u * 11.0f + 3.1f,
                                         v * 11.0f + 7.7f);
                alpha = cloud * (0.25f + 0.75f * n);
            } else {
                const float d = std::sqrt(
                    (u - 0.5f) * (u - 0.5f) +
                    (v - 0.5f) * (v - 0.5f));
                const float outer = 1.0f - SmoothStep(0.32f, 0.48f, d);
                const float inner = 1.0f - SmoothStep(0.22f, 0.34f, d);
                alpha = std::max(0.0f, outer - inner * 0.82f);
                const float hx = u - 0.37f;
                const float hy = v - 0.36f;
                alpha = std::max(alpha, 1.0f - SmoothStep(
                    0.0f, 0.10f, std::sqrt(hx * hx + hy * hy)));
            }
            alpha = glm::clamp(alpha, 0.0f, 1.0f);
            const uint32_t a = static_cast<uint32_t>(alpha * 255.0f + 0.5f);
            pixels[static_cast<size_t>(y) * size + x] =
                0x00FFFFFFu | (a << 24);
        }
    }
    return bgfx::createTexture2D(
        static_cast<uint16_t>(size), static_cast<uint16_t>(size),
        false, 1, bgfx::TextureFormat::RGBA8,
        BGFX_TEXTURE_NONE | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
        bgfx::copy(pixels.data(),
                   static_cast<uint32_t>(pixels.size() * sizeof(uint32_t))));
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

    m_texUniform    = bgfx::createUniform("s_particleTex", bgfx::UniformType::Sampler);
    m_paramsUniform = bgfx::createUniform("u_particleParams", bgfx::UniformType::Vec4);

    // 1x1 white fallback texture so the shader can always sample slot 0.
    const uint32_t whitePixel = 0xFFFFFFFFu;
    m_whiteTex = bgfx::createTexture2D(1, 1, false, 1,
        bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
        bgfx::copy(&whitePixel, sizeof(whitePixel)));
    if (!bgfx::isValid(m_whiteTex)) {
        spdlog::error("[ParticleSystem] Failed to create white fallback texture");
    }

    m_splashDropletTex = CreateSplashTexture(64, 0);
    m_splashFoamTex = CreateSplashTexture(128, 1);
    m_splashMistTex = CreateSplashTexture(128, 2);
    m_bubbleTex = CreateSplashTexture(64, 3);
    if (!bgfx::isValid(m_splashDropletTex) ||
        !bgfx::isValid(m_splashFoamTex) ||
        !bgfx::isValid(m_splashMistTex) ||
        !bgfx::isValid(m_bubbleTex)) {
        spdlog::warn("[ParticleSystem] Splash sprites unavailable; "
                     "using the soft fallback sprite");
    }
}

void ParticleSystem::Shutdown() {
    if (bgfx::isValid(m_program)) {
        bgfx::destroy(m_program);
        m_program = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(m_whiteTex)) {
        bgfx::destroy(m_whiteTex);
        m_whiteTex = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(m_splashDropletTex)) {
        bgfx::destroy(m_splashDropletTex);
        m_splashDropletTex = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(m_splashFoamTex)) {
        bgfx::destroy(m_splashFoamTex);
        m_splashFoamTex = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(m_splashMistTex)) {
        bgfx::destroy(m_splashMistTex);
        m_splashMistTex = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(m_bubbleTex)) {
        bgfx::destroy(m_bubbleTex);
        m_bubbleTex = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(m_texUniform)) {
        bgfx::destroy(m_texUniform);
        m_texUniform = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(m_paramsUniform)) {
        bgfx::destroy(m_paramsUniform);
        m_paramsUniform = BGFX_INVALID_HANDLE;
    }
    m_emitters.clear();
    m_splashDroplets.clear();
    m_splashFoam.clear();
    m_splashMist.clear();
    m_fluidParticles.clear();
    m_bubbles.clear();
    m_totalParticles = 0;
}

void ParticleSystem::Reset() {
    m_emitters.clear();
    m_splashDroplets.clear();
    m_splashFoam.clear();
    m_splashMist.clear();
    m_fluidParticles.clear();
    m_bubbles.clear();
    m_totalParticles = 0;
}

size_t ParticleSystem::ParticleCount(entt::entity emitter) const {
    auto it = m_emitters.find(emitter);
    return it != m_emitters.end() ? it->second.particles.size() : 0;
}

void ParticleSystem::RemoveEmitter(entt::entity emitter) {
    auto it = m_emitters.find(emitter);
    if (it == m_emitters.end()) return;
    m_totalParticles = m_totalParticles > it->second.particles.size()
        ? m_totalParticles - it->second.particles.size()
        : 0;
    m_emitters.erase(it);
}

void ParticleSystem::EmitSplash(const glm::vec3& position, int count,
                                float speed, float sizeScale,
                                const glm::vec3& waterColor) {
    count = std::clamp(count, 0, 400);
    if (count == 0) return;

    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const glm::vec3 upward(0.0f, 1.0f, 0.0f);
    const float scale = std::clamp(sizeScale, 0.25f, 4.0f);
    const float killY = position.y - 0.04f;
    const glm::vec3 base =
        glm::clamp(waterColor, glm::vec3(0.0f), glm::vec3(1.0f));
    const glm::vec3 dropletColor =
        glm::mix(glm::vec3(1.0f), base, 0.35f);
    const glm::vec3 foamColor =
        glm::mix(glm::vec3(1.0f), base, 0.45f);
    const glm::vec3 mistColor =
        glm::mix(glm::vec3(0.85f, 0.92f, 1.0f), base, 0.30f);

    auto emit = [&](std::vector<Particle>& target, int amount,
                    auto&& setup) {
        constexpr size_t kLayerCap = 4096;
        if (target.size() + static_cast<size_t>(amount) > kLayerCap) {
            const size_t drop = target.size() +
                                static_cast<size_t>(amount) - kLayerCap;
            target.erase(target.begin(),
                         target.begin() +
                             static_cast<std::ptrdiff_t>(drop));
        }
        for (int i = 0; i < amount; ++i) {
            Particle p;
            setup(p, unit);
            p.age = 0.0f;
            target.push_back(p);
        }
    };

    const int droplets = std::max(1, count * 60 / 100);
    const int foam = std::max(1, count * 25 / 100);
    const int mist = std::max(1, count - droplets - foam);

    // Fast crown droplets: small, bright, ballistic and depth-killed when
    // they fall back through the water surface.
    emit(m_splashDroplets, droplets,
         [&](Particle& p, auto& rng) {
        const float radius = std::sqrt(rng(m_rng)) * 0.45f * scale;
        const float angle = rng(m_rng) * 6.2831853f;
        p.position = position +
            glm::vec3(std::cos(angle) * radius, 0.02f,
                      std::sin(angle) * radius);
        glm::vec3 dir = RandomUnitVector(m_rng);
        dir.y = std::abs(dir.y) * 0.85f + 0.20f;
        dir = glm::normalize(dir);
        p.velocity = upward * (0.30f * speed) +
                     dir * (speed * (0.60f + rng(m_rng) * 0.85f));
        p.lifetime = 0.40f + rng(m_rng) * 0.65f;
        p.sizeScale = scale * (0.22f + rng(m_rng) * 0.42f);
        p.tint = glm::vec4(
            dropletColor * (0.82f + rng(m_rng) * 0.18f),
            0.78f + rng(m_rng) * 0.22f);
        p.killY = killY;
        p.gravityScale = 1.0f;
        p.drag = 0.12f;
    });

    // Foam bubbles: low, wide, textured rings that form the splash crown.
    emit(m_splashFoam, foam,
         [&](Particle& p, auto& rng) {
        const float radius = (0.18f + rng(m_rng) * 0.95f) * scale;
        const float angle = rng(m_rng) * 6.2831853f;
        const glm::vec3 outward(std::cos(angle), 0.0f, std::sin(angle));
        p.position = position +
            glm::vec3(outward.x * radius, 0.015f,
                      outward.z * radius);
        p.velocity = outward * (speed * (0.22f + rng(m_rng) * 0.58f)) +
                     upward * (speed * (0.05f + rng(m_rng) * 0.24f));
        p.lifetime = 0.40f + rng(m_rng) * 0.62f;
        p.sizeScale = scale * (0.32f + rng(m_rng) * 0.55f);
        p.tint = glm::vec4(
            foamColor * (0.92f + rng(m_rng) * 0.08f),
            0.68f + rng(m_rng) * 0.28f);
        p.killY = killY;
        p.gravityScale = 0.40f;
        p.drag = 1.0f;
    });

    // Soft mist: low-alpha noise clouds that linger above the impact.
    emit(m_splashMist, mist,
         [&](Particle& p, auto& rng) {
        const float radius = std::sqrt(rng(m_rng)) * 0.9f * scale;
        const float angle = rng(m_rng) * 6.2831853f;
        const glm::vec3 outward(std::cos(angle), 0.0f, std::sin(angle));
        p.position = position +
            glm::vec3(outward.x * radius, 0.10f + rng(m_rng) * 0.55f,
                      outward.z * radius);
        p.velocity = upward * (speed * (0.10f + rng(m_rng) * 0.28f)) +
                     outward * (speed * (0.08f + rng(m_rng) * 0.30f));
        p.lifetime = 0.95f + rng(m_rng) * 0.90f;
        p.sizeScale = scale * (0.60f + rng(m_rng) * 0.95f);
        p.tint = glm::vec4(
            mistColor * (0.88f + rng(m_rng) * 0.12f),
            0.10f + rng(m_rng) * 0.14f);
        p.killY = -3.402823466e+38F;
        p.gravityScale = 0.10f;
        p.drag = 1.4f;
    });
}

void ParticleSystem::SetFluidParticles(
    const std::vector<glm::vec3>& positions,
    const std::vector<glm::vec3>& velocities,
    const std::vector<uint8_t>& active,
    const glm::vec3& waterColor) {
    m_fluidParticles.clear();
    const size_t count = std::min(
        positions.size(), std::min(velocities.size(), active.size()));
    m_fluidParticles.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        if (!active[i]) continue;
        Particle p;
        p.position = positions[i];
        p.velocity = velocities[i];
        p.age = 0.0f;
        p.lifetime = 1.0f;
        const float speed = glm::length(velocities[i]);
        p.sizeScale = 0.55f + std::min(speed * 0.06f, 0.75f);
        p.tint = glm::vec4(
            glm::mix(glm::vec3(1.0f), glm::clamp(
                waterColor, glm::vec3(0.0f), glm::vec3(1.0f)), 0.30f),
            0.85f);
        m_fluidParticles.push_back(p);
    }
}

void ParticleSystem::EmitBubble(const glm::vec3& position, float surfaceY,
                                float radius) {
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    Particle p;
    const float angle = unit(m_rng) * 6.2831853f;
    const float radial = std::sqrt(unit(m_rng)) * 0.22f * radius;
    p.position = position +
        glm::vec3(std::cos(angle) * radial,
                  unit(m_rng) * 0.16f * radius,
                  std::sin(angle) * radial);
    p.velocity = glm::vec3(
        (unit(m_rng) - 0.5f) * 0.12f,
        0.35f + unit(m_rng) * 0.55f,
        (unit(m_rng) - 0.5f) * 0.12f);
    p.lifetime = std::max(
        0.15f, (surfaceY - p.position.y) / std::max(p.velocity.y, 0.05f));
    p.age = 0.0f;
    p.sizeScale = (0.55f + unit(m_rng) * 0.9f) * radius;
    p.tint = glm::vec4(0.72f, 0.90f, 1.0f, 0.55f);
    p.killY = surfaceY;
    m_bubbles.push_back(p);
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

    // Splash layers use separate runtime pools so they do not need a scene
    // emitter entity and are never serialized.
    auto updateSplashLayer = [&](std::vector<Particle>& particles) {
        for (auto it = particles.begin(); it != particles.end();) {
            Particle& p = *it;
            p.age += dt;
            if (p.age >= p.lifetime) {
                *it = particles.back();
                particles.pop_back();
                continue;
            }
            p.velocity.y -= 9.81f * p.gravityScale * dt;
            p.velocity *= std::max(0.0f, 1.0f - p.drag * dt);
            p.position += p.velocity * dt;
            if (p.killY > -1.0e30f && p.position.y < p.killY &&
                p.velocity.y < 0.0f) {
                *it = particles.back();
                particles.pop_back();
                continue;
            }
            ++it;
        }
    };
    updateSplashLayer(m_splashDroplets);
    updateSplashLayer(m_splashFoam);
    updateSplashLayer(m_splashMist);

    // Rising air bubbles pop at the water surface.
    for (auto it = m_bubbles.begin(); it != m_bubbles.end();) {
        Particle& p = *it;
        p.age += dt;
        if (p.age >= p.lifetime || p.position.y >= p.killY) {
            *it = m_bubbles.back();
            m_bubbles.pop_back();
            continue;
        }
        p.velocity.y += 0.18f * dt;
        p.velocity.x = std::sin(p.age * 11.0f + p.position.z * 3.0f) * 0.08f;
        p.velocity.z = std::cos(p.age * 9.0f + p.position.x * 3.0f) * 0.08f;
        p.velocity *= std::max(0.0f, 1.0f - 0.7f * dt);
        p.position += p.velocity * dt;
        ++it;
    }
    m_totalParticles += m_fluidParticles.size() + m_bubbles.size();
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
        // Spawn either inside a box volume (when configured) or the sphere.
        const bool useBox = emitter.spawnBoxSize.x > 0.0f ||
                            emitter.spawnBoxSize.y > 0.0f ||
                            emitter.spawnBoxSize.z > 0.0f;
        if (useBox) {
            std::uniform_real_distribution<float> sym(-1.0f, 1.0f);
            p.position = origin + glm::vec3(
                sym(m_rng) * emitter.spawnBoxSize.x,
                sym(m_rng) * emitter.spawnBoxSize.y,
                sym(m_rng) * emitter.spawnBoxSize.z);
        } else {
            // Uniform point inside the spawn sphere.
            const glm::vec3 dir = RandomUnitVector(m_rng);
            const float radius = emitter.spawnRadius * std::cbrt(unit(m_rng));
            p.position = origin + dir * radius;
        }

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

    size_t totalParticles = m_splashDroplets.size() +
                            m_splashFoam.size() +
                            m_splashMist.size() +
                            m_fluidParticles.size() +
                            m_bubbles.size();
    for (auto entity : view) {
        auto it = m_emitters.find(entity);
        if (it != m_emitters.end()) {
            totalParticles += it->second.particles.size();
        }
    }
    if (totalParticles == 0) {
        return;
    }

    const glm::mat4& v = camera.View();
    const glm::vec3 right = glm::normalize(glm::vec3(v[0][0], v[1][0], v[2][0]));
    const glm::vec3 up    = glm::normalize(glm::vec3(v[0][1], v[1][1], v[2][1]));

    bgfx::setViewFrameBuffer(viewId, target);
    bgfx::setViewRect(viewId, 0, 0, width, height);
    bgfx::setViewClear(viewId, BGFX_CLEAR_NONE);
    bgfx::setViewTransform(viewId, glm::value_ptr(camera.View()),
                           glm::value_ptr(camera.Projection()));

    auto submitParticles = [&](const std::vector<Particle>& particles,
                               float startSize, float endSize,
                               const glm::vec4& startColor,
                               const glm::vec4& endColor,
                               bgfx::TextureHandle texture,
                               bool useTexture) {
        if (particles.empty()) return;
        uint32_t quads = static_cast<uint32_t>(
            std::min<size_t>(particles.size(), 1u << 20));

        // Clamp to what the transient buffers can currently hold.
        const uint32_t availVerts =
            bgfx::getAvailTransientVertexBuffer(quads * 4, m_layout);
        const uint32_t availIndices =
            bgfx::getAvailTransientIndexBuffer(quads * 6, true);
        quads = std::min(quads, std::min(availVerts / 4, availIndices / 6));
        if (quads == 0) return;

        bgfx::TransientVertexBuffer tvb;
        bgfx::TransientIndexBuffer tib;
        bgfx::allocTransientVertexBuffer(&tvb, quads * 4, m_layout);
        bgfx::allocTransientIndexBuffer(&tib, quads * 6, true);
        if (!tvb.data || !tib.data) return;

        auto* vertices = reinterpret_cast<Vertex*>(tvb.data);
        auto* indices = reinterpret_cast<uint32_t*>(tib.data);

        uint32_t q = 0;
        for (const auto& p : particles) {
            if (q >= quads) break;

            const float t = glm::clamp(p.age / p.lifetime, 0.0f, 1.0f);
            const float size =
                glm::mix(startSize, endSize, t) * p.sizeScale;
            glm::vec4 color =
                glm::mix(startColor, endColor, t) * p.tint;
            color = glm::clamp(color, glm::vec4(0.0f),
                               glm::vec4(1.0f));
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

        bgfx::setTexture(0, m_texUniform, texture);
        const glm::vec4 particleParams(
            useTexture ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
        bgfx::setUniform(m_paramsUniform, glm::value_ptr(particleParams));

        bgfx::setVertexBuffer(0, &tvb);
        bgfx::setIndexBuffer(&tib);
        // The output FBO depth attachment is not the scene depth, so particles
        // are drawn without depth testing.  Alpha blending keeps them soft.
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                       BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                                             BGFX_STATE_BLEND_INV_SRC_ALPHA) |
                       BGFX_STATE_MSAA);
        bgfx::submit(viewId, m_program);
    };

    // Each emitter is submitted as its own batch so it can bind its own
    // particle texture (or the white fallback) to sampler slot 0.
    for (auto entity : view) {
        auto it = m_emitters.find(entity);
        if (it == m_emitters.end() || it->second.particles.empty()) continue;

        const auto& emitter = view.get<ParticleEmitterComponent>(entity);
        const bool useTexture = emitter.texture && emitter.texture->Valid();
        submitParticles(
            it->second.particles, emitter.startSize, emitter.endSize,
            emitter.startColor, emitter.endColor,
            useTexture ? emitter.texture->Handle() : m_whiteTex,
            useTexture);
    }

    // Splash layers use dedicated procedural sprites.
    const bool dropletTex = bgfx::isValid(m_splashDropletTex);
    const bool foamTex = bgfx::isValid(m_splashFoamTex);
    const bool mistTex = bgfx::isValid(m_splashMistTex);
    submitParticles(
        m_splashDroplets, 0.10f, 0.008f,
        {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 0.0f},
        dropletTex ? m_splashDropletTex : m_whiteTex, dropletTex);
    submitParticles(
        m_splashFoam, 0.12f, 0.010f,
        {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 0.0f},
        foamTex ? m_splashFoamTex : m_whiteTex, foamTex);
    submitParticles(
        m_splashMist, 0.16f, 0.020f,
        {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 0.0f},
        mistTex ? m_splashMistTex : m_whiteTex, mistTex);

    // SPlisHSPlasH splash droplets: positions come from the fluid solver.
    submitParticles(
        m_fluidParticles, 0.085f, 0.085f,
        {1.0f, 1.0f, 1.0f, 0.95f},
        {1.0f, 1.0f, 1.0f, 0.95f},
        dropletTex ? m_splashDropletTex : m_whiteTex, dropletTex);

    const bool bubbleTex = bgfx::isValid(m_bubbleTex);
    submitParticles(
        m_bubbles, 0.055f, 0.018f,
        {1.0f, 1.0f, 1.0f, 0.90f}, {1.0f, 1.0f, 1.0f, 0.0f},
        bubbleTex ? m_bubbleTex : m_whiteTex, bubbleTex);
}

} // namespace Galeshi

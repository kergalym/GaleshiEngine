#include "GaleshiEngine/Fluid/FluidSimulation.hpp"

#include <SPlisHSPlasH/FluidModel.h>
#include <SPlisHSPlasH/Simulation.h>
#include <SPlisHSPlasH/TimeManager.h>
#include <SPlisHSPlasH/TimeStep.h>
#include <Utilities/Counting.h>
#include <Utilities/Logger.h>
#include <Utilities/Timing.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <random>

// SPlisHSPlasH defines these globals in the simulator executable. The engine
// hosts the library without its GUI/executable, so own them here.
INIT_LOGGING
INIT_TIMING
INIT_COUNTING


namespace Galeshi {

namespace {
constexpr float kFluidStep = 1.0f / 60.0f;
constexpr float kHiddenY = -2000.0f;
constexpr int kMaxSubsteps = 3;

glm::vec3 ToGlm(const Vector3r& v) {
    return glm::vec3(v.x(), v.y(), v.z());
}

Vector3r ToSph(const glm::vec3& v) {
    return Vector3r(v.x, v.y, v.z);
}

void HideParticle(SPH::FluidModel& model, int i, int maxParticles) {
    const Vector3r hidden(
        0.0f,
        kHiddenY - static_cast<float>(i % maxParticles) * 0.12f,
        0.0f);
    model.setParticleState(i, SPH::ParticleState::Fixed);
    model.setPosition(i, hidden);
    model.setPosition0(i, hidden);
    model.setVelocity(i, Vector3r::Zero());
    model.setVelocity0(i, Vector3r::Zero());
}

} // namespace

struct FluidSimulation::Impl {
    bool initialized = false;
    int maxParticles = 0;
    float radius = 0.055f;
    SPH::Simulation* simulation = nullptr;
    SPH::FluidModel* model = nullptr;

    // Storage owned by this wrapper. SPlisHSPlasH copies these into its model,
    // but keeping them alive makes the initialization path explicit.
    std::vector<Vector3r> initPositions;
    std::vector<Vector3r> initVelocities;
    std::vector<unsigned int> objectIds;

    std::vector<float> age;
    std::vector<float> surfaceY;
    std::vector<uint8_t> active;
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> velocities;

    std::mt19937 rng{0x51F15EEDu};
    float accumulator = 0.0f;
    int activeCount = 0;
};

FluidSimulation::FluidSimulation() : m_impl(std::make_unique<Impl>()) {}
FluidSimulation::~FluidSimulation() { Shutdown(); }

bool FluidSimulation::Init(int maxParticles, float particleRadius) {
    if (m_impl->initialized) return true;
    maxParticles = std::clamp(maxParticles, 64, 8192);
    particleRadius = std::clamp(particleRadius, 0.01f, 0.25f);

    m_impl->maxParticles = maxParticles;
    m_impl->radius = particleRadius;
    m_impl->active.assign(static_cast<size_t>(maxParticles), 0);
    m_impl->age.assign(static_cast<size_t>(maxParticles), 0.0f);
    m_impl->surfaceY.assign(static_cast<size_t>(maxParticles), 0.0f);
    m_impl->positions.assign(static_cast<size_t>(maxParticles), glm::vec3(0.0f));
    m_impl->velocities.assign(static_cast<size_t>(maxParticles), glm::vec3(0.0f));

    m_impl->initPositions.assign(static_cast<size_t>(maxParticles),
                                 Vector3r(0.0f, kHiddenY, 0.0f));
    m_impl->initVelocities.assign(static_cast<size_t>(maxParticles),
                                  Vector3r::Zero());
    m_impl->objectIds.assign(static_cast<size_t>(maxParticles), 0u);

    m_impl->simulation = SPH::Simulation::getCurrent();
    m_impl->simulation->init(static_cast<Real>(particleRadius), false);
    m_impl->simulation->addFluidModel(
        "SplashFluid", static_cast<unsigned int>(maxParticles),
        m_impl->initPositions.data(), m_impl->initVelocities.data(),
        m_impl->objectIds.data(), 0u);
    // The time-step constructor allocates its per-fluid simulation data and
    // registers fields on the models, so it must be selected after the model
    // exists.
    m_impl->simulation->setSimulationMethod(
        SPH::Simulation::ENUM_SIMULATION_PBF);
    m_impl->simulation->deferredInit();

    m_impl->model = m_impl->simulation->getFluidModel(0);
    if (!m_impl->model) {
        m_impl->initialized = false;
        return false;
    }

    for (int i = 0; i < maxParticles; ++i) {
        HideParticle(*m_impl->model, i, maxParticles);
    }
    m_impl->model->setNumActiveParticles(
        static_cast<unsigned int>(maxParticles));

    SPH::TimeManager::getCurrent()->setTimeStepSize(kFluidStep);
    SPH::TimeManager::getCurrent()->setTime(0.0f);
    m_impl->initialized = true;
    m_impl->activeCount = 0;
    m_impl->accumulator = 0.0f;
    return true;
}

void FluidSimulation::Shutdown() {
    if (!m_impl) return;
    if (SPH::Simulation::hasCurrent()) {
        delete SPH::Simulation::getCurrent();
    }
    m_impl->simulation = nullptr;
    m_impl->model = nullptr;
    m_impl->initialized = false;
    m_impl->activeCount = 0;
}

void FluidSimulation::Reset() {
    if (!m_impl->initialized || !m_impl->model) return;
    for (int i = 0; i < m_impl->maxParticles; ++i) {
        HideParticle(*m_impl->model, i, m_impl->maxParticles);
        m_impl->active[static_cast<size_t>(i)] = 0;
        m_impl->age[static_cast<size_t>(i)] = 0.0f;
        m_impl->positions[static_cast<size_t>(i)] = glm::vec3(0.0f, kHiddenY, 0.0f);
        m_impl->velocities[static_cast<size_t>(i)] = glm::vec3(0.0f);
    }
    m_impl->activeCount = 0;
    m_impl->accumulator = 0.0f;
}

void FluidSimulation::SpawnSplash(const glm::vec3& position,
                                  const glm::vec3& velocity,
                                  float strength, float surfaceY, int count) {
    if (!m_impl->initialized || !m_impl->model) return;
    count = std::clamp(count, 0, m_impl->maxParticles);
    if (count == 0) return;

    strength = std::clamp(strength, 0.0f, 4.0f);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const float spread = 0.35f + strength * 0.22f;
    const float impact = std::max(0.0f, -velocity.y);
    const float upward = 1.2f + strength * 1.4f + impact * 0.18f;
    const float radius = 0.30f + strength * 0.35f;

    int spawned = 0;
    std::vector<int> slots;
    slots.reserve(static_cast<size_t>(count));
    for (int i = 0; i < m_impl->maxParticles &&
                    static_cast<int>(slots.size()) < count; ++i) {
        if (!m_impl->active[static_cast<size_t>(i)]) slots.push_back(i);
    }
    if (static_cast<int>(slots.size()) < count) {
        std::vector<int> activeSlots;
        activeSlots.reserve(static_cast<size_t>(m_impl->activeCount));
        for (int i = 0; i < m_impl->maxParticles; ++i) {
            if (m_impl->active[static_cast<size_t>(i)]) activeSlots.push_back(i);
        }
        std::sort(activeSlots.begin(), activeSlots.end(),
                  [&](int a, int b) {
                      return m_impl->age[static_cast<size_t>(a)] >
                             m_impl->age[static_cast<size_t>(b)];
                  });
        for (int i : activeSlots) {
            if (static_cast<int>(slots.size()) >= count) break;
            slots.push_back(i);
        }
    }

    for (int i : slots) {
        if (spawned >= count) break;
        const float angle = unit(m_impl->rng) * 6.2831853f;
        const float radial = std::sqrt(unit(m_impl->rng)) * radius;
        Vector3r p(
            position.x + std::cos(angle) * radial,
            position.y + 0.08f + unit(m_impl->rng) * 0.45f,
            position.z + std::sin(angle) * radial);
        const float outward = spread * (0.45f + unit(m_impl->rng) * 0.75f);
        Vector3r v(
            velocity.x * 0.22f + std::cos(angle) * outward,
            upward * (0.55f + unit(m_impl->rng) * 0.75f),
            velocity.z * 0.22f + std::sin(angle) * outward);

        m_impl->model->setParticleState(i, SPH::ParticleState::Active);
        m_impl->model->setPosition(i, p);
        m_impl->model->setPosition0(i, p);
        m_impl->model->setVelocity(i, v);
        m_impl->model->setVelocity0(i, v);
        m_impl->active[static_cast<size_t>(i)] = 1;
        m_impl->age[static_cast<size_t>(i)] = 0.0f;
        m_impl->surfaceY[static_cast<size_t>(i)] = surfaceY;
        ++spawned;
    }

    m_impl->activeCount = 0;
    for (uint8_t a : m_impl->active) {
        if (a) ++m_impl->activeCount;
    }
}

void FluidSimulation::Update(float dt) {
    if (!m_impl->initialized || !m_impl->model || m_impl->activeCount == 0) {
        m_impl->accumulator = 0.0f;
        return;
    }
    m_impl->accumulator += std::clamp(dt, 0.0f, 0.10f);
    int steps = 0;
    while (m_impl->accumulator >= kFluidStep && steps < kMaxSubsteps) {
        m_impl->simulation->getTimeStep()->step();
        m_impl->accumulator -= kFluidStep;
        ++steps;
    }
    if (steps == kMaxSubsteps) m_impl->accumulator = 0.0f;

    int activeCount = 0;
    for (int i = 0; i < m_impl->maxParticles; ++i) {
        const size_t index = static_cast<size_t>(i);
        if (!m_impl->active[index]) continue;
        m_impl->age[index] += kFluidStep * static_cast<float>(steps);
        const glm::vec3 p = ToGlm(m_impl->model->getPosition(i));
        const glm::vec3 v = ToGlm(m_impl->model->getVelocity(i));
        const bool belowSurface = p.y <
            m_impl->surfaceY[index] - 0.03f;
        if (belowSurface || m_impl->age[index] > 3.0f) {
            HideParticle(*m_impl->model, i, m_impl->maxParticles);
            m_impl->active[index] = 0;
            m_impl->age[index] = 0.0f;
            m_impl->positions[index] = glm::vec3(0.0f, kHiddenY, 0.0f);
            m_impl->velocities[index] = glm::vec3(0.0f);
            continue;
        }
        m_impl->positions[index] = p;
        m_impl->velocities[index] = v;
        ++activeCount;
    }
    m_impl->activeCount = activeCount;
}

bool FluidSimulation::Ready() const {
    return m_impl && m_impl->initialized;
}

int FluidSimulation::MaxParticles() const {
    return m_impl ? m_impl->maxParticles : 0;
}

const std::vector<glm::vec3>& FluidSimulation::Positions() const {
    return m_impl->positions;
}

const std::vector<glm::vec3>& FluidSimulation::Velocities() const {
    return m_impl->velocities;
}

const std::vector<uint8_t>& FluidSimulation::Active() const {
    return m_impl->active;
}

} // namespace Galeshi

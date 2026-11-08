#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace Galeshi {

// SPlisHSPlasH-backed splash fluid. The simulation is a local particle burst
// used for splash droplets, foam and spray; the main water surface remains
// the procedural water body. Particle state is fixed when idle so the solver
// does not drift or consume time when nothing is splashing.
class FluidSimulation {
public:
    FluidSimulation();
    ~FluidSimulation();

    FluidSimulation(const FluidSimulation&) = delete;
    FluidSimulation& operator=(const FluidSimulation&) = delete;

    bool Init(int maxParticles = 1600, float particleRadius = 0.055f);
    void Shutdown();
    void Reset();

    // Adds an impact burst. `surfaceY` is the water surface used to remove
    // droplets that fall back into the body.
    void SpawnSplash(const glm::vec3& position, const glm::vec3& velocity,
                     float strength, float surfaceY, int count);

    // Fixed-step SPH update. Safe to call every frame.
    void Update(float dt);

    bool Ready() const;
    int MaxParticles() const;

    // One entry per pool slot. Use Active() to filter live particles.
    const std::vector<glm::vec3>& Positions() const;
    const std::vector<glm::vec3>& Velocities() const;
    const std::vector<uint8_t>& Active() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Galeshi

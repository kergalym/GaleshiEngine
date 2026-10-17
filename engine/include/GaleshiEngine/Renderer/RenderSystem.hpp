#pragma once

#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include <cstdint>

namespace Galeshi {

class Scene;
class Camera;
class AssetManager;

// Рисует всю сцену: обходит ECS, для каждой сущности с
// MeshComponent+MaterialComponent+TransformComponent делает submit.
class RenderSystem {
public:
    RenderSystem();
    ~RenderSystem();

    void Init(AssetManager* assets);
    void Shutdown();

    void Render(Scene& scene,
                const Camera& camera,
                bgfx::ViewId viewId,
                uint16_t width,
                uint16_t height);

private:
    void ApplyEnvironment(const Camera& camera, const glm::vec3& lightDir,
                          const glm::vec3& lightColor, float lightIntensity);

    AssetManager* m_assets = nullptr;
    bgfx::ProgramHandle m_program = BGFX_INVALID_HANDLE;

    bgfx::UniformHandle u_albedoFactor        = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_metallicFactor      = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_roughnessFactor     = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_emissiveFactor      = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_cameraPosition      = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_lightDirection      = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_lightColorIntensity = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_albedoMap           = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_metallicRoughnessMap= BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_normalMap           = BGFX_INVALID_HANDLE;
};

} // namespace Galeshi

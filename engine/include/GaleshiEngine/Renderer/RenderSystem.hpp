#pragma once
#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include <cstdint>

namespace Galeshi {
class Scene; class Camera; class AssetManager;
class RenderSystem {
public:
    RenderSystem(); ~RenderSystem();
    void Init(AssetManager* assets); void Shutdown();
    void Render(Scene& scene, const Camera& camera, bgfx::ViewId viewId, uint16_t width, uint16_t height);
    static constexpr int kMaxPointLights = 4;
    static constexpr int kMaxBones = 256;
private:
    AssetManager* m_assets = nullptr;
    bgfx::ProgramHandle m_program = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_albedoFactor = BGFX_INVALID_HANDLE, u_metallicFactor = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_roughnessFactor = BGFX_INVALID_HANDLE, u_emissiveFactor = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_cameraPosition = BGFX_INVALID_HANDLE, u_lightDirection = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_lightColorIntensity = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_pointLightPosRadius = BGFX_INVALID_HANDLE, u_pointLightColorInt = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_pointLightCount = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_boneMatrices = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_boneTexture = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_albedoMap = BGFX_INVALID_HANDLE, s_metallicRoughnessMap = BGFX_INVALID_HANDLE, s_normalMap = BGFX_INVALID_HANDLE;
};
} // namespace Galeshi

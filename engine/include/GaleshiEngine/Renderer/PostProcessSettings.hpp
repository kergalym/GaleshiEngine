#pragma once

#include <glm/glm.hpp>

namespace Galeshi {

// Screen-space ambient-occlusion algorithm used by the post-process stack.
enum class AmbientOcclusionMode : int {
    SSAO = 0,
    HBAO = 1,
    GTAO = 2
};

// Tuning parameters for every post-processing effect. A
// PostProcessVolumeComponent overrides these values while the camera is inside
// its bounds; otherwise the editor's global toggles/defaults are used.
struct PostProcessSettings {
    // Bloom
    bool  bloomEnabled   = true;
    float bloomThreshold = 1.2f;
    float bloomIntensity = 0.8f;
    float bloomRadius    = 1.0f;

    // Depth of Field. Disabled globally by default; post-process volumes
    // enable it for their own settings.
    bool  dofEnabled       = false;
    bool  dofAutoFocus     = true;   // focus on the player/character target
    float dofFocusDistance = 1.4f;   // manual focus distance in world units
    float dofBlur          = 13.5f;  // maximum circle-of-confusion size in pixels

    // Motion Blur
    bool  motionBlurEnabled    = true;
    float motionBlurAmount     = 0.4f;  // camera velocity scale
    float motionBlurMaxVelocity = 70.0f; // clamp in pixels

    // Tonemap
    float exposure   = 1.0f;
    float contrast   = 1.0f;
    float saturation = 1.0f;

    // Color correction
    float colorExposure   = 1.0f;
    float colorContrast   = 1.0f;
    float colorSaturation = 1.0f;

    // Vignette
    float vignetteIntensity  = 0.35f;
    float vignetteSmoothness = 0.9f;

    // Chromatic aberration
    float chromaticAberration = 0.004f;

    // Ambient occlusion. ssaoEnabled is the shared on/off switch for all
    // three algorithms; the SSAO fields are kept for existing scenes.
    bool  ssaoEnabled     = false;
    AmbientOcclusionMode aoMode = AmbientOcclusionMode::SSAO;

    // Legacy SSAO parameters.
    float ssaoRadius      = 80.0f;
    float ssaoBias        = 0.0333f;
    float ssaoMaxDistance = 6.9f;
    float ssaoStrength    = 1.1f;

    // HBAO/GTAO parameters (radius and distances are in world units).
    float aoRadius       = 1.5f;
    float aoBias         = 0.05f;
    float aoThickness    = 1.0f; // GTAO depth-rejection thickness
    float aoMaxDistance  = 50.0f;
    float aoStrength     = 1.0f;
    int   aoDirections   = 8;   // HBAO directions / GTAO slices
    int   aoSteps        = 4;   // HBAO samples per direction / GTAO steps per side

    // SSR
    bool  ssrEnabled  = false;
    float ssrIntensity = 1.0f;

    // Anti-aliasing
    bool  fxaaEnabled = false;
    bool  smaaEnabled = false;
    bool  taaEnabled  = false;
    float taaBlend    = 0.05f;

    // God rays
    bool  godRaysEnabled   = false;
    float godRaysIntensity = 1.0f;
    float godRaysDecay     = 0.95f;
    float godRaysDensity   = 0.5f;
    float godRaysWeight    = 0.4f;

    // Atmosphere
    bool  atmosphereEnabled   = false;
    float atmosphereIntensity = 20.0f;

    // Fog. The analytical mode uses an exponential height-fog integral;
    // volumetric mode ray-marches the same density and samples PSSM shadows
    // for directional-light shafts.
    bool      fogEnabled       = false;
    bool      fogVolumetric    = false;
    glm::vec3 fogColor{0.58f, 0.64f, 0.72f};
    float     fogDensity       = 0.004f;
    float     fogHeightFalloff = 0.020f;
    float     fogBaseHeight    = 0.0f;
    float     fogStartDistance = 5.0f;
    float     fogMaxDistance   = 300.0f;
    float     fogSunScattering  = 1.5f;
    float     fogAnisotropy     = 0.55f;
    int       fogSteps          = 24;

    // Invert
    bool invertEnabled = false;
};

} // namespace Galeshi

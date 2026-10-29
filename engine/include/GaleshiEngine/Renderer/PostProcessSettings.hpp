#pragma once

namespace Galeshi {

// Tuning parameters for every post-processing effect. A
// PostProcessVolumeComponent overrides these values while the camera is inside
// its bounds; otherwise the editor's global toggles/defaults are used.
struct PostProcessSettings {
    // Bloom
    bool  bloomEnabled   = true;
    float bloomThreshold = 1.2f;
    float bloomIntensity = 0.8f;
    float bloomRadius    = 1.0f;

    // Depth of Field
    bool  dofEnabled       = true;
    float dofFocusDistance = 1.4f;
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

    // SSAO
    bool  ssaoEnabled     = false;
    float ssaoRadius      = 80.0f;
    float ssaoBias        = 0.0333f;
    float ssaoMaxDistance = 6.9f;
    float ssaoStrength    = 1.1f;

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

    // Invert
    bool invertEnabled = false;
};

} // namespace Galeshi

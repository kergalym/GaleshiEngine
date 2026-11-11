#pragma once

#include <glm/glm.hpp>

#include <string>

namespace Galeshi {

// Screen-space ambient-occlusion algorithm used by the post-process stack.
enum class AmbientOcclusionMode : int {
    SSAO = 0,
    HBAO = 1,
    GTAO = 2
};

// Tuning parameters for every post-processing effect. An enabled
// PostProcessVolumeComponent overrides these values for the entire scene,
// independent of camera position; otherwise the editor's global
// toggles/defaults are used.
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

    // Shadows. PCSS estimates penumbra from a blocker search; contact
    // shadows ray-march screen-space depth toward the sun.
    bool  pcssEnabled           = false;
    float shadowLightSourceSize = 1.0f;   // world units, penumbra scale
    // Cube shadows for the first point light. Costs six scene passes per frame.
    bool  pointShadowsEnabled   = false;
    bool  contactShadowsEnabled = false;
    float contactShadowLength   = 0.05f;  // world units along the light
    float contactShadowThickness = 0.02f; // depth rejection, world units

    // Temporal AA
    float taaSharpen = 0.0f;  // 0 = off, ~0.25 subtle

    // Depth of field bokeh. 0 blades keeps the classic Gaussian blur,
    // 3..8 switches to a polygonal (cinematic) disk kernel.
    float dofBokehBlades = 0.0f;
    float dofBokehRadius = 1.0f;

    // Ambient occlusion temporal accumulation.
    bool  aoTemporalEnabled = false;
    float aoTemporalBlend   = 0.9f;  // 1 = keep all history

    // Tonemap
    float exposure   = 1.0f;
    float exposureEv = 0.0f;  // manual exposure in stops (2^EV), UE-style
    float contrast   = 1.0f;
    float saturation = 1.0f;

    // Eye adaptation. The adapted value is added to the manual exposure in
    // stops. Calibration is the target average scene luminance.
    bool  autoExposureEnabled    = false;
    float autoExposureMinEv      = -4.0f;
    float autoExposureMaxEv      = 4.0f;
    float autoExposureSpeedUp    = 3.0f;
    float autoExposureSpeedDown  = 1.0f;
    float autoExposureCalibration = 0.18f;

    // Scene-referred color grading. Applied before the ACES curve, matching
    // the UE-style linear grading stage.
    float colorExposure   = 1.0f;
    float colorContrast   = 1.0f;
    float colorSaturation = 1.0f;

    // Per-channel UE-style gain/gamma/offset in linear space (pre-tonemap).
    glm::vec3 colorGain{1.0f};
    glm::vec3 colorGamma{1.0f};
    glm::vec3 colorOffset{0.0f};

    // White balance. Higher temperature is warmer (orange), positive tint
    // shifts toward green.
    float colorTemperature = 6500.0f;
    float colorTint        = 0.0f;

    // Shadows/midtones/highlights tints, multiplied in display space after
    // the ACES curve. UE4 bakes these into the grading LUT; we apply them
    // directly.
    glm::vec3 gradeShadows{1.0f};
    glm::vec3 gradeMidtones{1.0f};
    glm::vec3 gradeHighlights{1.0f};

    // Film grain applied after the ACES curve.
    float grainStrength = 0.06f;
    float grainScale    = 1.0f;

    // Environment colour palette.  The final environment render is remapped
    // through a three-point colour ramp (shadows, midtones, highlights) while
    // preserving the original luminance.  This is applied after tonemapping
    // and before UI, so it acts as an environment-wide colour filter.
    bool      environmentPaletteEnabled = false;
    float     environmentPaletteAmount  = 1.0f;
    glm::vec3 environmentPaletteShadow{0.07f, 0.09f, 0.14f};
    glm::vec3 environmentPaletteMid{0.48f, 0.45f, 0.40f};
    glm::vec3 environmentPaletteHighlight{1.0f, 0.94f, 0.82f};

    // Vignette
    float vignetteIntensity  = 0.35f;
    float vignetteSmoothness = 0.9f;

    // Chromatic aberration
    float chromaticAberration = 0.004f;

    // Colour grading LUT: a 2D strip of N slices, each NxN texels, laid out
    // left to right (texture height = N). Applied last, in display space.
    // An empty path disables it.
    std::string colorLutPath;
    float colorLutStrength = 1.0f;

    // Lens flare from the sun, built from the HDR scene above the threshold.
    // Zero intensity disables it. It rides the bloom pass, so bloom must be on.
    float lensFlareIntensity = 0.0f;
    float lensFlareThreshold = 4.0f;

    // Histogram metering: averages the 10..90 percentile of log luminance.
    // Off falls back to the plain log average.
    bool autoExposureHistogram = true;

    // Ambient occlusion. ssaoEnabled is the shared on/off switch for all
    // three algorithms; the SSAO fields are kept for existing scenes.
    bool  ssaoEnabled     = false;
    AmbientOcclusionMode aoMode = AmbientOcclusionMode::SSAO;

    // Legacy SSAO parameters.
    float ssaoRadius      = 80.0f;
    float ssaoBias        = 0.0333f;
    float ssaoMaxDistance = 6.9f;
    float ssaoStrength    = 1.1f;

    // AO evaluation resolution relative to the viewport. Quarter resolution
    // is the fast default for GTAO; switch to half for maximum contact detail.
    float aoResolutionScale = 0.25f;

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

    // Screen-space global illumination. The indirect diffuse bounce is traced
    // at half resolution from the lit HDR buffer and composited with a
    // depth-aware bilateral filter. Disabled by default because it is the most
    // expensive screen-space effect.
    bool  ssgiEnabled   = false;
    float ssgiIntensity = 1.0f;
    float ssgiRadius    = 5.0f;   // maximum ray length in world units
    float ssgiThickness = 0.08f;  // view-space depth bias for a hit
    int   ssgiRays      = 6;      // diffuse hemisphere rays, 1..8
    int   ssgiSteps     = 12;     // screen-space march steps per ray, 2..24

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

};

} // namespace Galeshi

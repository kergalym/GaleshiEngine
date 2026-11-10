$input v_texcoord0
#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);
// Adapted exposure in stops (1x1 R32F), 0 when auto exposure is disabled.
SAMPLER2D(s_exposure, 1);
// x = 1 when eye adaptation is active.
uniform vec4 u_autoExposure;
// x = combined scene exposure, y = film contrast, z = film saturation
uniform vec4 u_colorParams;
// x = scene-referred color-grading contrast, y = grading saturation
uniform vec4 u_gradeParams;
// Per-channel gain/gamma/offset applied in linear space before the ACES curve.
uniform vec4 u_gradeGain;
uniform vec4 u_gradeGamma;
uniform vec4 u_gradeOffset;
// x = grain strength, y = grain UV scale, z = time.
uniform vec4 u_grain;
// x = temperature (K), y = tint (-1..1, positive = green).
uniform vec4 u_whiteBalance;
// Display-space multipliers for the luminance regions (UE4 LUT bakes these).
uniform vec4 u_gradeShadows;
uniform vec4 u_gradeMidtones;
uniform vec4 u_gradeHighlights;

// ACES fitted transform (Stephen Hill fit of the RRT + ODT).
// GLSL mat3 constructors fill columns: these values are deliberately in
// column-major order.
const mat3 ACESInputMat = mat3(
    0.59719, 0.35458, 0.04823,
    0.07600, 0.90834, 0.01566,
    0.02840, 0.13383, 0.83777);
const mat3 ACESOutputMat = mat3(
     1.60475, -0.53108, -0.07367,
    -0.10208,  1.10813, -0.00605,
    -0.00327, -0.07276,  1.07602);

vec3 RRTAndODTFit(vec3 v) {
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return a / b;
}

vec3 ACESFitted(vec3 color) {
    // Negative HDR values can appear after chromatic post effects. Feeding
    // them into the rational fit produces NaN/flat saturated pixels.
    color = max(color, vec3_splat(0.0));
    color = ACESInputMat * color;
    color = RRTAndODTFit(color);
    color = ACESOutputMat * color;
    return clamp(color, 0.0, 1.0);
}

vec3 sceneContrast(vec3 color, float contrast) {
    const float midGrey = 0.18;
    float luma = max(dot(color, vec3(0.2126, 0.7152, 0.0722)), 0.000001);
    float graded = exp2((log2(luma) - log2(midGrey)) * contrast + log2(midGrey));
    return color * (graded / luma);
}

float hashNoise(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

// UE-style white balance: warmer at higher temperature, +tint = green.
vec3 applyWhiteBalance(vec3 c) {
    float t = clamp((u_whiteBalance.x - 6500.0) / 8500.0, -1.0, 1.0);
    vec3 wb = mix(vec3_splat(1.0), vec3(1.26, 0.97, 0.74), max(t, 0.0));
    wb *= mix(vec3_splat(1.0), vec3(0.74, 0.93, 1.26), max(-t, 0.0));
    wb *= vec3_splat(1.0) +
          vec3(-0.10, 0.10, -0.10) * clamp(u_whiteBalance.y, -1.0, 1.0);
    return c * wb;
}

void main() {
    vec3 color = texture2D(s_texColor, v_texcoord0).rgb;

    // Scene-referred grading runs before the filmic curve, matching the
    // Unreal-style pipeline. LUT-style palette grading stays after tonemap.
    color *= max(u_colorParams.x, 0.0);
    // Eye-adapted stops sit on top of the manual exposure multiplier.
    if (u_autoExposure.x > 0.5) {
        color *= exp2(texture2D(s_exposure, vec2(0.5, 0.5)).x);
    }
    color = applyWhiteBalance(color);
    // UE-style per-channel gain -> gamma -> offset in linear space.
    color = max(color * u_gradeGain.xyz + u_gradeOffset.xyz, vec3_splat(0.0));
    color = pow(color, u_gradeGamma.xyz);
    color = sceneContrast(color, u_gradeParams.x);
    float gradingLuma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(gradingLuma), color, u_gradeParams.y);

    color = ACESFitted(color);

    // Contrast around middle grey, then saturation.
    color = clamp((color - 0.5) * u_colorParams.y + 0.5, 0.0, 1.0);
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = clamp(mix(vec3(luma), color, u_colorParams.z), 0.0, 1.0);

    // Shadows/midtones/highlights tints, display space (the UE4 LUT stage).
    float shadowW = 1.0 - smoothstep(0.0, 0.6, luma);
    float highW = smoothstep(0.35, 1.0, luma);
    float midW = 1.0 - shadowW - highW;
    vec3 regionTint = u_gradeShadows.xyz * shadowW +
                      u_gradeMidtones.xyz * midW +
                      u_gradeHighlights.xyz * highW;
    color = clamp(color * clamp(regionTint, vec3_splat(0.0),
                                vec3_splat(4.0)), 0.0, 1.0);

    // Film grain plus a fixed 1-LSB dither so sky/fog gradients do not band.
    vec2 grainUV = v_texcoord0 * u_grain.y + vec2(u_grain.z * 0.31,
                                                  u_grain.z * 0.17);
    float grain = hashNoise(grainUV);
    float dither = hashNoise(gl_FragCoord.xy + vec2(0.5, 0.5));
    color += (grain - 0.5) * max(u_grain.x, 0.0);
    color += (dither - 0.5) / 255.0;
    color = clamp(color, 0.0, 1.0);

    // Keep the result linear. The sRGB backbuffer performs the transfer
    // function in hardware during presentation.
    gl_FragColor = vec4(color, 1.0);
}

$input v_texcoord0
#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);
SAMPLER2D(s_colorLut, 1);
// The exposure/contrast/saturation inputs stay neutral in the post-tonemap
// palette pass; scene-referred grading is performed by fs_tonemap.
uniform vec4 u_colorCorrectionParams; // x = exposure, y = contrast, z = saturation, w = palette amount
uniform vec4 u_envPaletteShadow;
uniform vec4 u_envPaletteMid;
uniform vec4 u_envPaletteHighlight;
// x = LUT size N, y = strength, z = enabled.
uniform vec4 u_colorLutParams;

// Colour grading LUT lookup. The strip holds N slices (blue) of NxN texels:
// red runs along each slice, green down it. Texel indices stay within
// [0, N-1], so bilinear taps never leave a slice.
vec3 applyColorLut(vec3 c) {
    float n = u_colorLutParams.x;
    vec3 p = clamp(c, 0.0, 1.0) * (n - 1.0);
    float b0 = floor(p.b);
    float b1 = min(b0 + 1.0, n - 1.0);
    vec2 uv0 = vec2((b0 * n + p.r + 0.5) / (n * n), (p.g + 0.5) / n);
    vec2 uv1 = vec2((b1 * n + p.r + 0.5) / (n * n), (p.g + 0.5) / n);
    return mix(texture2D(s_colorLut, uv0).rgb, texture2D(s_colorLut, uv1).rgb, p.b - b0);
}

void main() {
    vec3 color = texture2D(s_texColor, v_texcoord0).rgb;

    // Exposure
    color *= u_colorCorrectionParams.x;

    // Contrast
    color = (color - 0.5) * u_colorCorrectionParams.y + 0.5;

    // Saturation
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luminance), color, u_colorCorrectionParams.z);

    // Environment colour palette.  Map luminance through a three-point ramp,
    // then restore the original luminance so the filter changes hue/tone
    // without flattening exposure or local contrast.
    float paletteAmount = clamp(u_colorCorrectionParams.w, 0.0, 1.0);
    if (paletteAmount > 0.0001) {
        float tone = clamp(luminance, 0.0, 1.0);
        vec3 palette = tone < 0.5
            ? mix(u_envPaletteShadow.rgb, u_envPaletteMid.rgb,
                  smoothstep(0.0, 0.5, tone))
            : mix(u_envPaletteMid.rgb, u_envPaletteHighlight.rgb,
                  smoothstep(0.5, 1.0, tone));
        float paletteLuminance = max(
            dot(palette, vec3(0.2126, 0.7152, 0.0722)), 0.0001);
        vec3 graded = palette * (luminance / paletteLuminance);
        color = mix(color, graded, paletteAmount);
    }

    if (u_colorLutParams.z > 0.5) {
        color = mix(color, applyColorLut(color), clamp(u_colorLutParams.y, 0.0, 1.0));
    }
    gl_FragColor = vec4(color, 1.0);
}

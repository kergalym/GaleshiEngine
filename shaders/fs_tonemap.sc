$input v_texcoord0
#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);
// x = combined scene exposure, y = film contrast, z = film saturation
uniform vec4 u_colorParams;
// x = scene-referred color-grading contrast, y = grading saturation
uniform vec4 u_gradeParams;

// ACES fitted transform (Stephen Hill fit of the RRT + ODT).
const mat3 ACESInputMat = mat3(
    0.59719, 0.07600, 0.02840,
    0.35458, 0.90834, 0.13383,
    0.04823, 0.01566, 0.83777);
const mat3 ACESOutputMat = mat3(
    1.60475, -0.10208, -0.00327,
   -0.53108,  1.10813, -0.07276,
   -0.07367, -0.00605,  1.07602);

vec3 RRTAndODTFit(vec3 v) {
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return a / b;
}

vec3 ACESFitted(vec3 color) {
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

void main() {
    vec3 color = texture2D(s_texColor, v_texcoord0).rgb;

    // Scene-referred grading runs before the filmic curve, matching the
    // Unreal-style pipeline. LUT-style palette grading stays after tonemap.
    color *= u_colorParams.x;
    color = sceneContrast(color, u_gradeParams.x);
    float gradingLuma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(gradingLuma), color, u_gradeParams.y);

    color = ACESFitted(color);

    // Contrast around middle grey, then saturation.
    color = clamp((color - 0.5) * u_colorParams.y + 0.5, 0.0, 1.0);
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = clamp(mix(vec3(luma), color, u_colorParams.z), 0.0, 1.0);

    // Keep the result linear. The sRGB backbuffer performs the transfer
    // function in hardware during presentation.
    gl_FragColor = vec4(color, 1.0);
}

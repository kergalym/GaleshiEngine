$input v_texcoord0
#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);
uniform vec4 u_colorCorrectionParams; // x = exposure, y = contrast, z = saturation, w = unused

void main() {
    vec3 color = texture2D(s_texColor, v_texcoord0).rgb;

    // Exposure
    color *= u_colorCorrectionParams.x;

    // Contrast
    color = (color - 0.5) * u_colorCorrectionParams.y + 0.5;

    // Saturation
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luminance), color, u_colorCorrectionParams.z);

    gl_FragColor = vec4(color, 1.0);
}

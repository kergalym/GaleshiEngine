$input v_texcoord0
#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);
uniform vec4 u_colorParams; // x = exposure, y = contrast, z = saturation

// ACES Filmic Tonemapping
vec3 ACESFilm(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 color = texture2D(s_texColor, v_texcoord0).rgb;

    color *= u_colorParams.x;
    color = ACESFilm(color);

    // Contrast around middle grey, then saturation.
    color = clamp((color - 0.5) * u_colorParams.y + 0.5, 0.0, 1.0);
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = clamp(mix(vec3(luma), color, u_colorParams.z), 0.0, 1.0);

    // The backbuffer is not an sRGB framebuffer, so convert manually.
    color = pow(color, vec3(1.0 / 2.2));

    gl_FragColor = vec4(color, 1.0);
}

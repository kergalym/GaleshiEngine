$input v_texcoord0
#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);
uniform vec4 u_vignetteParams; // x = intensity, y = smoothness

void main() {
    vec2 uv = v_texcoord0;
    vec3 color = texture2D(s_texColor, uv).rgb;

    float dist = distance(uv, vec2(0.5));
    // edge0 < edge1 is required: smoothstep with reversed edges is undefined.
    float outer = clamp(u_vignetteParams.y, 0.26, 1.0);
    float vignette = 1.0 - smoothstep(0.25, outer, dist);
    color *= mix(1.0, vignette, u_vignetteParams.x);

    gl_FragColor = vec4(color, 1.0);
}

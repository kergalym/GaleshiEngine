$input v_texcoord0
#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);
uniform vec4 u_chromaParams; // x = intensity, y = unused

void main() {
    vec2 uv = v_texcoord0;
    vec2 center = vec2(0.5);
    vec2 dir = uv - center;
    float dist = length(dir);

    float intensity = u_chromaParams.x * dist;
    vec3 color;
    color.r = texture2D(s_texColor, uv - dir * intensity).r;
    color.g = texture2D(s_texColor, uv).g;
    color.b = texture2D(s_texColor, uv + dir * intensity).b;

    gl_FragColor = vec4(color, 1.0);
}

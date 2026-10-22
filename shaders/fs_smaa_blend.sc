$input v_texcoord0
#include <bgfx_shader.sh>
SAMPLER2D(s_texColor, 0);
SAMPLER2D(s_edges, 1);
uniform vec4 u_texelSize;
void main() {
    vec2 uv = v_texcoord0;
    float e = texture2D(s_edges, uv).r;
    if (e < 0.1) { gl_FragData[0] = texture2D(s_texColor, uv); return; }
    vec3 col = texture2D(s_texColor, uv).rgb;
    vec3 n = texture2D(s_texColor, uv + vec2(0, u_texelSize.y)).rgb;
    vec3 s = texture2D(s_texColor, uv - vec2(0, u_texelSize.y)).rgb;
    vec3 w = texture2D(s_texColor, uv - vec2(u_texelSize.x, 0)).rgb;
    vec3 e_ = texture2D(s_texColor, uv + vec2(u_texelSize.x, 0)).rgb;
    vec3 avg = (n + s + w + e_) * 0.25;
    gl_FragData[0] = vec4(mix(col, avg, 0.5), 1.0);
}

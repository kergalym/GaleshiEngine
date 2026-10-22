$input v_texcoord0
#include <bgfx_shader.sh>
SAMPLER2D(s_texColor, 0);
uniform vec4 u_texelSize;
void main() {
    vec2 uv = v_texcoord0;
    vec3 c = texture2D(s_texColor, uv).rgb;
    vec3 l = texture2D(s_texColor, uv - vec2(u_texelSize.x, 0)).rgb;
    vec3 r = texture2D(s_texColor, uv + vec2(u_texelSize.x, 0)).rgb;
    vec3 t = texture2D(s_texColor, uv - vec2(0, u_texelSize.y)).rgb;
    vec3 b = texture2D(s_texColor, uv + vec2(0, u_texelSize.y)).rgb;
    float edge = (abs(l.r + r.r - 2.0*c.r) + abs(l.g + r.g - 2.0*c.g) + abs(l.b + r.b - 2.0*c.b) +
                  abs(t.r + b.r - 2.0*c.r) + abs(t.g + b.g - 2.0*c.g) + abs(t.b + b.b - 2.0*c.b));
    gl_FragData[0] = vec4(step(0.1, edge), 0, 0, 1);
}

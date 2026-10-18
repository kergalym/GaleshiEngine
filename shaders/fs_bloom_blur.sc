$input v_texcoord0
#include <bgfx_shader.sh>
SAMPLER2D(s_color, 0);
uniform vec4 u_bloomTexelSize;
uniform vec4 u_bloomDir;
void main() {
    vec2 uv = v_texcoord0;
    vec2 off1 = u_bloomDir.xy * u_bloomTexelSize.xy;
    vec3 c = texture2D(s_color, uv).rgb * 0.227027;
    c += texture2D(s_color, uv + off1 * 1.384615).rgb * 0.316216;
    c += texture2D(s_color, uv - off1 * 1.384615).rgb * 0.316216;
    c += texture2D(s_color, uv + off1 * 3.230769).rgb * 0.070270;
    c += texture2D(s_color, uv - off1 * 3.230769).rgb * 0.070270;
    gl_FragColor = vec4(c, 1.0);
}

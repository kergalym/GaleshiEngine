$input v_texcoord0
#include <bgfx_shader.sh>
SAMPLER2D(s_color, 0);
SAMPLER2D(s_bloom, 1);
uniform vec4 u_bloomIntensity;
void main() {
    vec3 base = texture2D(s_color, v_texcoord0).rgb;
    vec3 bloom = texture2D(s_bloom, v_texcoord0).rgb;
    gl_FragColor = vec4(base + bloom * u_bloomIntensity.x, 1.0);
}

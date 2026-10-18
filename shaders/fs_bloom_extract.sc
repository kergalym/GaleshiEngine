$input v_texcoord0
#include <bgfx_shader.sh>
SAMPLER2D(s_color, 0);
uniform vec4 u_threshold;
void main() {
    vec3 c = texture2D(s_color, v_texcoord0).rgb;
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    gl_FragColor = vec4(luma > u_threshold.x ? c : vec3(0.0), 1.0);
}

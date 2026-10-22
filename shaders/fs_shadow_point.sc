$input v_worldPos, v_normal, v_tangent, v_uv
#include <bgfx_shader.sh>
uniform vec4 u_shadowLightPos;
void main() {
    // Write linear distance from light to fragment
    float dist = length(v_worldPos.xyz - u_shadowLightPos.xyz);
    gl_FragColor = vec4(dist, 0.0, 0.0, 1.0);
}

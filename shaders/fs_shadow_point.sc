$input v_worldPos
#include <bgfx_shader.sh>
uniform vec4 u_shadowLightPos;
void main() {
    // Write linear distance from light to fragment
    float dist = length(v_worldPos.xyz - u_shadowLightPos.xyz);
    gl_FragData[0] = vec4(dist, 0.0, 0.0, 1.0);
}

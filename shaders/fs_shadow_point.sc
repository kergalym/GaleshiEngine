$input v_worldPos
#include <bgfx_shader.sh>
uniform vec4 u_lightPos;
void main() {
    // Write linear distance from light to fragment
    float dist = length(v_worldPos.xyz - u_lightPos.xyz);
    gl_FragData[0] = vec4(dist, 0.0, 0.0, 1.0);
}

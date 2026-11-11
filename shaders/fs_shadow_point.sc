$input v_worldPos, v_normal, v_tangent, v_uv, v_motion, v_landscapeBlend
#include <bgfx_shader.sh>
// xyz = light position, w = 1 / far plane.
uniform vec4 u_shadowLightPos;
void main() {
    // Distance over the far plane, so the cleared 1.0 reads as no occluder.
    float dist = length(v_worldPos.xyz - u_shadowLightPos.xyz);
    gl_FragColor = vec4(dist * u_shadowLightPos.w, 0.0, 0.0, 1.0);
}

$input a_position, a_normal
#include <bgfx_shader.sh>
uniform vec4 u_shadowDepthOffset;
void main()
{
    vec4 worldPos = mul(u_model[0], vec4(a_position.xyz, 1.0));
    // Material shadow-depth offset. Zero for regular casters, so this is
    // just one extra transform in the depth-only path.
    if (abs(u_shadowDepthOffset.x) > 1e-6) {
        vec3 worldNormal = normalize(mul(u_model[0], vec4(a_normal, 0.0)).xyz);
        worldPos.xyz += worldNormal * u_shadowDepthOffset.x;
    }
    gl_Position = mul(u_viewProj, worldPos);
}

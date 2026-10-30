$input a_position, a_normal, a_tangent, a_texcoord0, a_indices
$output v_worldPos, v_normal, v_tangent, v_uv, v_random, v_clip
#include <bgfx_shader.sh>

void main()
{
    vec4 world = mul(u_model[0], vec4(a_position, 1.0));
    v_worldPos = world.xyz;
    v_normal = normalize(mul(u_model[0], vec4(a_normal, 0.0)).xyz);
    v_tangent = normalize(mul(u_model[0], vec4(a_tangent, 0.0)).xyz);
    v_uv = a_texcoord0;
    v_random = a_indices.x;
    v_clip = mul(u_viewProj, world);
    gl_Position = v_clip;
}

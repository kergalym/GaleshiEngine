$input a_position, a_normal
$output v_normal, v_worldPos

#include <bgfx_shader.sh>

void main()
{
    vec4 wp = mul(u_model[0], vec4(a_position, 1.0));
    v_worldPos = wp.xyz;
    v_normal   = mul(u_model[0], vec4(a_normal, 0.0)).xyz;
    gl_Position = mul(u_viewProj, wp);
}

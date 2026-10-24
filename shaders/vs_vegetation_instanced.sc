$input a_position, a_normal, a_tangent, a_texcoord0, i_data0, i_data1, i_data2, i_data3
$output v_worldPos, v_normal, v_tangent, v_uv

#include <bgfx_shader.sh>

void main()
{
    mat4 instanceModel = mtxFromCols(i_data0, i_data1, i_data2, i_data3);
    mat4 model = mul(u_model[0], instanceModel);
    vec4 worldPos = mul(model, vec4(a_position, 1.0));

    v_worldPos = worldPos.xyz;
    v_normal   = mul(model, vec4(a_normal, 0.0)).xyz;
    v_tangent  = mul(model, vec4(a_tangent, 0.0)).xyz;
    v_uv       = a_texcoord0;

    gl_Position = mul(u_viewProj, worldPos);
}

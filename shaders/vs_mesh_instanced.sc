$input a_position, a_normal, a_tangent, a_texcoord0, i_data0, i_data1, i_data2, i_data3
$output v_worldPos, v_normal, v_tangent, v_uv, v_motion

#include <bgfx_shader.sh>

uniform mat4 u_prevViewProj;

vec2 clipToScreenUV(vec4 clip) {
    float w = max(clip.w, 0.0001);
    vec2 ndc = clip.xy / w;
    return vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
}

// Generic instanced static-mesh vertex shader. The per-instance matrix is
// concatenated with u_model[0], which lets vegetation use a landscape-space
// transform while repeated scene meshes pass world-space matrices with an
// identity u_model[0].
void main()
{
    mat4 instanceModel = mtxFromCols(i_data0, i_data1, i_data2, i_data3);
    mat4 model = mul(u_model[0], instanceModel);
    vec4 worldPos = mul(model, vec4(a_position, 1.0));

    v_worldPos = worldPos.xyz;
    v_normal   = mul(model, vec4(a_normal, 0.0)).xyz;
    v_tangent  = mul(model, vec4(a_tangent, 0.0)).xyz;
    v_uv       = a_texcoord0;

    // Instanced scene meshes are static; moving rigid objects are submitted
    // through the non-instanced path so their previous model matrix is used.
    vec4 currentClip = mul(u_viewProj, worldPos);
    vec4 previousClip = mul(u_prevViewProj, worldPos);
    v_motion = clipToScreenUV(previousClip) - clipToScreenUV(currentClip);
    gl_Position = currentClip;
}

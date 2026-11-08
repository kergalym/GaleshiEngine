$input a_position, a_normal, a_tangent, a_texcoord0, a_indices, a_weight
$output v_worldPos, v_normal, v_tangent, v_uv, v_motion, v_landscapeBlend

#include <bgfx_shader.sh>

uniform mat4 u_prevViewProj;
uniform mat4 u_prevModel;

vec2 clipToScreenUV(vec4 clip) {
    float w = max(clip.w, 0.0001);
    vec2 ndc = clip.xy / w;
    return vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
}

// Dedicated landscape vertex path. No bone palette, no wind, no vertex
// animation: the terrain geometry is immutable while the game is running.
// a_weight.z carries the landscape paint-layer blend.
void main()
{
    vec4 worldPos = mul(u_model[0], vec4(a_position, 1.0));
    vec4 prevWorld = mul(u_prevModel, vec4(a_position, 1.0));

    v_worldPos = worldPos.xyz;
    v_normal = mul(u_model[0], vec4(a_normal, 0.0)).xyz;
    v_tangent = mul(u_model[0], vec4(a_tangent, 0.0)).xyz;
    v_uv = a_texcoord0;
    v_landscapeBlend = a_weight.z;

    vec4 currentClip = mul(u_viewProj, worldPos);
    vec4 previousClip = mul(u_prevViewProj, prevWorld);
    v_motion = clipToScreenUV(previousClip) - clipToScreenUV(currentClip);
    gl_Position = currentClip;
}

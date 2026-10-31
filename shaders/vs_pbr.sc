$input a_position, a_normal, a_tangent, a_texcoord0, a_indices, a_weight
$output v_worldPos, v_normal, v_tangent, v_uv, v_motion

#include <bgfx_shader.sh>

SAMPLER2D(s_boneMatrices, 3);
uniform mat4 u_prevViewProj;
uniform mat4 u_prevModel;
uniform vec4 u_shadowDepthOffset;

vec2 clipToScreenUV(vec4 clip) {
    float w = max(clip.w, 0.0001);
    vec2 ndc = clip.xy / w;
    return vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
}

mat4 getBoneMatrix(int idx) {
    float y = (float(idx) + 0.5) / 256.0;
    vec4 r0 = texture2D(s_boneMatrices, vec2(0.125, y));
    vec4 r1 = texture2D(s_boneMatrices, vec2(0.375, y));
    vec4 r2 = texture2D(s_boneMatrices, vec2(0.625, y));
    vec4 r3 = texture2D(s_boneMatrices, vec2(0.875, y));
    return mat4(r0, r1, r2, r3);
}

void main()
{
    int i0 = int(a_indices.x + 0.5);
    int i1 = int(a_indices.y + 0.5);
    int i2 = int(a_indices.z + 0.5);
    int i3 = int(a_indices.w + 0.5);

    mat4 skinMat = getBoneMatrix(i0) * a_weight.x +
                   getBoneMatrix(i1) * a_weight.y +
                   getBoneMatrix(i2) * a_weight.z +
                   getBoneMatrix(i3) * a_weight.w;

    vec4 skinnedPos = mul(skinMat, vec4(a_position, 1.0));
    vec4 worldPos   = mul(u_model[0], skinnedPos);
    vec4 prevWorld  = mul(u_prevModel, skinnedPos);

    vec3 worldNormal = mul(u_model[0], mul(skinMat, vec4(a_normal, 0.0))).xyz;
    // Used only by the point-shadow pass; regular scene draws bind zero.
    if (abs(u_shadowDepthOffset.x) > 1e-6) {
        worldPos.xyz += normalize(worldNormal) * u_shadowDepthOffset.x;
    }
    v_worldPos = worldPos.xyz;

    v_normal  = worldNormal;
    v_tangent = mul(u_model[0], mul(skinMat, vec4(a_tangent, 0.0))).xyz;

    v_uv = a_texcoord0;
    vec4 currentClip = mul(u_viewProj, worldPos);
    vec4 previousClip = mul(u_prevViewProj, prevWorld);
    v_motion = clipToScreenUV(previousClip) - clipToScreenUV(currentClip);
    gl_Position = currentClip;
}

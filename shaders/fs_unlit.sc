$input v_worldPos, v_normal, v_tangent, v_uv, v_motion, v_landscapeBlend

#include <bgfx_shader.sh>

SAMPLER2D(s_albedoMap, 0);
SAMPLER2D(s_opacityMap, 9);

uniform vec4 u_albedoFactor;
uniform vec4 u_materialParams;
uniform vec4 u_materialFlags;

void main()
{
    vec4 albedo = texture2D(s_albedoMap, v_uv) * u_albedoFactor;
    const float opacity = texture2D(s_opacityMap, v_uv).r;
    const float alpha = albedo.a * opacity;
    const float cutoff =
        u_materialFlags.w > 0.5 ? u_materialParams.z : 0.0;
    if (alpha < cutoff) {
        discard;
    }

    // The unlit view is an editor diagnostic: keep base colour readable
    // without exposure, lights or the post-process chain.
    gl_FragColor = vec4(pow(albedo.rgb, vec3_splat(1.0 / 2.2)), alpha);
}

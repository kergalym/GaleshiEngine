$input v_worldPos, v_normal, v_tangent, v_uv, v_motion
#include <bgfx_shader.sh>

SAMPLER2D(s_albedoMap,            0);
SAMPLER2D(s_metallicRoughnessMap, 1);
SAMPLER2D(s_normalMap,            2);
SAMPLER2D(s_emissiveMap,          7);
SAMPLER2D(s_occlusionMap,         8);
SAMPLER2D(s_opacityMap,           9);
SAMPLER2D(s_metallicMap,         10);
SAMPLER2D(s_roughnessMap,        11);
SAMPLER2D(s_specularMap,         12);

uniform vec4 u_albedoFactor;
uniform vec4 u_metallicFactor;
uniform vec4 u_roughnessFactor;
uniform vec4 u_emissiveFactor;
// x = normal strength, y = occlusion strength, z = alpha cutoff,
// w = two-sided material flag
uniform vec4 u_materialParams;
// x = separate metallic map, y = separate roughness map,
// z = specular map, w = opacity map
uniform vec4 u_materialFlags;

void main()
{
    vec4 a = texture2D(s_albedoMap, v_uv);
    float opacity = texture2D(s_opacityMap, v_uv).r;
    // Only texture alpha drives the cutout. Legacy FBX materials often carry
    // a fractional albedo/opacity factor (e.g. cornea transparency) while
    // their opacity map is fully white; multiplying the factor in would
    // discard the whole mesh.
    float alpha = a.a * opacity;
    // Alpha testing is only meaningful when the material actually has an
    // opacity map; otherwise a legacy alpha factor would make opaque
    // materials disappear.
    float cutoff = u_materialFlags.w > 0.5 ? u_materialParams.z : 0.0;
    if (alpha < cutoff) discard;

    vec4 mr = texture2D(s_metallicRoughnessMap, v_uv);
    float metallic = mr.b * u_metallicFactor.r;
    float rough    = mr.g * u_roughnessFactor.r;
    if (u_materialFlags.x > 0.5) {
        metallic = texture2D(s_metallicMap, v_uv).r * u_metallicFactor.r;
    }
    if (u_materialFlags.y > 0.5) {
        rough = texture2D(s_roughnessMap, v_uv).r * u_roughnessFactor.r;
    }
    metallic = clamp(metallic, 0.0, 1.0);
    rough    = clamp(rough, 0.04, 1.0);

    vec3 albedo = a.rgb * u_albedoFactor.rgb;

    // Two-sided shading: a back-facing fragment has an interpolated normal
    // pointing away from the viewer. Flip it (and the tangent handedness) so
    // capes, cloth and other open surfaces are lit from the visible side.
    // u_materialParams.w is the per-material Two Sided switch.
    bool backFace = u_materialParams.w > 0.5 && !gl_FrontFacing;
    vec3 N = normalize(v_normal);
    if (backFace) N = -N;

    // Tangent-space normal mapping. If the mesh has no tangent data the
    // geometric normal is kept instead of producing NaNs.
    vec3 T = v_tangent - N * dot(N, v_tangent);
    float tangentLen = length(T);
    if (tangentLen > 1e-4) {
        T /= tangentLen;
        if (backFace) T = -T;
        vec3 B = cross(N, T);
        vec3 tangentNormal = texture2D(s_normalMap, v_uv).xyz * 2.0 - 1.0;
        tangentNormal.xy *= u_materialParams.x;
        N = normalize(mul(mat3(T, B, N), tangentNormal));
    }

    float occlusion = mix(1.0, texture2D(s_occlusionMap, v_uv).r,
                          clamp(u_materialParams.y, 0.0, 1.0));
    vec3 emissive = u_emissiveFactor.rgb * texture2D(s_emissiveMap, v_uv).rgb;
    // White fallback yields 1.0 (standard dielectric F0). A real specular
    // map modulates the dielectric reflectance in the lighting pass.
    float specular = clamp(texture2D(s_specularMap, v_uv).r, 0.0, 1.0);

    gl_FragData[0] = vec4(albedo, metallic);          // RT0
    gl_FragData[1] = vec4(N * 0.5 + 0.5, rough);      // RT1
    // w = 1 + specular keeps 0 reserved for the sky sentinel while carrying
    // the material specular mask to the deferred lighting pass.
    gl_FragData[2] = vec4(v_worldPos, 1.0 + specular); // RT2
    gl_FragData[3] = vec4(emissive, occlusion);        // RT3
    gl_FragData[4] = vec4(v_motion, 0.0, 1.0);         // RT4: screen-space motion
}

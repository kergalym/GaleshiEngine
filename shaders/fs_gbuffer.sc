$input v_worldPos, v_normal, v_tangent, v_uv, v_motion, v_landscapeBlend
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
SAMPLER2D(s_landscapeAlbedoMap,  13);
SAMPLER2D(s_landscapeOrmMap,     14);
SAMPLER2D(s_landscapeNormalMap,  15);

uniform vec4 u_landscapeParams;
uniform vec4 u_landscapeDetail;
uniform vec4 u_albedoFactor;
// x = metallic factor, y = specular strength
uniform vec4 u_metallicFactor;
uniform vec4 u_roughnessFactor;
uniform vec4 u_emissiveFactor;
// x = normal strength, y = occlusion strength, z = alpha cutoff,
// w = two-sided material flag
uniform vec4 u_materialParams;
// x = separate metallic map, y = separate roughness map,
// z = specular map, w = opacity map
uniform vec4 u_materialFlags;
// rgb = subsurface color premultiplied by strength, a = normalized radius
uniform vec4 u_subsurfaceParams;

void main()
{
    float landscapeBlend = u_landscapeParams.x > 0.5
        ? clamp(v_landscapeBlend * u_landscapeParams.y, 0.0, 1.0)
        : 0.0;
    // Macro tiling plus a second higher-frequency octave of the same maps.
    // The octave breaks up the stretched pattern on large carved shores.
    vec2 landscapeUV = v_uv * max(u_landscapeParams.z, 0.001);
    vec2 landscapeDetailUV =
        landscapeUV * max(u_landscapeDetail.x, 1.0) + vec2(0.37, 0.71);
    float landscapeDetailStrength =
        clamp(u_landscapeDetail.y, 0.0, 1.0);
    // Landscape base textures (albedo/normal/ORM) use the same world-scale
    // UVs as the painted layer. This keeps the ground grain stable and avoids
    // stretching one texture across the whole terrain.
    vec2 baseUV = u_landscapeParams.x > 0.5 ? landscapeUV : v_uv;
    vec4 a = texture2D(s_albedoMap, baseUV);
    if (landscapeBlend > 0.0) {
        vec4 layerAlbedo =
            texture2D(s_landscapeAlbedoMap, landscapeUV);
        if (landscapeDetailStrength > 0.0) {
            vec4 detailAlbedo =
                texture2D(s_landscapeAlbedoMap, landscapeDetailUV);
            layerAlbedo.rgb = clamp(
                layerAlbedo.rgb +
                    (detailAlbedo.rgb - vec3(0.5)) *
                        landscapeDetailStrength,
                0.0, 1.0);
        }
        a = mix(a, layerAlbedo, landscapeBlend);
    }
    float opacity = texture2D(s_opacityMap, baseUV).r;
    // Only texture alpha drives the cutout. Legacy FBX materials often carry
    // a fractional albedo/opacity factor (e.g. cornea transparency) while
    // their opacity map is fully white; multiplying the factor in would
    // discard the whole mesh.
    float alpha = a.a * opacity;
    // Alpha testing is enabled only for materials with an opacity map or an
    // explicit albedo-alpha cutout; otherwise a legacy alpha factor would
    // make opaque materials disappear.
    float cutoff = u_materialFlags.w > 0.5 ? u_materialParams.z : 0.0;
    if (alpha < cutoff) discard;

    vec4 mr = texture2D(s_metallicRoughnessMap, baseUV);
    float metallic = mr.b * u_metallicFactor.r;
    float rough    = mr.g * u_roughnessFactor.r;
    if (u_materialFlags.x > 0.5) {
        metallic = texture2D(s_metallicMap, baseUV).r * u_metallicFactor.r;
    }
    if (u_materialFlags.y > 0.5) {
        rough = texture2D(s_roughnessMap, baseUV).r * u_roughnessFactor.r;
    }
    if (landscapeBlend > 0.0) {
        vec4 layerOrm = texture2D(s_landscapeOrmMap, landscapeUV);
        float layerRoughness = layerOrm.g;
        if (landscapeDetailStrength > 0.0) {
            float detailRoughness =
                texture2D(s_landscapeOrmMap, landscapeDetailUV).g;
            layerRoughness = clamp(
                layerRoughness +
                    (detailRoughness - 0.5) *
                        landscapeDetailStrength * 0.5,
                0.0, 1.0);
        }
        metallic = mix(
            metallic, layerOrm.b * u_metallicFactor.r, landscapeBlend);
        rough = mix(
            rough, layerRoughness * u_roughnessFactor.r, landscapeBlend);
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
    vec3 T = vec3(0.0);
    vec3 B = vec3(0.0);
    bool frameValid = false;
    if (landscapeBlend > 0.0) {
        // Landscape UVs are a stable planar projection. Use the authored
        // tangent instead of screen-space derivatives: derivative frames make
        // the tiled shore normal map swim while the camera moves.
        float tangentLen2 = dot(v_tangent, v_tangent);
        if (tangentLen2 > 1e-8) {
            vec3 tangent = v_tangent * inversesqrt(tangentLen2);
            T = tangent - N * dot(N, tangent);
            float tlen2 = dot(T, T);
            if (tlen2 > 1e-8) {
                T *= inversesqrt(tlen2);
                B = cross(T, N);
                frameValid = true;
            }
        }
    } else {
        // Build the tangent frame from screen-space derivatives. This
        // respects mirrored UV islands and works for meshes without authored
        // tangents (the armour and sword meshes).
        vec3 dp1 = dFdx(v_worldPos);
        vec3 dp2 = dFdy(v_worldPos);
        vec2 duv1 = dFdx(v_uv);
        vec2 duv2 = dFdy(v_uv);
        vec3 dp2perp = cross(dp2, N);
        vec3 dp1perp = cross(N, dp1);
        T = dp2perp * duv1.x + dp1perp * duv2.x;
        B = dp2perp * duv1.y + dp1perp * duv2.y;
        float maxLen = max(dot(T, T), dot(B, B));
        if (maxLen > 1e-12) {
            float invmax = inversesqrt(maxLen);
            T *= invmax;
            B *= invmax;
            frameValid = true;
        }
    }
    if (frameValid) {
        vec3 tangentNormal =
            texture2D(s_normalMap, baseUV).xyz * 2.0 - 1.0;
        if (landscapeBlend > 0.0) {
            vec3 layerNormal =
                texture2D(s_landscapeNormalMap, landscapeUV).xyz *
                    2.0 - 1.0;
            if (landscapeDetailStrength > 0.0) {
                vec2 detailNormal =
                    (texture2D(s_landscapeNormalMap, landscapeDetailUV).xy *
                        2.0 - 1.0) * u_landscapeDetail.x;
                layerNormal = normalize(vec3(
                    layerNormal.xy +
                        detailNormal * landscapeDetailStrength * 0.5,
                    layerNormal.z));
            }
            layerNormal.xy *= clamp(u_landscapeParams.w, 0.0, 4.0);
            tangentNormal = normalize(
                mix(tangentNormal, layerNormal, landscapeBlend));
        }
        tangentNormal.xy *= u_materialParams.x;
        N = normalize(mat3(T, B, N) * tangentNormal);
    }

    float occlusionValue = texture2D(s_occlusionMap, baseUV).r;
    if (landscapeBlend > 0.0) {
        float layerOcclusion =
            texture2D(s_landscapeOrmMap, landscapeUV).r;
        occlusionValue = mix(
            occlusionValue, layerOcclusion, landscapeBlend);
    }
    float occlusion = mix(1.0, occlusionValue,
                          clamp(u_materialParams.y, 0.0, 1.0));
    vec3 emissive = u_emissiveFactor.rgb * texture2D(s_emissiveMap, baseUV).rgb;
    // White fallback yields 1.0 (standard dielectric F0). A real specular
    // map modulates the dielectric reflectance in the lighting pass.
    float specular = clamp(texture2D(s_specularMap, baseUV).r, 0.0, 1.0) *
                     clamp(u_metallicFactor.y, 0.0, 1.0);

    gl_FragData[0] = vec4(albedo, metallic);          // RT0
    gl_FragData[1] = vec4(N * 0.5 + 0.5, rough);      // RT1
    // w = 1 + specular keeps 0 reserved for the sky sentinel while carrying
    // the material specular mask to the deferred lighting pass.
    gl_FragData[2] = vec4(v_worldPos, 1.0 + specular); // RT2
    gl_FragData[3] = vec4(emissive, occlusion);        // RT3
    gl_FragData[4] = vec4(v_motion, 0.0, 1.0);         // RT4: screen-space motion
    gl_FragData[5] = u_subsurfaceParams;               // RT5: SSS color + radius
}

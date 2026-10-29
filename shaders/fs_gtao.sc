$input v_texcoord0
#include <bgfx_shader.sh>

// Half-resolution Ground Truth Ambient Occlusion (GTAO).
//
// Horizon integration follows the formulation from Jimenez et al.,
// "Practical Realtime Strategies for Accurate Indirect Occlusion" (2016).
// For each pixel a small set of view-space directions is evaluated. Each
// direction defines a slice through the view vector; both sides of the slice
// are sampled, the two horizon cosines are integrated analytically, and the
// resulting visibility is averaged over the directions.

SAMPLER2D(s_worldPos,    0);
SAMPLER2D(s_normalRough, 1);

uniform mat4 u_aoView;
uniform mat4 u_aoProj;
uniform vec4 u_aoParams;   // x=radius (world), y=bias, z=max distance, w=strength
uniform vec4 u_aoParams2;  // x=directions, y=steps, z=falloff start, w=thickness
uniform vec4 u_aoViewport; // xy=full resolution, zw=1/full resolution

#define GTAO_PI 3.14159265359
#define GTAO_MAX_DIRECTIONS 16
#define GTAO_MAX_STEPS 16

float gtaoHash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec2 gtaoProjectToUV(vec3 viewPos)
{
    vec4 clip = mul(u_aoProj, vec4(viewPos, 1.0));
    if (clip.w <= 0.0001) return vec2(-1.0, -1.0);
    vec3 ndc = clip.xyz / clip.w;
    return vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
}

// Returns xyz = view-space position, w = 1 when the pixel contains geometry.
vec4 gtaoSampleViewPosition(vec2 uv)
{
    vec4 wp = texture2D(s_worldPos, uv);
    if (wp.w < 0.5) return vec4(0.0, 0.0, 0.0, -1.0);
    return vec4(mul(u_aoView, vec4(wp.xyz, 1.0)).xyz, 1.0);
}

// Reconstruct the geometric normal from neighbouring world positions. This
// is independent of the material's two-sided normal flag and avoids feeding
// a flipped normal into the horizon integration.
vec3 gtaoGeometricNormal(vec2 uv, vec3 viewPos)
{
    vec2 texel = u_aoViewport.zw;
    vec4 left  = gtaoSampleViewPosition(uv - vec2(texel.x, 0.0));
    vec4 right = gtaoSampleViewPosition(uv + vec2(texel.x, 0.0));
    vec4 up    = gtaoSampleViewPosition(uv - vec2(0.0, texel.y));
    vec4 down  = gtaoSampleViewPosition(uv + vec2(0.0, texel.y));

    vec3 normal;
    if (left.w > 0.5 && right.w > 0.5 && up.w > 0.5 && down.w > 0.5) {
        vec3 dx = right.xyz - left.xyz;
        vec3 dy = down.xyz - up.xyz;
        normal = cross(dx, dy);
    } else {
        normal = texture2D(s_normalRough, uv).xyz * 2.0 - 1.0;
        normal = mul(mat3(u_aoView), normal);
    }

    float len = length(normal);
    if (len < 1e-6) {
        normal = normalize(-viewPos);
    } else {
        normal /= len;
    }

    // AO only cares about the visible side of the surface.
    vec3 toCamera = normalize(-viewPos);
    if (dot(normal, toCamera) < 0.0) normal = -normal;
    return normal;
}

void main()
{
    ivec2 coord = ivec2(gl_FragCoord.xy);

    // Each half-resolution pixel maps to the centre of a 2x2 full-res block.
    vec2 uv = (vec2(coord) * 2.0 + 1.0) * u_aoViewport.zw;

    vec4 centerWp = texture2D(s_worldPos, uv);
    if (centerWp.w < 0.5) {
        gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    vec3 viewPos = mul(u_aoView, vec4(centerWp.xyz, 1.0)).xyz;
    float viewDist = max(-viewPos.z, 0.001);
    if (u_aoParams.z > 0.0 && viewDist > u_aoParams.z) {
        gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    vec3 viewNormal = gtaoGeometricNormal(uv, viewPos);
    vec3 viewDir = normalize(-viewPos);

    float radius = max(u_aoParams.x, 0.001);
    float bias = max(u_aoParams.y, 0.0001);
    float thickness = max(u_aoParams2.w, 0.0001);
    float strength = max(u_aoParams.w, 0.0);

    int numDirections = int(clamp(u_aoParams2.x, 1.0, float(GTAO_MAX_DIRECTIONS)));
    int numSteps = int(clamp(u_aoParams2.y, 1.0, float(GTAO_MAX_STEPS)));

    // Per-pixel rotation and step jitter decorrelate the sampling pattern.
    float baseAngle = gtaoHash12(gl_FragCoord.xy) * GTAO_PI;
    float stepJitter = 0.5 + 0.5 * gtaoHash12(gl_FragCoord.xy + vec2(53.0, 29.0));

    float ao = 0.0;

    for (int i = 0; i < GTAO_MAX_DIRECTIONS; ++i) {
        if (i >= numDirections) break;

        float angle = baseAngle + float(i) * GTAO_PI / float(numDirections);
        vec3 sampleDir = vec3(cos(angle), sin(angle), 0.0);

        // Slice basis and the projection of the surface normal into the slice.
        vec3 sliceBitangent = cross(sampleDir, viewDir);
        float bitangentLen = length(sliceBitangent);
        if (bitangentLen < 1e-5) continue;
        sliceBitangent /= bitangentLen;

        vec3 sliceTangent = cross(sliceBitangent, viewDir);
        vec3 normalInSlice = viewNormal -
                             sliceBitangent * dot(viewNormal, sliceBitangent);
        float normalLen = length(normalInSlice);
        if (normalLen < 1e-5) continue;
        normalInSlice /= normalLen;

        vec3 tangentToNormalInSlice = cross(normalInSlice, sliceBitangent);
        vec2 cosHorizons = vec2(dot(viewDir, tangentToNormalInSlice),
                                dot(viewDir, -tangentToNormalInSlice));
        cosHorizons = clamp(cosHorizons, vec2(-1.0), vec2(1.0));

        // Integrate the two horizon directions of this slice.
        for (int j = 0; j < GTAO_MAX_STEPS; ++j) {
            if (j >= numSteps) break;

            float ratio = float(j + 1) / float(numSteps);
            vec3 sampleViewOffset = sampleDir * radius * stepJitter * ratio;

            // Positive side of the slice.
            vec2 sampleUV = gtaoProjectToUV(viewPos + sampleViewOffset);
            if (sampleUV.x >= 0.0 && sampleUV.x <= 1.0 &&
                sampleUV.y >= 0.0 && sampleUV.y <= 1.0) {
                vec4 wp = texture2D(s_worldPos, sampleUV);
                if (wp.w >= 0.5) {
                    vec3 sampleViewPos = mul(u_aoView, vec4(wp.xyz, 1.0)).xyz;
                    vec3 viewDelta = sampleViewPos - viewPos;
                    float dist = length(viewDelta);
                    if (dist > bias && abs(viewDelta.z) < thickness) {
                        float sampleCosHorizon = dot(viewDir, viewDelta / dist);
                        float blend = 2.0 / float(j + 2);
                        cosHorizons.x += max(0.0,
                            (sampleCosHorizon - cosHorizons.x) * blend);
                    }
                }
            }

            // Negative side of the slice.
            sampleUV = gtaoProjectToUV(viewPos - sampleViewOffset);
            if (sampleUV.x >= 0.0 && sampleUV.x <= 1.0 &&
                sampleUV.y >= 0.0 && sampleUV.y <= 1.0) {
                vec4 wp = texture2D(s_worldPos, sampleUV);
                if (wp.w >= 0.5) {
                    vec3 sampleViewPos = mul(u_aoView, vec4(wp.xyz, 1.0)).xyz;
                    vec3 viewDelta = sampleViewPos - viewPos;
                    float dist = length(viewDelta);
                    if (dist > bias && abs(viewDelta.z) < thickness) {
                        float sampleCosHorizon = dot(viewDir, viewDelta / dist);
                        float blend = 2.0 / float(j + 2);
                        cosHorizons.y += max(0.0,
                            (sampleCosHorizon - cosHorizons.y) * blend);
                    }
                }
            }
        }

        vec2 sinHorizons = sqrt(max(vec2(0.0), 1.0 - cosHorizons * cosHorizons));
        float nx = dot(normalInSlice, sliceTangent);
        float ny = dot(normalInSlice, viewDir);
        float nxb = 0.5 * (acos(cosHorizons.y) - acos(cosHorizons.x)
                         + sinHorizons.x * cosHorizons.x
                         - sinHorizons.y * cosHorizons.y);
        float nyb = 0.5 * (2.0 - cosHorizons.x * cosHorizons.x
                                - cosHorizons.y * cosHorizons.y);
        ao += nx * nxb + ny * nyb;
    }

    ao = clamp(ao / float(numDirections), 0.0, 1.0);
    ao = pow(ao, max(strength, 0.0001));

    gl_FragColor = vec4(ao, ao, ao, 1.0);
}

$input v_texcoord0
#include <bgfx_shader.sh>

// Half-resolution horizon-based ambient occlusion.
//
// The G-buffer provides world position and world normal. View-space positions
// are reconstructed with the camera view matrix, so this works independently
// of the backend's depth convention. The horizon search follows the classic
// HBAO formulation (Bavoil & Sainz): march along screen-space directions,
// track the maximum horizon tangent, and integrate the visible altitude.

SAMPLER2D(s_worldPos,    0);
SAMPLER2D(s_normalRough, 1);

uniform mat4 u_aoView;
uniform mat4 u_aoProj;
uniform vec4 u_aoParams;   // x=radius (world), y=bias, z=max distance, w=strength
uniform vec4 u_aoParams2;  // x=directions, y=steps
uniform vec4 u_aoViewport; // xy=full resolution, zw=1/full resolution

#define HBAO_PI 3.14159265359
#define HBAO_MAX_DIRECTIONS 16
#define HBAO_MAX_STEPS 16

float hbaoLength2(vec3 v)
{
    return dot(v, v);
}

float hbaoHash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// Signed tangent of the elevation angle of V above the view plane.
float hbaoTangent(vec3 v)
{
    float len2 = dot(v.xy, v.xy);
    return v.z * inversesqrt(max(len2, 1e-8));
}

float hbaoTanToSin(float x)
{
    return x * inversesqrt(x * x + 1.0);
}

vec3 hbaoMinDiff(vec3 p, vec3 pr, vec3 pl)
{
    vec3 v1 = pr - p;
    vec3 v2 = p - pl;
    return hbaoLength2(v1) < hbaoLength2(v2) ? v1 : v2;
}

float hbaoFalloff(float d2, float negInvR2)
{
    return max(0.0, d2 * negInvR2 + 1.0);
}

float hbaoHorizonOcclusion(vec2 centerUV, vec2 deltaUV, vec3 p,
                           vec3 dPdu, vec3 dPdv,
                           float randStep, float numSteps,
                           float radius2, float negInvR2, float tanBias)
{
    // Offset the first sample to decorrelate neighbouring pixels.
    vec2 uv = centerUV + deltaUV * randStep;

    // Tangent of the surface in the sampling direction.
    vec3 t = deltaUV.x * dPdu + deltaUV.y * dPdv;
    float tanH = hbaoTangent(t) + tanBias;
    float sinH = hbaoTanToSin(tanH);
    float ao = 0.0;

    for (int s = 0; s < HBAO_MAX_STEPS; ++s) {
        if (float(s) >= numSteps) break;

        uv += deltaUV;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;

        vec4 wp = texture2D(s_worldPos, uv);
        if (wp.w < 0.5) continue;

        vec3 samplePos = mul(u_aoView, vec4(wp.xyz, 1.0)).xyz;
        vec2 deltaXY = samplePos.xy - p.xy;
        float tanS = -(p.z - samplePos.z) *
                     inversesqrt(max(dot(deltaXY, deltaXY), 1e-8));
        float d2 = hbaoLength2(samplePos - p);

        // Keep only samples inside the world-space AO radius that raise the
        // horizon; integrate the altitude difference.
        if (d2 < radius2 && tanS > tanH) {
            float sinS = hbaoTanToSin(tanS);
            ao += hbaoFalloff(d2, negInvR2) * (sinS - sinH);
            tanH = tanS;
            sinH = sinS;
        }
    }

    return ao;
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

    vec3 p = mul(u_aoView, vec4(centerWp.xyz, 1.0)).xyz;
    float viewDist = max(-p.z, 0.001);
    if (viewDist > u_aoParams.z) {
        gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    float radius = max(u_aoParams.x, 0.001);
    float radius2 = radius * radius;
    float negInvR2 = -1.0 / radius2;
    float tanBias = max(u_aoParams.y, 0.0001);
    float strength = max(u_aoParams.w, 0.0);

    int directions = int(clamp(u_aoParams2.x, 1.0, float(HBAO_MAX_DIRECTIONS)));
    float numSteps = clamp(u_aoParams2.y, 1.0, float(HBAO_MAX_STEPS));

    // Project the world-space radius to pixels. The vertical projection term
    // is sufficient for square pixels; the horizontal radius is equivalent.
    float focalPix = 0.5 * u_aoViewport.y * u_aoProj[1][1];
    float rayRadiusPix = min(focalPix * radius / viewDist, 256.0);
    if (rayRadiusPix < 1.0) {
        gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    // Avoid oversampling when the projected radius is only a few pixels.
    numSteps = min(numSteps, rayRadiusPix);
    float stepSizePix = rayRadiusPix / (numSteps + 1.0);
    vec2 stepSizeUV = stepSizePix * u_aoViewport.zw;

    // Build the local tangent basis from neighbouring world positions. This
    // captures the actual depth slope of the surface. If a neighbour is
    // missing, fall back to a basis derived from the G-buffer normal.
    vec2 texel = u_aoViewport.zw;
    vec4 wpR = texture2D(s_worldPos, uv + vec2(texel.x, 0.0));
    vec4 wpL = texture2D(s_worldPos, uv - vec2(texel.x, 0.0));
    vec4 wpT = texture2D(s_worldPos, uv + vec2(0.0, texel.y));
    vec4 wpB = texture2D(s_worldPos, uv - vec2(0.0, texel.y));

    vec3 viewN = normalize(mul(mat3(u_aoView),
                               texture2D(s_normalRough, uv).xyz * 2.0 - 1.0));
    // Two-sided materials may store a normal facing away from the viewer.
    // The fallback basis must use the visible side of the surface.
    if (dot(viewN, normalize(-p)) < 0.0) viewN = -viewN;
    vec3 helper = abs(viewN.z) < 0.999 ? vec3(0.0, 0.0, 1.0)
                                       : vec3(1.0, 0.0, 0.0);
    vec3 fallbackT = normalize(cross(helper, viewN));
    vec3 fallbackB = cross(viewN, fallbackT);

    vec3 dPdu;
    vec3 dPdv;

    if (wpR.w >= 0.5 && wpL.w >= 0.5) {
        vec3 pr = mul(u_aoView, vec4(wpR.xyz, 1.0)).xyz;
        vec3 pl = mul(u_aoView, vec4(wpL.xyz, 1.0)).xyz;
        dPdu = hbaoMinDiff(p, pr, pl);
    } else if (wpR.w >= 0.5) {
        dPdu = mul(u_aoView, vec4(wpR.xyz, 1.0)).xyz - p;
    } else if (wpL.w >= 0.5) {
        dPdu = p - mul(u_aoView, vec4(wpL.xyz, 1.0)).xyz;
    } else {
        dPdu = fallbackT;
    }

    if (wpT.w >= 0.5 && wpB.w >= 0.5) {
        vec3 pt = mul(u_aoView, vec4(wpT.xyz, 1.0)).xyz;
        vec3 pb = mul(u_aoView, vec4(wpB.xyz, 1.0)).xyz;
        dPdv = hbaoMinDiff(p, pt, pb);
    } else if (wpT.w >= 0.5) {
        dPdv = mul(u_aoView, vec4(wpT.xyz, 1.0)).xyz - p;
    } else if (wpB.w >= 0.5) {
        dPdv = p - mul(u_aoView, vec4(wpB.xyz, 1.0)).xyz;
    } else {
        dPdv = fallbackB;
    }

    if (hbaoLength2(dPdu) < 1e-8) dPdu = fallbackT;
    if (hbaoLength2(dPdv) < 1e-8) dPdv = fallbackB;

    // Rotate the sampling pattern per pixel.
    float angle = hbaoHash12(gl_FragCoord.xy) * 2.0 * HBAO_PI;
    vec2 rot = vec2(cos(angle), sin(angle));
    float randStep = hbaoHash12(gl_FragCoord.xy + vec2(17.0, 31.0));

    float ao = 0.0;
    for (int d = 0; d < HBAO_MAX_DIRECTIONS; ++d) {
        if (d >= directions) break;

        float theta = 2.0 * HBAO_PI * float(d) / float(directions);
        vec2 dir = vec2(cos(theta), sin(theta));
        dir = vec2(dir.x * rot.x - dir.y * rot.y,
                   dir.x * rot.y + dir.y * rot.x);

        vec2 deltaUV = dir * stepSizeUV;
        ao += hbaoHorizonOcclusion(uv, deltaUV, p, dPdu, dPdv,
                                   randStep, numSteps, radius2,
                                   negInvR2, tanBias);
    }

    ao = 1.0 - (ao / float(directions)) * strength;
    ao = clamp(ao, 0.0, 1.0);

    gl_FragColor = vec4(ao, ao, ao, 1.0);
}

$input v_texcoord0
#include <bgfx_shader.sh>

// Screen-space lighting composite.
//   1. SSGI: depth-aware bilateral upsample of the half-resolution diffuse
//      bounce and addition to the lit HDR colour.
//   2. SSR: the existing approximate specular reflection march.
// Both effects share this pass so the post-process view ordering stays compact.

SAMPLER2D(s_texColor, 0);
SAMPLER2D(s_ssrNormal, 1);
SAMPLER2D(s_texDepth, 2);
SAMPLER2D(s_ssgi, 3);
SAMPLER2D(s_worldPos, 4);
SAMPLER2D(s_albedoMr, 5);
SAMPLER2D(s_aoTex, 6);
SAMPLER2D(s_materialTex, 7);

uniform mat4 u_ssrInvProj;
uniform mat4 u_ssrProj;
uniform mat4 u_ssgiView;
uniform vec4 u_ssrParams;            // x = SSR intensity, y = SSR enabled
uniform vec4 u_ssgiSource;           // xy = source size, zw = 1/source size
uniform vec4 u_ssgiCompositeParams;  // x = SSGI intensity, y = SSGI enabled
uniform vec4 u_cameraPosition;
uniform vec4 u_texelSize;

vec3 getViewPos(vec2 uv, float depth) {
    vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    vec4 clip = vec4(ndc, depth, 1.0);
    vec4 view = mul(u_ssrInvProj, clip);
    return view.xyz / view.w;
}

float viewDepth(vec3 worldPos) {
    return -mul(u_ssgiView, vec4(worldPos, 1.0)).z;
}

vec3 sampleSSGI(vec2 uv, vec3 centerWp, vec3 centerN) {
    float centerDepth = viewDepth(centerWp);
    vec3 accumulated = vec3(0.0);
    float totalWeight = 0.0;
    vec2 texel = u_ssgiSource.zw;

    // 3x3 bilateral gathering suppresses the half-resolution ray noise while
    // preserving depth/normal discontinuities.
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 sampleUV = uv + vec2(float(x), float(y)) * texel;
            vec4 sampleWp = texture2D(s_worldPos, sampleUV);
            if (sampleWp.w < 0.5) continue;

            vec3 sampleN = normalize(
                texture2D(s_ssrNormal, sampleUV).rgb * 2.0 - 1.0);
            float sampleDepth = viewDepth(sampleWp.xyz);
            float depthWeight = 1.0 - saturate(
                abs(sampleDepth - centerDepth) / 0.75);
            float normalWeight = pow(max(dot(sampleN, centerN), 0.0), 4.0);
            float spatialWeight = exp(-float(x * x + y * y) * 0.35);
            float weight = depthWeight * normalWeight * spatialWeight;
            accumulated += texture2D(s_ssgi, sampleUV).rgb * weight;
            totalWeight += weight;
        }
    }

    if (totalWeight < 1e-4) {
        return texture2D(s_ssgi, uv).rgb;
    }
    return accumulated / totalWeight;
}

void main() {
    vec2 uv = v_texcoord0;
    vec4 col = texture2D(s_texColor, uv);
    vec4 nR = texture2D(s_ssrNormal, uv);
    float depth = texture2D(s_texDepth, uv).r;

    // Background keeps its sky colour; screen-space lighting has no surface
    // to receive or reflect light here.
    if (depth >= 0.9999) {
        gl_FragColor = col;
        return;
    }

    vec3 N = normalize(nR.rgb * 2.0 - 1.0);
    float roughness = nR.a;

    // --- SSGI diffuse bounce ------------------------------------------------
    if (u_ssgiCompositeParams.y > 0.5) {
        vec4 world = texture2D(s_worldPos, uv);
        if (world.w >= 0.5) {
            vec4 albedoMr = texture2D(s_albedoMr, uv);
            float ssao = saturate(texture2D(s_aoTex, uv).r);
            float materialAO = saturate(texture2D(s_materialTex, uv).a);
            vec3 indirect = sampleSSGI(uv, world.xyz, N);
            float diffuseWeight = (1.0 - saturate(albedoMr.a)) * ssao * materialAO;
            col.rgb += indirect * albedoMr.rgb * diffuseWeight *
                       max(u_ssgiCompositeParams.x, 0.0);
        }
    }

    // --- SSR specular reflection -------------------------------------------
    if (u_ssrParams.y < 0.5 || roughness > 0.4) {
        gl_FragColor = col;
        return;
    }

    vec3 viewPos = getViewPos(uv, depth);
    vec3 V = normalize(viewPos);
    vec3 R = reflect(V, N);

    // Project the ray start and march in screen space.
    vec3 startPos = viewPos + N * 0.05;
    vec3 rayEnd = startPos + R * 30.0;

    const int STEPS = 32;
    bool hit = false;
    vec3 hitColor = vec3(0.0);
    float hitFade = 0.0;

    for (int i = 1; i <= STEPS; ++i) {
        float t = float(i) / float(STEPS);
        vec3 rayPos = mix(startPos, rayEnd, t);

        // Approximate projection, matching the original SSR implementation.
        vec2 sampleUV = mix(uv, uv + R.xy * 0.5, t);
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 ||
            sampleUV.y < 0.0 || sampleUV.y > 1.0) break;

        float sampleDepth = texture2D(s_texDepth, sampleUV).r;
        if (sampleDepth >= 0.9999) continue;

        vec3 samplePos = getViewPos(sampleUV, sampleDepth);
        float dz = samplePos.z - rayPos.z;
        if (dz > 0.0 && dz < 1.0) {
            hit = true;
            hitColor = texture2D(s_texColor, sampleUV).rgb;
            hitFade = 1.0 - t;
            break;
        }
    }

    if (hit) {
        float fresnel = pow(1.0 - max(dot(N, -V), 0.0), 3.0);
        float blend = fresnel * (1.0 - roughness) * hitFade * 0.5;
        blend *= max(u_ssrParams.x, 0.0);
        col.rgb = mix(col.rgb, hitColor, blend);
    }

    gl_FragColor = col;
}

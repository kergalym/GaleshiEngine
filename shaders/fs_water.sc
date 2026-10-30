$input v_worldPos, v_normal, v_uv, v_clip, v_viewZ, v_wave
#include <bgfx_shader.sh>

SAMPLER2D(s_waterSceneColor, 0);
SAMPLER2D(s_waterDepth, 1);
SAMPLERCUBE(s_envCube, 2);
SAMPLER2D(s_waterPlanar, 3);

uniform vec4 u_waterParams0; // x=time, y=wave height, z=wave scale, w=wave speed
uniform vec4 u_waterParams1; // x=choppiness, y=detail strength,
                             // z=refraction strength, w=reflection strength
uniform vec4 u_waterParams2; // x=depth fade, y=absorption,
                             // z=foam distance, w=foam intensity
uniform vec4 u_waterParams3; // x=clarity, y=transparency,
                             // z=fresnel power, w=sparkle
uniform vec4 u_waterFlow;    // xy=flow direction
uniform vec4 u_waterInvNearFar; // x=1/near, y=1/far
uniform vec4 u_waterPlanar;    // x=enabled, y=plane height, z=strength
uniform vec4 u_waterShallow;
uniform vec4 u_waterDeep;
uniform vec4 u_waterFoam;
uniform vec4 u_cameraPosition;
uniform vec4 u_lightDirection;
uniform vec4 u_lightColorIntensity;

#define PI 3.14159265359

float hash21(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float valueNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

void main()
{
    vec2 screenUV = v_clip.xy / max(v_clip.w, 0.0001) * 0.5 + 0.5;

    // Manual depth test against the opaque G-buffer. The water is rendered
    // after the scene into a color-only target, so it cannot use hardware
    // depth testing without a feedback loop on the depth attachment.
    float sceneDepth = texture2D(s_waterDepth, screenUV).r;
    float invZ = (1.0 - sceneDepth) * u_waterInvNearFar.x +
                 sceneDepth * u_waterInvNearFar.y;
    float sceneZ = 1.0 / max(invZ, 1e-6);
    if (v_viewZ > sceneZ + 0.04) {
        discard;
    }

    vec2 flow = normalize(u_waterFlow.xy + vec2(0.0001, 0.0));
    vec2 side = vec2(-flow.y, flow.x);
    float time = u_waterParams0.x * u_waterParams0.w;
    float scale = u_waterParams0.z;
    float waveHeight = max(u_waterParams0.y, 0.001);

    // High-frequency normal detail on top of the vertex wave normal.
    float f1 = 5.3 * scale;
    float f2 = 7.9 * scale;
    float f3 = 11.0 * scale;
    float p1 = dot(v_worldPos.xz, flow) * f1 + time * 1.3;
    float p2 = dot(v_worldPos.xz, side) * f2 + time * 2.1;
    float p3 = dot(v_worldPos.xz, normalize(flow + side)) * f3 + time * 3.0;
    float detail = u_waterParams1.y * (0.035 + 0.045 * u_waterParams1.x);
    float dhdx = detail * (cos(p1) * flow.x * f1 +
                           cos(p2) * side.x * f2 +
                           cos(p3) * (flow.x + side.x) * f3 * 0.7071);
    float dhdz = detail * (cos(p1) * flow.y * f1 +
                           cos(p2) * side.y * f2 +
                           cos(p3) * (flow.y + side.y) * f3 * 0.7071);

    vec3 N = normalize(v_normal + vec3(-dhdx, 0.0, -dhdz));
    vec3 V = normalize(u_cameraPosition.xyz - v_worldPos);
    if (dot(N, V) < 0.0) N = -N;

    // Refraction. Keep the sample valid when the distorted ray lands on an
    // opaque surface in front of the water.
    vec2 distortion = N.xz * (u_waterParams1.z * 0.045);
    vec2 refrUV = clamp(screenUV + distortion, vec2(0.002), vec2(0.998));
    float refrDepth = texture2D(s_waterDepth, refrUV).r;
    float refrInvZ = (1.0 - refrDepth) * u_waterInvNearFar.x +
                     refrDepth * u_waterInvNearFar.y;
    float refrZ = 1.0 / max(refrInvZ, 1e-6);
    if (refrZ < v_viewZ) {
        refrUV = screenUV;
        refrDepth = sceneDepth;
        refrZ = sceneZ;
    }
    vec3 refracted = texture2D(s_waterSceneColor, refrUV).rgb;

    float thickness = max(sceneZ - v_viewZ, 0.0);
    float depthMix = 1.0 - exp(-thickness / max(u_waterParams2.x, 0.05));
    vec3 body = mix(u_waterShallow.rgb, u_waterDeep.rgb, depthMix);
    float absorption = exp(-thickness * u_waterParams2.y);

    float tintAmount = clamp(u_waterParams3.x * (0.55 + 0.45 * depthMix),
                             0.0, 0.92);
    vec3 color = mix(refracted, body * (0.65 + 0.35 * absorption),
                     tintAmount);

    // Fresnel reflection from the procedural environment cubemap.
    vec3 reflectionDir = reflect(-V, N);
    // Small waves should reflect the sky, not the dark procedural ground
    // below the horizon. This keeps the wave detail blue instead of producing
    // black blotches on a sunny surface.
    reflectionDir.y = max(reflectionDir.y, 0.03);
    reflectionDir = normalize(reflectionDir);
    vec3 reflection = textureCube(s_envCube, reflectionDir).rgb;

    // Planar reflection replaces the environment where the mirrored scene
    // actually drew geometry. Pixels with zero alpha contain no reflected
    // object and keep the cubemap fallback (sky/ground).
    if (u_waterPlanar.x > 0.5) {
        vec2 planarUV = clamp(screenUV + distortion * 1.25,
                              vec2(0.002), vec2(0.998));
        vec4 planarSample = texture2D(s_waterPlanar, planarUV);
        float planarMask = smoothstep(0.0, 0.55, planarSample.a);
        reflection = mix(reflection, planarSample.rgb,
                         planarMask * clamp(u_waterPlanar.z, 0.0, 1.0));
    }

    float fresnel = 0.02 + 0.98 *
                    pow(1.0 - max(dot(N, V), 0.0),
                        max(u_waterParams3.z, 1.0));
    float reflectionAmount = clamp((0.07 + 0.93 * fresnel) *
                                  u_waterParams1.w, 0.0, 0.92);
    color = mix(color, reflection, reflectionAmount);

    // Sun glints and small sparkle highlights on wave crests.
    vec3 L = normalize(-u_lightDirection.xyz);
    vec3 H = normalize(V + L);
    float NdotH = max(dot(N, H), 0.0);
    float sunStrength = max(u_lightColorIntensity.a, 0.0);
    float glint = pow(NdotH, 700.0) * (1.0 + u_waterParams3.w * 2.0);
    float sparkleNoise = valueNoise(v_worldPos.xz * 7.0 + flow * time * 0.35);
    float sparkle = pow(NdotH, 120.0) * u_waterParams3.w *
                    smoothstep(0.55, 1.0, sparkleNoise);
    color += u_lightColorIntensity.rgb * sunStrength *
             (glint * 1.6 + sparkle * 0.35);

    // Shoreline foam plus foam on the highest wave crests.
    float shore = 1.0 - smoothstep(0.0, max(u_waterParams2.z, 0.01),
                                   thickness);
    float crest = smoothstep(waveHeight * 0.65, waveHeight * 1.0, v_wave);
    float foamNoise = valueNoise(v_worldPos.xz * 3.5 + flow * time * 0.22);
    float foam = clamp((shore * 0.85 + crest * 0.35) *
                       u_waterParams2.w *
                       (0.40 + 0.60 * foamNoise), 0.0, 0.95);
    color = mix(color, u_waterFoam.rgb, foam * 0.88);

    float alpha = mix(0.45, 0.94, depthMix) * u_waterParams3.y;
    alpha = clamp(alpha + fresnel * 0.18 + foam * 0.35, 0.04, 1.0);
    gl_FragColor = vec4(color, alpha);
}

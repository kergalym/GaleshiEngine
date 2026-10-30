$input v_texcoord0
#include <bgfx_shader.sh>

// Reduced-resolution SSAO pass.
// Ported from tobspr/RenderPipeline: rpplugins/ao/shader/ao_sample.frag.glsl
// and rpplugins/ao/shader/ssao.kernel.glsl.
//
// The G-buffer stores world position and world normal, so the view-space
// position is reconstructed with the camera view matrix. This avoids any
// assumptions about the depth-buffer convention of the backend.

SAMPLER2D(s_worldPos,    0);
SAMPLER2D(s_normalRough, 1);

uniform mat4 u_aoView;
uniform mat4 u_aoProj;
uniform vec4 u_aoParams;   // x=sample radius in pixels, y=bias, z=max distance, w=strength
uniform vec4 u_aoViewport; // xy=full resolution, zw=1/full resolution
uniform vec4 u_aoSource;   // xy=AO source size, zw=1/AO source size

vec3 halton3D(int i)
{
    if (i == 0)  return vec3( 0.00000, -0.33333, -0.60000);
    if (i == 1)  return vec3(-0.50000,  0.33333, -0.20000);
    if (i == 2)  return vec3( 0.50000, -0.77778,  0.20000);
    if (i == 3)  return vec3(-0.75000, -0.11111,  0.60000);
    if (i == 4)  return vec3( 0.25000,  0.55556, -0.92000);
    if (i == 5)  return vec3(-0.25000, -0.55556, -0.52000);
    if (i == 6)  return vec3( 0.75000,  0.11111, -0.12000);
    if (i == 7)  return vec3(-0.87500,  0.77778,  0.28000);
    if (i == 8)  return vec3( 0.12500, -0.92593,  0.68000);
    if (i == 9)  return vec3(-0.37500, -0.25926, -0.84000);
    if (i == 10) return vec3( 0.62500,  0.40741, -0.44000);
    if (i == 11) return vec3(-0.62500, -0.70370, -0.04000);
    if (i == 12) return vec3( 0.37500, -0.03704,  0.36000);
    if (i == 13) return vec3(-0.12500,  0.62963,  0.76000);
    if (i == 14) return vec3( 0.87500, -0.48148, -0.76000);
    return vec3(-0.93750, 0.18519, -0.36000);
}

vec3 hash33(vec3 p)
{
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.xxy + p.yxx) * p.zyx) * 2.0 - 1.0;
}

void main()
{
    ivec2 coord = ivec2(gl_FragCoord.xy);

    // Map the reduced-resolution AO pixel to its centre in full-screen UV.
    vec2 uv = (vec2(coord) + 0.5) * u_aoSource.zw;

    vec4 wp = texture2D(s_worldPos, uv);
    if (wp.w < 0.5) {
        gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    vec3 viewPos = mul(u_aoView, vec4(wp.xyz, 1.0)).xyz;
    float viewDist = max(-viewPos.z, 0.001);
    vec3 viewN = normalize(mul(mat3(u_aoView), texture2D(s_normalRough, uv).xyz * 2.0 - 1.0));

    float kernelScale = min(5.0, 10.0 / viewDist);
    float sampleOffset = u_aoParams.x * u_aoViewport.z * 30.0 / (0.8 * kernelScale);
    float bias = u_aoParams.y * 0.5 / kernelScale;
    float maxRange = u_aoParams.z;

    vec3 noise = hash33(vec3(gl_FragCoord.xy, 0.0));

    float accum = 0.0;
    float rangeAccum = 0.0;

    for (int i = 0; i < 16; ++i) {
        vec3 offset = halton3D(i);
        offset = mix(offset, noise, 0.25);
        offset *= 0.6;

        // Keep the sample in the hemisphere around the view-space normal.
        offset = dot(offset, viewN) < 0.0 ? -offset : offset;

        vec3 offsetPos = viewPos + offset * sampleOffset;

        vec4 clip = mul(u_aoProj, vec4(offsetPos, 1.0));
        if (clip.w <= 0.0001) continue;

        vec3 ndc = clip.xyz / clip.w;
        vec2 sampleUV = vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 ||
            sampleUV.y < 0.0 || sampleUV.y > 1.0) continue;

        vec4 sampleWp = texture2D(s_worldPos, sampleUV);
        if (sampleWp.w < 0.5) continue;

        vec3 sampleViewPos = mul(u_aoView, vec4(sampleWp.xyz, 1.0)).xyz;
        float linzA = -offsetPos.z;
        float linzB = -sampleViewPos.z;

        float modifier = step(abs(linzA - linzB), maxRange * 0.2);
        rangeAccum += modifier * 0.5 + 0.5;
        modifier *= step(linzB + bias, linzA);
        accum += modifier;
    }

    accum /= max(0.1, rangeAccum);
    accum *= 0.4;

    float ao = 1.0 - accum;
    ao = pow(saturate(ao), u_aoParams.w);

    gl_FragColor = vec4(ao, ao, ao, 1.0);
}

$input v_texcoord0
#include <bgfx_shader.sh>

// Bilateral upsample of the half-resolution AO buffer.

SAMPLER2D(s_aoSource,   0);
SAMPLER2D(s_worldPos,   1);
SAMPLER2D(s_normalRough, 2);

uniform mat4 u_aoView;
uniform vec4 u_aoViewport;   // xy=full resolution, zw=1/full resolution
uniform vec4 u_aoSource;     // xy=AO source size, zw=1/AO source size
uniform vec4 u_aoBlurParams; // x=max depth difference, y=normal power

vec3 normalAt(vec2 uv)
{
    return normalize(texture2D(s_normalRough, uv).xyz * 2.0 - 1.0);
}

float depthAt(vec3 worldPos)
{
    return -mul(u_aoView, vec4(worldPos, 1.0)).z;
}

void main()
{
    vec2 uv = v_texcoord0;

    vec4 midWp = texture2D(s_worldPos, uv);
    if (midWp.w < 0.5) {
        gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    float centerDepth = depthAt(midWp.xyz);
    vec3 centerNormal = normalAt(uv);

    ivec2 sourceSize = ivec2(max(vec2(1.0), u_aoSource.xy));
    vec2 sourcePos = uv * u_aoSource.xy - 0.5;
    ivec2 start = ivec2(floor(sourcePos));
    vec2 frac = fract(sourcePos);

    float accum = 0.0;
    float weights = 0.0;

    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            ivec2 sourceCoord = start + ivec2(x, y);
            if (sourceCoord.x < 0 || sourceCoord.y < 0 ||
                sourceCoord.x >= sourceSize.x || sourceCoord.y >= sourceSize.y) continue;

            vec2 sourceUV = (vec2(sourceCoord) + 0.5) * u_aoSource.zw;
            float sourceAO = texture2D(s_aoSource, sourceUV).r;

            vec4 sourceWp = texture2D(s_worldPos, sourceUV);
            if (sourceWp.w < 0.5) continue;

            float sampleDepth = depthAt(sourceWp.xyz);
            vec3 sampleNormal = normalAt(sourceUV);

            float depthWeight = 1.0 - saturate(
                abs(sampleDepth - centerDepth) / max(u_aoBlurParams.x, 0.001));
            float normalWeight = pow(
                max(dot(sampleNormal, centerNormal), 0.0), u_aoBlurParams.y);
            float spatialWeight = (x == 0 ? 1.0 - frac.x : frac.x) *
                                  (y == 0 ? 1.0 - frac.y : frac.y);
            float weight = depthWeight * normalWeight * spatialWeight;

            accum += sourceAO * weight;
            weights += weight;
        }
    }

    float ao;
    if (weights < 1e-5) {
        ao = texture2D(s_aoSource, uv).r;
    } else {
        ao = accum / weights;
    }

    gl_FragColor = vec4(ao, ao, ao, 1.0);
}

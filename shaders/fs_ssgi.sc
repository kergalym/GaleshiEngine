$input v_texcoord0
#include <bgfx_shader.sh>

// Screen-space diffuse global illumination. The pass runs at half resolution
// and marches a small cosine-weighted hemisphere from each visible surface.
// Hits borrow the lit HDR radiance from the current frame's colour buffer; the
// composite in fs_ssr.sc applies the receiver albedo and AO.

SAMPLER2D(s_texColor,    0);
SAMPLER2D(s_worldPos,    1);
SAMPLER2D(s_normalRough, 2);
SAMPLER2D(s_texDepth,    3);

uniform mat4 u_ssgiViewProj;
uniform mat4 u_ssgiView;
uniform vec4 u_ssgiParams; // x = radius, y = thickness, z = steps, w = rays

#define PI 3.14159265359

void main() {
    vec2 uv = v_texcoord0;
    vec4 originWp = texture2D(s_worldPos, uv);
    if (originWp.w < 0.5) {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    vec3 N = normalize(texture2D(s_normalRough, uv).rgb * 2.0 - 1.0);
    float radius = max(u_ssgiParams.x, 0.05);
    float thickness = max(u_ssgiParams.y, 0.001);
    int steps = int(clamp(u_ssgiParams.z, 2.0, 24.0));
    int rays = int(clamp(u_ssgiParams.w, 1.0, 8.0));

    // Keep the receiver from immediately hitting itself on the first step.
    vec3 origin = originWp.xyz + N * max(radius * 0.002, 0.008);

    vec3 helper = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(helper, N));
    vec3 bitangent = cross(N, tangent);

    float hash = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
    vec3 accumulated = vec3(0.0);

    for (int r = 0; r < 8; ++r) {
        if (r >= rays) break;

        // Cosine-weighted hemisphere sample. This already includes the NdotL
        // factor, so a simple average of the hit radiance is the irradiance.
        float cosTheta = sqrt((float(r) + 0.5) / float(rays));
        float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
        float phi = 2.0 * PI * fract(hash + float(r) * 0.61803398875);
        vec3 direction = normalize(
            tangent * (cos(phi) * sinTheta) +
            bitangent * (sin(phi) * sinTheta) +
            N * cosTheta);

        for (int s = 0; s < 24; ++s) {
            if (s >= steps) break;

            float t = (float(s) + 0.5) / float(steps);
            vec3 rayPos = origin + direction * (t * radius);
            vec4 clip = mul(u_ssgiViewProj, vec4(rayPos, 1.0));
            if (clip.w <= 0.0001) break;

            vec2 sampleUV = clip.xy / clip.w * 0.5 + 0.5;
            // G-buffer textures use a top-left origin, while NDC Y points up.
            sampleUV.y = 1.0 - sampleUV.y;
            if (sampleUV.x < 0.0 || sampleUV.x > 1.0 ||
                sampleUV.y < 0.0 || sampleUV.y > 1.0) {
                continue;
            }

            float sceneDepth = texture2D(s_texDepth, sampleUV).r;
            if (sceneDepth >= 0.9999) continue;

            vec4 sceneWp = texture2D(s_worldPos, sampleUV);
            if (sceneWp.w < 0.5) continue;

            float rayViewZ = mul(u_ssgiView, vec4(rayPos, 1.0)).z;
            float sceneViewZ = mul(u_ssgiView, vec4(sceneWp.xyz, 1.0)).z;
            // View-space Z is negative in front of the camera. A ray behind
            // the visible surface has crossed geometry and deposits its
            // radiance at this screen position.
            if (rayViewZ <= sceneViewZ - thickness) {
                vec3 hitRadiance = texture2D(s_texColor, sampleUV).rgb;
                hitRadiance = min(hitRadiance, vec3(16.0));
                float distanceTravelled = t * radius;
                float falloff = 1.0 / (1.0 + distanceTravelled * distanceTravelled * 0.15);
                accumulated += hitRadiance * falloff;
                break;
            }
        }
    }

    gl_FragColor = vec4(accumulated / float(rays), 1.0);
}

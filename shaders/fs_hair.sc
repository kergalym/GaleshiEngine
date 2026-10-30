$input v_worldPos, v_normal, v_tangent, v_uv, v_random, v_clip
#include <bgfx_shader.sh>

SAMPLER2D(s_hairDepth, 1);
SAMPLERCUBE(s_hairEnvCube, 2);

uniform vec4 u_hairParams0;    // x=specular, y=shininess, z=ambient, w=opacity
uniform vec4 u_hairParams1;    // x=random tint, y=root darkening, z=tip fade, w=unused
uniform vec4 u_hairInvNearFar; // x=1/near, y=1/far
uniform vec4 u_hairRootColor;
uniform vec4 u_hairTipColor;
uniform vec4 u_cameraPosition;
uniform vec4 u_cameraForward;
uniform vec4 u_lightDirection;      // direction from the surface toward the sun
uniform vec4 u_lightColorIntensity; // rgb + intensity

#define PI 3.14159265359

float strandSpecular(vec3 tangent, vec3 normal, vec3 H, float exponent)
{
    vec3 shifted = normalize(tangent + normal * 0.09);
    float th = dot(shifted, H);
    return pow(sqrt(max(0.0, 1.0 - th * th)), exponent);
}

void main()
{
    // The source is the copied opaque depth. It uses the same NDC
    // convention as the scene pass, so no Y flip is needed here.
    vec2 screenUV = v_clip.xy / max(v_clip.w, 0.0001) * 0.5 + 0.5;
    float sceneDepth = texture2D(s_hairDepth, screenUV).r;
    // Compare in clip-space depth, the same space as the copied depth buffer.
    // This avoids reconstructing view depth and is robust for ribbons that
    // lie almost exactly on the scalp.
    float hairDepth = v_clip.z / max(v_clip.w, 1e-5);
    if (hairDepth > sceneDepth + 0.0008) discard;

    vec3 V = normalize(u_cameraPosition.xyz - v_worldPos);

    float t = clamp(v_uv.y, 0.0, 1.0);
    vec3 base = mix(u_hairRootColor.rgb, u_hairTipColor.rgb, t);
    float tint = 1.0 + (v_random - 0.5) * 2.0 * u_hairParams1.x;
    base *= max(tint, 0.0);
    float rootDark = mix(1.0 - clamp(u_hairParams1.y, 0.0, 1.0), 1.0,
                         smoothstep(0.0, 0.30, t));
    base *= rootDark;

    vec3 N = normalize(v_normal);
    if (dot(N, V) < 0.0) N = -N;
    vec3 T = v_tangent - N * dot(N, v_tangent);
    if (length(T) < 1e-5) T = normalize(cross(N, vec3(0.0, 1.0, 0.0)));
    T = normalize(T);

    vec3 L = normalize(u_lightDirection.xyz);
    vec3 H = normalize(V + L);
    vec3 radiance = u_lightColorIntensity.rgb * max(u_lightColorIntensity.a, 0.0);

    // Kajiya-Kay diffuse: hair is bright where the strand tangent is
    // perpendicular to the light direction.
    float TL = clamp(dot(T, L), -1.0, 1.0);
    float sinTL = sqrt(max(0.0, 1.0 - TL * TL));
    float diffuse = 0.20 + 0.80 * sinTL;

    vec3 color = base * (diffuse * radiance * 0.72 +
                         vec3_splat(u_hairParams0.z));

    // Soft environment ambient/reflection.
    vec3 ambientDir = normalize(vec3(N.x, abs(N.y) + 0.08, N.z));
    vec3 ambientEnv = textureCube(s_hairEnvCube, ambientDir).rgb;
    color += base * ambientEnv * u_hairParams0.z * 0.20;

    // Two shifted specular lobes produce the characteristic hair highlight.
    float exponent = max(u_hairParams0.y, 1.0);
    float specA = strandSpecular(T, N, H, exponent);
    float specB = strandSpecular(T, N, H, exponent * 0.58);
    float rootFade = mix(0.25, 1.0, smoothstep(0.0, 0.45, t));
    color += radiance * u_hairParams0.x *
             (specA * 0.65 + specB * 0.25) * rootFade;

    // A faint reflection band gives coarse hair a slightly wet sheen without
    // replacing the anisotropic highlight.
    vec3 R = reflect(-V, N);
    color += textureCube(s_hairEnvCube, R).rgb * u_hairParams0.x * 0.045;

    float alpha = u_hairParams0.w *
                  mix(1.0, 1.0 - clamp(u_hairParams1.z, 0.0, 1.0),
                      smoothstep(0.72, 1.0, t));
    gl_FragColor = vec4(color, clamp(alpha, 0.0, 1.0));
}

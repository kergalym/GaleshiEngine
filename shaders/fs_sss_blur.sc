$input v_texcoord0
#include <bgfx_shader.sh>

// Screen-space subsurface scattering. Two separable passes over the lit colour:
// the horizontal pass blurs SSS pixels into the scratch target, the vertical
// pass blurs that result again and blends it in by the scatter colour. Pixels
// without SSS pass through unchanged.
SAMPLER2D(s_sssSrc,      0); // H pass: lit colour. V pass: horizontally blurred colour
SAMPLER2D(s_sssOrig,     1); // unblurred lit colour
SAMPLER2D(s_sssWorldPos, 2); // G-buffer RT2: world position
SAMPLER2D(s_sssFeature,  3); // G-buffer RT5: scatter colour * strength (rgb), radius / 5 (a)

uniform vec4 u_sssDir;    // xy = pass direction in pixels, zw = 1 / target size
uniform vec4 u_sssMisc;   // x = pixels per world unit at distance 1, y = 1 for the final pass
uniform vec4 u_sssCamPos; // xyz = camera position

const int   kTaps          = 8;
const float kMaxKernelPx   = 24.0;
// The stored radius is radius / 5 with the radius in centimetres.
const float kRadiusToMetres = 0.05;

void main() {
    vec4 orig = texture2D(s_sssOrig, v_texcoord0);
    vec4 feature = texture2D(s_sssFeature, v_texcoord0);
    vec3 scatter = feature.rgb;
    if (feature.a < 0.0005 || dot(scatter, scatter) < 1e-8) {
        gl_FragColor = orig;
        return;
    }

    float depth = length(texture2D(s_sssWorldPos, v_texcoord0).xyz - u_sssCamPos.xyz);
    float radiusWorld = feature.a * kRadiusToMetres;
    float radiusPx = min(radiusWorld * u_sssMisc.x / max(depth, 0.05), kMaxKernelPx);
    vec2 stepUv = u_sssDir.xy * u_sssDir.zw;

    // Per-channel spread: red scatters furthest, blue stays near the surface.
    vec3 spread = vec3(0.5, 0.33, 0.22);
    vec3 sum = vec3(0.0, 0.0, 0.0);
    vec3 weightSum = vec3(0.0, 0.0, 0.0);
    for (int i = -kTaps; i <= kTaps; ++i) {
        float t = float(i) / float(kTaps);
        vec2 uv = v_texcoord0 + stepUv * (t * radiusPx);
        // Only other SSS pixels contribute, and not across depth edges.
        float valid = texture2D(s_sssFeature, uv).a > 0.0005 ? 1.0 : 0.0;
        float sampleDepth = length(texture2D(s_sssWorldPos, uv).xyz - u_sssCamPos.xyz);
        float depthWeight = clamp(1.0 - abs(sampleDepth - depth) / max(radiusWorld, 0.0005), 0.0, 1.0);
        vec3 w = exp(-(t * t) / (2.0 * spread * spread)) * depthWeight * valid;
        sum += texture2D(s_sssSrc, uv).rgb * w;
        weightSum += w;
    }
    vec3 blurred = sum / max(weightSum, vec3(0.00001, 0.00001, 0.00001));

    if (u_sssMisc.y < 0.5) {
        gl_FragColor = vec4(blurred, orig.a);
    } else {
        gl_FragColor = vec4(mix(orig.rgb, blurred, clamp(scatter, 0.0, 1.0)), orig.a);
    }
}

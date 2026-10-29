$input v_texcoord0
#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);
SAMPLER2D(s_texDepth, 1);
uniform vec4 u_sunScreenPos; // xy = screen pos, z = intensity, w = aspect
uniform vec4 u_godraysParams; // x = decay, y = density, z = weight, w = exposure

void main() {
    vec2 uv = v_texcoord0;
    vec3 baseColor = texture2D(s_texColor, uv).rgb;

    vec2 sunPos = u_sunScreenPos.xy;
    // Density is the fraction of the path to the light source covered by the
    // samples, not a per-sample multiplier. The old code multiplied every
    // step by density, so values like 2.23 jumped off-screen after one tap.
    const int kSamples = 32;
    vec2 delta = (sunPos - uv) *
                 (clamp(u_godraysParams.y, 0.0, 1.0) / float(kSamples));
    vec2 coord = uv;
    float illuminationDecay = 1.0;
    vec3 lightAccum = vec3(0.0);

    for (int i = 0; i < kSamples; ++i) {
        coord += delta;
        if (coord.x < 0.0 || coord.x > 1.0 ||
            coord.y < 0.0 || coord.y > 1.0) {
            break;
        }

        float sampleDepth = texture2D(s_texDepth, coord).r;
        if (sampleDepth >= 0.999) {
            vec3 sky = texture2D(s_texColor, coord).rgb;
            lightAccum += sky * illuminationDecay;
        }
        illuminationDecay *= u_godraysParams.x;
    }

    // Composite the light shafts over the whole image. The old shader
    // returned on geometry pixels, so the effect only brightened the sky and
    // was invisible whenever the sun was off-screen or behind an object.
    vec3 rays = lightAccum * (1.0 / float(kSamples)) *
                u_sunScreenPos.z * u_godraysParams.z * u_godraysParams.w;
    // Keep the shafts readable without washing out the whole scene at the
    // inspector's default intensity/weight values.
    gl_FragColor = vec4(baseColor + rays * 0.25, 1.0);
}

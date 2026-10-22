$input v_texcoord0
#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);
SAMPLER2D(s_texDepth, 1);
uniform vec4 u_sunScreenPos; // xy = screen pos, z = intensity, w = aspect
uniform vec4 u_godraysParams; // x = decay, y = density, z = weight, w = exposure

void main() {
    vec2 uv = v_texcoord0;
    vec3 baseColor = texture2D(s_texColor, uv).rgb;
    float depth = texture2D(s_texDepth, uv).r;

    if (depth < 0.999) {
        gl_FragColor = vec4(baseColor, 1.0);
        return;
    }

    vec2 sunPos = u_sunScreenPos.xy;
    // Если солнце за экраном, не рисуем лучи
    if (sunPos.x < 0.0 || sunPos.x > 1.0 || sunPos.y < 0.0 || sunPos.y > 1.0) {
        gl_FragColor = vec4(baseColor, 1.0);
        return;
    }

    // Radial blur в сторону солнца
    vec2 delta = (sunPos - uv) * u_godraysParams.y;
    vec2 coord = uv;
    float illuminationDecay = 1.0;
    vec3 lightAccum = vec3(0.0);

    for (int i = 0; i < 32; ++i) {
        coord += delta;
        float sampleDepth = texture2D(s_texDepth, coord).r;
        if (sampleDepth >= 0.999) {
            vec3 sky = texture2D(s_texColor, coord).rgb;
            lightAccum += sky * illuminationDecay * u_godraysParams.z;
        }
        illuminationDecay *= u_godraysParams.x;
    }

    gl_FragColor = vec4(baseColor + lightAccum * u_sunScreenPos.z * u_godraysParams.w, 1.0);
}

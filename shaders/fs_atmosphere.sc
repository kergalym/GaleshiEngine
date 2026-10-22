$input v_texcoord0
#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);
SAMPLER2D(s_texDepth, 1);
uniform mat4 u_atmoInvVP;
uniform vec4 u_atmoSunDir; // xyz = direction, w = intensity
uniform vec4 u_atmoParams; // x = planetRadius, y = atmosphereRadius, z = rayleighScaleHeight, w = mieScaleHeight

#define PI 3.14159265359
const vec3 rayleighCoeff = vec3(5.5e-6, 13.0e-6, 22.4e-6);
const float mieCoeff = 21e-6;

bool raySphereIntersect(vec3 ro, vec3 rd, float r, out float t0, out float t1) {
    float b = dot(ro, rd);
    float c = dot(ro, ro) - r*r;
    float d = b*b - c;
    if (d < 0.0) return false;
    d = sqrt(d);
    t0 = -b - d;
    t1 = -b + d;
    return true;
}

void main() {
    float depth = texture2D(s_texDepth, v_texcoord0).r;
    // Если это не небо (есть геометрия), оставляем как есть
    if (depth < 0.9999) {
        gl_FragColor = texture2D(s_texColor, v_texcoord0);
        return;
    }

    vec2 ndc = vec2(v_texcoord0.x * 2.0 - 1.0, 1.0 - v_texcoord0.y * 2.0);
    vec4 clip = vec4(ndc, 1.0, 1.0);
    vec4 world = mul(u_atmoInvVP, clip);
    vec3 rd = normalize(world.xyz / world.w);

    // Позиция камеры (условно 1000 м над поверхностью планеты)
    vec3 ro = vec3(0.0, u_atmoParams.x + 1000.0, 0.0);

    float tAtmo, tAtmoMax;
    if (!raySphereIntersect(ro, rd, u_atmoParams.y, tAtmo, tAtmoMax)) {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0); // Космос
        return;
    }
    tAtmo = max(tAtmo, 0.0);

    float tGround, tGroundMax;
    bool hitGround = raySphereIntersect(ro, rd, u_atmoParams.x, tGround, tGroundMax) && tGround > 0.0;
    if (hitGround) tAtmoMax = min(tAtmoMax, tGround);

    float segmentLength = (tAtmoMax - tAtmo) / 16.0;

    vec3 totalRayleigh = vec3(0.0);
    vec3 totalMie = vec3(0.0);
    float opticalDepthR = 0.0;
    float opticalDepthM = 0.0;

    vec3 sunDir = normalize(u_atmoSunDir.xyz);

    for (int i = 0; i < 16; ++i) {
        vec3 p = ro + rd * (tAtmo + segmentLength * (float(i) + 0.5));
        float height = length(p) - u_atmoParams.x;

        float hr = exp(-height / u_atmoParams.z) * segmentLength;
        float hm = exp(-height / u_atmoParams.w) * segmentLength;

        opticalDepthR += hr;
        opticalDepthM += hm;

        float tLight, tLightMax;
        raySphereIntersect(p, sunDir, u_atmoParams.y, tLight, tLightMax);

        float segmentLengthLight = tLightMax / 8.0;
        float opticalDepthLightR = 0.0;
        float opticalDepthLightM = 0.0;
        bool blocked = false;

        for (int j = 0; j < 8; ++j) {
            vec3 pLight = p + sunDir * (segmentLengthLight * (float(j) + 0.5));
            float heightLight = length(pLight) - u_atmoParams.x;
            if (heightLight < 0.0) { blocked = true; break; }
            opticalDepthLightR += exp(-heightLight / u_atmoParams.z) * segmentLengthLight;
            opticalDepthLightM += exp(-heightLight / u_atmoParams.w) * segmentLengthLight;
        }

        if (!blocked) {
            vec3 tau = rayleighCoeff * (opticalDepthR + opticalDepthLightR) + mieCoeff * 1.1 * (opticalDepthM + opticalDepthLightM);
            vec3 attenuation = exp(-tau);
            totalRayleigh += hr * attenuation;
            totalMie += hm * attenuation;
        }
    }

    float mu = dot(rd, sunDir);
    float phaseR = 3.0 / (16.0 * PI) * (1.0 + mu * mu);
    float g = 0.76;
    float phaseM = 3.0 / (8.0 * PI) * ((1.0 - g * g) * (1.0 + mu * mu)) / ((2.0 + g * g) * pow(1.0 + g * g - 2.0 * g * mu, 1.5));

    vec3 color = u_atmoSunDir.w * (totalRayleigh * rayleighCoeff * phaseR + totalMie * mieCoeff * phaseM);

    float sun = max(dot(rd, sunDir), 0.0);
    if (sun > 0.9999) {
        color += vec3(1.0, 0.9, 0.8) * 20.0;
    }

    // Тонмаппинг для неба
    color = 1.0 - exp(-color * 2.0);

    gl_FragColor = vec4(color, 1.0);
}

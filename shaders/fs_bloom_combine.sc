$input v_texcoord0
#include <bgfx_shader.sh>
SAMPLER2D(s_texColor, 0);
SAMPLER2D(s_bloom, 1);
uniform vec4 u_bloomIntensity;
// xy = sun position in UV space, z = visibility (0 behind the camera or off screen).
uniform vec4 u_lensFlareSun;
// x = intensity, y = HDR threshold, z = aspect ratio (width / height).
uniform vec4 u_lensFlareParams;

// Soft disc in aspect-corrected screen space; radius is in screen heights.
float flareDisc(vec2 uv, vec2 centre, float radius, float aspect) {
    vec2 d = (uv - centre) * vec2(aspect, 1.0);
    return 1.0 - smoothstep(radius * 0.6, radius, length(d));
}

void main() {
    vec3 base = texture2D(s_texColor, v_texcoord0).rgb;
    vec3 bloom = texture2D(s_bloom, v_texcoord0).rgb;
    vec3 color = base + bloom * u_bloomIntensity.x;

    float intensity = u_lensFlareParams.x;
    float visibility = u_lensFlareSun.z;
    if (intensity > 0.0 && visibility > 0.0) {
        vec2 sunUv = u_lensFlareSun.xy;
        float aspect = u_lensFlareParams.z;
        // Sun brightness above the threshold. An occluder in front of the sun
        // darkens this sample, so the flare fades behind it without a depth test.
        vec3 source = max(texture2D(s_texColor, sunUv).rgb - vec3(1.0, 1.0, 1.0) * u_lensFlareParams.y,
                          vec3(0.0, 0.0, 0.0));

        vec2 centre = vec2(0.5, 0.5);
        vec2 axis = centre - sunUv;
        vec3 flare = vec3(0.0, 0.0, 0.0);
        flare += source * vec3(1.0, 0.7, 0.4) * flareDisc(v_texcoord0, centre + axis * 0.45, 0.035, aspect);
        flare += source * vec3(0.5, 0.8, 1.0) * flareDisc(v_texcoord0, centre + axis * 0.9, 0.06, aspect);
        flare += source * vec3(0.8, 1.0, 0.6) * flareDisc(v_texcoord0, centre + axis * 1.3, 0.025, aspect);
        flare += source * vec3(1.0, 0.5, 0.7) * flareDisc(v_texcoord0, centre + axis * 1.7, 0.09, aspect);
        flare += source * vec3(0.6, 0.6, 1.0) * flareDisc(v_texcoord0, centre - axis * 0.35, 0.045, aspect) * 0.6;

        // Halo: a faint ring around the centre, brightest on the sun's side.
        vec2 d = (v_texcoord0 - centre) * vec2(aspect, 1.0);
        vec2 sd = (sunUv - centre) * vec2(aspect, 1.0);
        if (length(d) > 0.0001 && length(sd) > 0.0001) {
            float facing = dot(normalize(d), normalize(sd)) * 0.5 + 0.5;
            float ring = 1.0 - smoothstep(0.0, 0.035, abs(length(d) - 0.42));
            flare += source * vec3(0.7, 0.85, 1.0) * ring * facing * 0.6;
        }
        color += flare * intensity * visibility * 0.1;
    }
    gl_FragColor = vec4(color, 1.0);
}

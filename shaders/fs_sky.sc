$input v_texcoord0
#include <bgfx_shader.sh>
uniform mat4 u_skyCustomInvVP; // Renamed to avoid BGFX reserved names
uniform vec4 u_skySunDir;
void main() {
    vec2 ndc = vec2(v_texcoord0.x * 2.0 - 1.0, 1.0 - v_texcoord0.y * 2.0);
    vec4 clip = vec4(ndc, 1.0, 1.0);
    vec4 world = mul(u_skyCustomInvVP, clip);
    vec3 dir = normalize(world.xyz / max(world.w, 0.000001));
    // Procedural sky: horizon color
    vec3 skyColor = mix(vec3(0.4, 0.6, 1.0), vec3(0.05, 0.1, 0.3), clamp(dir.y, 0.0, 1.0));
    // Sun
    float sun = max(dot(dir, normalize(u_skySunDir.xyz)), 0.0);
    skyColor += vec3(1.0, 0.9, 0.8) * pow(sun, 128.0) * 10.0;
    gl_FragData[0] = vec4(skyColor, 1.0);
}

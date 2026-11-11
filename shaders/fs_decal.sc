$input v_clip
#include <bgfx_shader.sh>

// Albedo is the only target written. Normal and world position are read from
// the G-buffer at the same pixel.
SAMPLER2D(s_decalAlbedo,   0);
SAMPLER2D(s_decalNormal,   1);
SAMPLER2D(s_decalWorldPos, 2);

uniform mat4 u_decalInvModel; // world -> decal box space
uniform vec4 u_decalAxis;     // decal projection axis (box +Y) in world space
uniform vec4 u_decalParams;   // x = opacity, y = min facing cosine, z = Y flip

void main() {
    // Same NDC-to-texture mapping as the shadow lookups, so the G-buffer texel
    // is the pixel this fragment covers.
    vec2 uv = v_clip.xy / v_clip.w * 0.5 + 0.5;
    if (u_decalParams.z > 0.5) uv.y = 1.0 - uv.y;

    vec4 surface = texture2D(s_decalWorldPos, uv);
    if (surface.w < 0.5) discard; // sky
    vec3 local = mul(u_decalInvModel, vec4(surface.xyz, 1.0)).xyz;
    if (abs(local.x) > 0.5 || abs(local.y) > 0.5 || abs(local.z) > 0.5) discard;

    vec3 N = normalize(texture2D(s_decalNormal, uv).xyz * 2.0 - 1.0);
    if (dot(N, u_decalAxis.xyz) < u_decalParams.y) discard;

    vec4 decal = texture2D(s_decalAlbedo, local.xz + 0.5);
    gl_FragColor = vec4(decal.rgb, decal.a * u_decalParams.x);
}

$input v_texcoord0
#include <bgfx_shader.sh>

// Procedural sky used to build the environment cubemap.
// Each face is rendered into a separate 2D atlas slice, then copied into
// the cubemap with bgfx::blit.

uniform vec4 u_iblParams;       // reserved
uniform vec4 u_faceParams;      // x = face size, y = face index
uniform vec4 u_lightDirection;  // direction the sunlight travels

vec3 faceDirection(vec2 uv, float face)
{
    vec2 st = uv * 2.0 - 1.0;
    if (face < 0.5)      return normalize(vec3( 1.0, -st.y, -st.x)); // +X
    else if (face < 1.5) return normalize(vec3(-1.0, -st.y,  st.x)); // -X
    else if (face < 2.5) return normalize(vec3( st.x,  1.0,  st.y)); // +Y
    else if (face < 3.5) return normalize(vec3( st.x, -1.0, -st.y)); // -Y
    else if (face < 4.5) return normalize(vec3( st.x, -st.y,  1.0)); // +Z
    else                 return normalize(vec3(-st.x, -st.y, -1.0)); // -Z
}

vec3 getSkyColor(vec3 dir)
{
    float up = clamp(dir.y, 0.0, 1.0);
    vec3 zenith  = vec3(0.05, 0.12, 0.32);
    vec3 horizon = vec3(0.55, 0.70, 0.95);
    vec3 skyColor = mix(horizon, zenith, pow(up, 0.45));

    // Below the horizon: dark ground bounce instead of repeating the sky.
    vec3 ground = vec3(0.035, 0.032, 0.028);
    skyColor = mix(ground, skyColor, smoothstep(-0.06, 0.02, dir.y));

    // Sun disc + broad glow.
    vec3 sunDir = normalize(-u_lightDirection.xyz);
    float sun = max(dot(dir, sunDir), 0.0);
    skyColor += vec3(1.0, 0.88, 0.72) * pow(sun, 512.0) * 32.0;
    skyColor += vec3(1.0, 0.72, 0.45) * pow(sun, 12.0) * 0.35;

    return skyColor;
}

void main()
{
    vec3 dir = faceDirection(v_texcoord0, u_faceParams.y);
    gl_FragColor = vec4(getSkyColor(dir), 1.0);
}

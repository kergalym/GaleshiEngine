$input a_position, a_normal, a_tangent, a_texcoord0
$output v_worldPos, v_normal, v_uv, v_clip, v_viewZ, v_wave
#include <bgfx_shader.sh>

uniform vec4 u_waterParams0; // x = time, y = height, z = scale, w = speed
uniform vec4 u_waterFlow;    // xy = flow direction
uniform vec4 u_cameraPosition;
uniform vec4 u_cameraForward;

void main()
{
    vec3 world = mul(u_model[0], vec4(a_position, 1.0)).xyz;

    vec2 flow = normalize(u_waterFlow.xy + vec2(0.0001, 0.0));
    vec2 side = vec2(-flow.y, flow.x);
    float t = u_waterParams0.x * u_waterParams0.w;
    float amp = u_waterParams0.y;
    float scale = u_waterParams0.z;

    float f1 = 1.8 * scale;
    float f2 = 3.1 * scale;
    float f3 = 4.7 * scale;
    float p1 = dot(world.xz, flow) * f1 + t * 1.0;
    float p2 = dot(world.xz, side) * f2 + t * 1.7;
    float p3 = dot(world.xz, normalize(flow + side)) * f3 + t * 2.3;

    float h = amp * (0.55 * sin(p1) + 0.30 * sin(p2) + 0.15 * sin(p3));
    float dhdx = amp * (0.55 * cos(p1) * flow.x * f1 +
                        0.30 * cos(p2) * side.x * f2 +
                        0.15 * cos(p3) * (flow.x + side.x) * f3 * 0.7071);
    float dhdz = amp * (0.55 * cos(p1) * flow.y * f1 +
                        0.30 * cos(p2) * side.y * f2 +
                        0.15 * cos(p3) * (flow.y + side.y) * f3 * 0.7071);

    world.y += h;
    v_worldPos = world;
    v_normal = normalize(vec3(-dhdx, 1.0, -dhdz));
    v_uv = a_texcoord0;
    v_wave = h;
    v_viewZ = dot(world - u_cameraPosition.xyz,
                  normalize(u_cameraForward.xyz));

    v_clip = mul(u_viewProj, vec4(world, 1.0));
    gl_Position = v_clip;
}

#include <bgfx_compute.sh>

#define TILE_SIZE 16
#define MAX_LIGHTS_PER_TILE 64
#define MAX_LIGHTS 64

uniform vec4 u_cullParams; // x = tilesX, y = tilesY, z = screenWidth, w = screenHeight
uniform mat4 u_cullViewProj;
uniform vec4 u_cullPointLightPosRadius[MAX_LIGHTS];
uniform vec4 u_cullPointLightCount;

// Layout: [0 .. totalTiles) = per-tile counts,
//         [totalTiles + tileIdx * MAX_LIGHTS_PER_TILE + n] = light indices.
BUFFER_WO(s_tileLights, uint, 0);

NUM_THREADS(1, 1, 1)
void main() {
    uvec2 tileId = gl_GlobalInvocationID.xy;
    if (tileId.x >= uint(u_cullParams.x) || tileId.y >= uint(u_cullParams.y)) return;

    uint tileIdx = tileId.y * uint(u_cullParams.x) + tileId.x;
    uint totalTiles = uint(u_cullParams.x) * uint(u_cullParams.y);
    uint count = 0;

    vec2 tileMinUV = vec2(float(tileId.x) * TILE_SIZE, float(tileId.y) * TILE_SIZE) / u_cullParams.zw;
    vec2 tileMaxUV = vec2(float(tileId.x + 1) * TILE_SIZE, float(tileId.y + 1) * TILE_SIZE) / u_cullParams.zw;

    for (int i = 0; i < int(u_cullPointLightCount.x); ++i) {
        vec3 lightPos = u_cullPointLightPosRadius[i].xyz;
        float radius = u_cullPointLightPosRadius[i].w;

        vec4 clipPos = mul(u_cullViewProj, vec4(lightPos, 1.0));
        if (clipPos.w <= 0.0) continue;
        vec2 screenPos = clipPos.xy / clipPos.w * 0.5 + 0.5;

        float screenRadius = radius / max(clipPos.w, 0.1);

        vec2 minUV = screenPos - screenRadius;
        vec2 maxUV = screenPos + screenRadius;

        if (maxUV.x >= tileMinUV.x && minUV.x <= tileMaxUV.x &&
            maxUV.y >= tileMinUV.y && minUV.y <= tileMaxUV.y) {
            if (count < MAX_LIGHTS_PER_TILE) {
                uint offset = totalTiles + tileIdx * MAX_LIGHTS_PER_TILE + count;
                s_tileLights[offset] = uint(i);
                count++;
            }
        }
    }
    s_tileLights[tileIdx] = count;
}

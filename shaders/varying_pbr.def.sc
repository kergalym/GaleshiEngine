vec3 a_position  : POSITION;
vec3 a_normal    : NORMAL;
vec3 a_tangent   : TANGENT;
vec2 a_texcoord0 : TEXCOORD0;
vec4 a_color0    : COLOR0;
vec3 v_worldPos  : TEXCOORD1 = vec3(0.0, 0.0, 0.0);
vec3 v_normal    : TEXCOORD2 = vec3(0.0, 0.0, 1.0);
vec3 v_tangent   : TEXCOORD3 = vec3(1.0, 0.0, 0.0);
vec2 v_uv        : TEXCOORD4 = vec2(0.0, 0.0);

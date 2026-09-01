#ifndef SCENE_GLSL
#define SCENE_GLSL

#define MAX_LIGHTS 8

struct Light {
    // w is 1 for a MIXED light -- one that is in the lightmap AND in this array,
    // because it lights dynamic objects the atlas cannot reach. A lightmapped
    // surface must skip it or it is lit twice; see LIGHT_IS_ALSO_BAKED below.
    vec4 position;          // xyz = world position, w = also baked
    vec4 direction;         // xyz = normalized direction, w unused
    vec4 radiance;          // rgb, already through shared/lighting.hpp's radiance_of
    vec4 spot_params;       // x = cos(inner), y = cos(outer), z = range, w = type
};

// lighting_def.md decision B: ONE array, and the surface decides. The -DLIGHTMAP
// variant split already encodes "is this surface in the atlas" at compile time,
// so the branch costs nothing at runtime and a second array costs a second
// upload plus a second thing to keep in agreement.
#define LIGHT_IS_ALSO_BAKED(light) ((light).position.w > 0.5)

layout(set = 3, binding = 1) uniform SceneUniform {
    mat4  view_projection;
    vec4  camera_position;  // xyz, w unused
    vec4  ambient;          // rgb = the constant floor, a unused
    int   light_count;
    int   debug_flags;
    int   _pad0;
    int   _pad1;
    Light lights[MAX_LIGHTS];
} scene;

#endif // SCENE_GLSL

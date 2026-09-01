#ifndef SHADER_TOOL_COMMON_GLSL
#define SHADER_TOOL_COMMON_GLSL

#define MAX_LIGHTS 8

// RADIANCE, not colour and intensity, for the reason scene.glsl carries the same
// field: shared/lighting.hpp's radiance_of is the ONE conversion from a Light
// component, the reference distance lives inside it and is not separately
// spellable, and a preview that multiplied colour by intensity itself is exactly
// the second lighting model lighting_def.md ss11 is about. It is what made this
// tool's 1500 and 30000 necessary.
struct Light {
    vec4 position;          // xyz = world position, w = unused
    vec4 direction;         // xyz = normalized direction, w = unused
    vec4 radiance;          // rgb, already through shared/lighting.hpp's radiance_of
    vec4 spot_params;       // x = cos(inner), y = cos(outer), z = range, w = type (0=point, 1=spot, 2=directional)
};

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 view;
    mat4 projection;
    mat4 view_projection;
    vec4 camera_position;     // xyz, w unused
    vec4 time;                // x = seconds, y = sin(t), z = cos(t), w = dt
    int  light_count;
    int  debug_flags, _pad1, _pad2;
    Light lights[MAX_LIGHTS];
    vec4  param_color[4];
    vec4  param_vec4[8];
    float param_float[16];
} scene;

// The falloff, the cone and the type tag are light_arrival.glsl's, which is the
// game's and the bake's too -- this helper held a FOURTH copy of them, and its
// falloff was a linear `1 - d/range` squared rather than the windowed inverse
// square everything else uses. A preview whose lights fall off differently from
// the game's is a tool that lies about the shader you are authoring in it.
//
// light_arrival rather than pbr_lighting: this header is included by the preview
// VERTEX shader too, and pbr_lighting's TBN needs dFdx.
#include "light_arrival.glsl"

#ifndef PI
#define PI 3.14159265359
#endif

// Lambert diffuse for a preview shader that does not want the full BRDF. The
// 1/PI is the same one shade_direct and lightmap_diffuse apply, for the same
// reason (lighting_def.md ss9).
vec3 compute_lighting(vec3 world_position, vec3 world_normal) {
    vec3 result = vec3(0.0);

    for (int i = 0; i < scene.light_count && i < MAX_LIGHTS; i++) {
        Light light = scene.lights[i];
        vec4  arrival = light_arrival(int(light.spot_params.w), light.position.xyz,
                                      light.direction.xyz, light.spot_params.x,
                                      light.spot_params.y, light.spot_params.z,
                                      world_position);

        float n_dot_l = max(dot(world_normal, arrival.xyz), 0.0);
        result += (light.radiance.rgb / PI) * n_dot_l * arrival.w;
    }

    return result;
}

#endif // SHADER_TOOL_COMMON_GLSL

#ifndef LIGHT_ARRIVAL_GLSL
#define LIGHT_ARRIVAL_GLSL

// One light, one surface point: which way it is and how much of it arrives.
// The layer between light_falloff.glsl (scalar, and compiled as C++ too) and
// pbr_lighting.glsl (the BRDF, samplers and screen-space derivatives).
//
// It is its own file because of who can compile what. Derivatives are
// FRAGMENT-ONLY, and shader_tool_common.glsl is included by the preview's vertex
// shader as well as its fragment one -- so the tool cannot reach the arrival
// maths through pbr_lighting.glsl without dragging dFdx in with it. Three layers,
// each one the largest thing all of its readers can compile.

#include "light_falloff.glsl"

#define LIGHT_TYPE_POINT       0
#define LIGHT_TYPE_SPOT        1
#define LIGHT_TYPE_DIRECTIONAL 2

// xyz = L, the unit vector FROM the surface toward the light; w = attenuation.
vec4 light_arrival(int light_type, vec3 light_position, vec3 light_forward,
                   float cos_inner, float cos_outer, float range, vec3 world_position)
{
    if (light_type == LIGHT_TYPE_DIRECTIONAL)
        return vec4(-normalize(light_forward), 1.0);

    vec3  to_light         = light_position - world_position;
    float squared_distance = dot(to_light, to_light);
    vec3  L                = to_light / max(sqrt(squared_distance), 0.001);
    float attenuation      = distance_attenuation(squared_distance, range);

    if (light_type == LIGHT_TYPE_SPOT)
        attenuation *= spot_cone_factor(dot(-L, normalize(light_forward)), cos_inner, cos_outer);

    return vec4(L, attenuation);
}

#endif // LIGHT_ARRIVAL_GLSL

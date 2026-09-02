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

// THE light struct, and it lives here rather than in scene.glsl because the
// shader tool's preview held a second copy of it -- identical by inspection and
// free to stop being so, which is exactly what happened to the falloff before it
// moved into this family. The game binds it out of scene.glsl's per-pass block
// and the tool out of its own UBO; the LAYOUT is one text, and the renderer's
// gpu_light_t is size-asserted against it.
struct Light {
    // xyz world position; w is which slot of the map's bake this light is, or -1
    // for a light the bake never saw. For the first baked_light_count entries it
    // is the index itself; in the tail it is what marks a Mixed light's second
    // copy, which a lightmapped surface must SKIP because it already shaded it
    // through its chart. The shader tool leaves it -1, having no bake.
    vec4 position;
    // xyz normalized direction; w = the emitter's RADIUS. A point or spot
    // measures it in world units. A directional light has no position to measure
    // from, so it carries tan(half its angular diameter) and is treated as a
    // sphere ONE UNIT away -- the same geometry with the distance divided out,
    // which is what lets everything below take one radius and no light-type
    // branch.
    vec4 direction;
    vec4 radiance;          // rgb, already through shared/lighting.hpp's radiance_of
    vec4 spot_params;       // x = cos(inner), y = cos(outer), z = range, w = type
};

// lighting_def.md decision B: ONE array, and the surface decides. The -DLIGHTMAP
// variant split already encodes "is this surface in the atlas" at compile time,
// so the branch costs nothing at runtime and a second array costs a second
// upload plus a second thing to keep in agreement.
#define LIGHT_BAKED_SLOT(light) (int((light).position.w))

// What one light does at one surface point. A struct rather than the vec4 of L
// and attenuation this returned before, because the DISTANCE is what an area
// light's specular needs and recomputing it at the call site is a second answer
// free to disagree with this one.
struct Light_Arrival {
    vec3  direction;        // L, unit, FROM the surface toward the light
    float attenuation;
    // To the emitter's centre. A directional light has none, so it reports 1.0 --
    // the unit distance its source radius is already expressed against.
    float distance;
};

Light_Arrival light_arrival(Light light, vec3 world_position)
{
    int   light_type = int(light.spot_params.w);
    float range      = light.spot_params.z;

    Light_Arrival arrival;

    if (light_type == LIGHT_TYPE_DIRECTIONAL)
    {
        arrival.direction   = -normalize(light.direction.xyz);
        arrival.attenuation = 1.0;
        arrival.distance    = 1.0;
        return arrival;
    }

    vec3  to_light         = light.position.xyz - world_position;
    float squared_distance = dot(to_light, to_light);

    arrival.distance    = max(sqrt(squared_distance), 0.001);
    arrival.direction   = to_light / arrival.distance;
    arrival.attenuation = distance_attenuation(squared_distance, range, light.direction.w);

    if (light_type == LIGHT_TYPE_SPOT)
        arrival.attenuation *= spot_cone_factor(dot(-arrival.direction, normalize(light.direction.xyz)),
                                                light.spot_params.x, light.spot_params.y);

    return arrival;
}

#endif // LIGHT_ARRIVAL_GLSL

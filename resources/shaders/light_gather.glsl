#ifndef LIGHT_GATHER_GLSL
#define LIGHT_GATHER_GLSL

// Which light reaches a fragment and through what shadow. What it then LOOKS
// like is shading.glsl's; nothing here knows a BRDF.

#include "scene.glsl"
#include "probes.glsl"
#include "direct_light.glsl"
#include "shading.glsl"

#ifdef LIGHTMAP
#include "lightmap.glsl"
#endif

Incoming_Light incoming_light(Light light, Light_Arrival arrival, vec3 visibility)
{
    Incoming_Light incoming;
    incoming.direction     = arrival.direction;
    incoming.radiance      = light.radiance.rgb;
    incoming.visibility    = visibility;
    incoming.attenuation   = arrival.attenuation;
    incoming.source_radius = light.direction.w;
    incoming.distance      = arrival.distance;
    return incoming;
}

vec3 gather_direct_light(int look, Surface surface, vec3 world_position, vec3 V)
{
    vec3 lit = vec3(0.0);

#ifdef LIGHTMAP
    // The four this face's chart kept, and nothing else in the level (lighting_def.md ss14 step 6).
    lightmap_coverage_t coverage = lightmap_coverage();
    for (int channel = 0; channel < LIGHTMAP_LIGHTS_PER_CHART; ++channel)
    {
        int slot = lightmap_chart_slot(channel);
        if (slot < 0 || lightmap_coverage_strength(coverage.slots[channel]) <= 0.0)
            continue;

        Light         light   = scene.lights[slot];
        Light_Arrival arrival = light_arrival(light, world_position);

        // Atlas visibility TIMES the shadow map (decision K): independent blockers, nothing counted twice.
        vec3 visibility = coverage.slots[channel] *
                          shadow_visibility(light, arrival, world_position, surface.normal);

        lit += shade_light(look, surface, V, incoming_light(light, arrival, visibility));
    }
#endif

    // The tail: the lights no bake saw, plus a second copy of every Mixed one.
    for (int index = scene.baked_light_count; index < scene.light_count; ++index)
    {
        Light light = scene.lights[index];

#ifdef LIGHTMAP
        // A lightmapped surface shaded this light through its chart above (the ss2 double-count).
        if (LIGHT_BAKED_SLOT(light) >= 0)
            continue;
#endif

        Light_Arrival arrival    = light_arrival(light, world_position);
        float         visibility = shadow_visibility(light, arrival, world_position, surface.normal);
#ifndef LIGHTMAP
        // The probes' static occlusion of a Mixed light, the atlas texel's job at a point in space.
        visibility *= probe_light_visibility(light, world_position);
#endif

        lit += shade_light(look, surface, V, incoming_light(light, arrival, vec3(visibility)));
    }

    return lit;
}

// The light with no direction left in it: the atlas on a charted face, the probes on everything else.
vec3 baked_irradiance(vec3 world_position, vec3 N)
{
#ifdef LIGHTMAP
    return lightmap_residual_diffuse() + lightmap_indirect_diffuse(N);
#else
    return probe_indirect_diffuse(world_position, N);
#endif
}

// The cel look shades a WHITE surface off the geometric normal, so it holds the
// light alone and can tell the lit side from the shadow side before the albedo goes on.
vec3 light_surface_cel(Surface surface, vec3 world_position, vec3 V)
{
    Surface white = surface;
    white.albedo  = vec3(1.0);
    white.normal  = surface.geometric_normal;

    vec3 direct  = gather_direct_light(LOOK_CEL, white, world_position, V);
    vec3 ambient = shade_ambient(LOOK_CEL, white, V, world_position,
                                 baked_irradiance(world_position, white.normal), scene.ambient.rgb);
    return compose_cel(surface, direct, ambient, world_position) + surface.emissive;
}

vec3 light_surface(int material_look, Surface surface, vec3 world_position, vec3 V)
{
    int look = frame_look(material_look);
    if (look == LOOK_CEL)
        return light_surface_cel(surface, world_position, V);

    return gather_direct_light(look, surface, world_position, V) +
           shade_ambient(look, surface, V, world_position,
                         baked_irradiance(world_position, surface.normal), scene.ambient.rgb) +
           surface.emissive;
}

#endif // LIGHT_GATHER_GLSL

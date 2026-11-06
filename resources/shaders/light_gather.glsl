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

// projector_mask.hpp's PROJECTOR_MASK_DARK_THRESHOLD: a picture cell this dark is solid, so the mark agrees with it.
const float SOLID_SHADOW_DARK_THRESHOLD = 0.25;

// Whether a solid light's shadow is CAST onto this point (shadow_volume_plan.md ss5): the surface faces the
// light, the light reaches it (a projector's frame holds it: outside the frame the picture reads black and
// is no shadow), and its map or picture leaves it this dark. The cel fill and the ink read it.
bool solid_shadow_lands_here(Light light, Light_Arrival arrival, vec3 visibility, vec3 N, vec3 world_position)
{
    return LIGHT_SOLID_SHADOWS(light) && arrival.attenuation > 0.0 && dot(N, arrival.direction) > 0.0 &&
           projector_frame_holds(light, world_position) &&
           dot(visibility, vec3(1.0 / 3.0)) < SOLID_SHADOW_DARK_THRESHOLD;
}

// `solid_shadow` is 1 where some solid light's shadow lands on the point.
vec4 gather_direct_light(int look, Surface surface, vec3 world_position, vec3 V, out float solid_shadow)
{
    vec4 lit     = vec4(0.0);
    solid_shadow = 0.0;

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
                          shadow_visibility(light, arrival, world_position, surface.normal) *
                          projector_mask(light, world_position);
        if (solid_shadow_lands_here(light, arrival, visibility, surface.normal, world_position))
            solid_shadow = 1.0;

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
        // The shadow map, and a projector's picture over it (1 for every other kind).
        vec3          visibility = shadow_visibility(light, arrival, world_position, surface.normal) *
                                   projector_mask(light, world_position);
#ifndef LIGHTMAP
        // The probes' static occlusion of a Mixed light, the atlas texel's job at a point in space.
        visibility *= probe_light_visibility(light, world_position);
#endif
        if (solid_shadow_lands_here(light, arrival, visibility, surface.normal, world_position))
            solid_shadow = 1.0;

        lit += shade_light(look, surface, V, incoming_light(light, arrival, visibility));
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
vec3 light_surface_cel(Surface surface, vec3 world_position, vec3 V, out float solid_ink, out float solid_shadow)
{
    Surface white = surface;
    white.albedo  = vec3(1.0);
    white.normal  = surface.geometric_normal;

    vec4 direct  = gather_direct_light(LOOK_CEL, white, world_position, V, solid_shadow);
    vec3 ambient = shade_ambient(LOOK_CEL, white, V, world_position,
                                 baked_irradiance(world_position, white.normal), scene.ambient.rgb);
    return compose_cel(surface, direct, ambient, world_position, solid_ink) + surface.emissive;
}

// `solid_ink` is how far the cel look drew the surface as solid ink (r_cel_black) and `solid_shadow` 1 where a
// solid light's shadow lands, both for store_surface_ink; such a shadow is filled flat with scene.solid_shadow_fill.
vec3 light_surface(int material_look, Surface surface, vec3 world_position, vec3 V, out float solid_ink,
                   out float solid_shadow)
{
    int  look = frame_look(material_look);
    vec3 color;
    if (look == LOOK_CEL)
        color = light_surface_cel(surface, world_position, V, solid_ink, solid_shadow);
    else
    {
        solid_ink = 0.0;
        color     = gather_direct_light(look, surface, world_position, V, solid_shadow).rgb +
                    shade_ambient(look, surface, V, world_position,
                                  baked_irradiance(world_position, surface.normal), scene.ambient.rgb) +
                    surface.emissive;
    }
    return mix(color, scene.solid_shadow_fill.rgb, solid_shadow * scene.solid_shadow_fill.w);
}

#endif // LIGHT_GATHER_GLSL

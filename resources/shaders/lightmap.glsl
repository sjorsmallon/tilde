#ifndef LIGHTMAP_GLSL
#define LIGHTMAP_GLSL

// Guarded so this file and pbr_lighting.glsl can both be included, in either
// order, by mesh_lit.frag's -DPBR -DLIGHTMAP variant.
#ifndef PI
#define PI 3.14159265359
#endif

// Both are guarded, so including them here costs nothing and stops this file
// depending on the order its includer wrote its own #includes in.
#include "scene.glsl"
#include "light_arrival.glsl"

// shared/lightmap.hpp's LIGHTMAP_LIGHTS_PER_CHART, and the renderer static_asserts
// the two agree. Four is what fits a UNORM8x4 texel, which is why the sample
// below is one vec4 and the slots one ivec4.
#define LIGHTMAP_LIGHTS_PER_CHART 4

layout(location = 5) in vec3 fragLightmapUV;
// flat, and it has to be: a slot is an identity, and interpolating two vertices
// that named lights 3 and 7 produces light 5. A chart never spans a triangle, so
// every vertex of one carries the same four and the provoking vertex is right.
layout(location = 7) flat in ivec4 fragLightmapSlots;

layout(set = 3, binding = 0) uniform sampler2DArray lightmapAtlas;
layout(set = 3, binding = 2) uniform sampler2DArray lightmapVisibility;

// THE ATLAS IS THE CULLING (lighting_def.md ss14 step 6). A chart ranked the
// map's lights at bake time and kept the four strongest; those four are on the
// vertex. So a lightmapped fragment iterates ITS OWN FOUR and indexes the scene
// array by slot, instead of walking every light in the level asking which of its
// channels each one is. That inversion is what let MAX_LIGHTS go to 64 without
// costing a brush face anything: per-FACE culling, computed once by the bake,
// finer than any per-draw list a renderer could build.
//
// A slot is skipped when it names nothing, when it points past the region the
// gather pass filled (its light entity was deleted since the bake), or when its
// visibility is zero -- which is one answer for two reasons, "fully occluded"
// and "this chart has no channel for you", and the N+1 policy's asymmetry
// arriving at the far end.
// The four coverages of this texel, in CHANNEL order -- one fetch, hoisted out of
// the loop by hand rather than by trusting the compiler to see that a sample at a
// loop-invariant coordinate is loop-invariant.
//
// All zero where the face matched no chart, which is the same answer an
// unclaimed channel gives and is the safe one: zero means both "fully occluded"
// and "this chart has no channel for you", which is the N+1 policy's asymmetry
// arriving at the far end.
vec4 lightmap_coverage()
{
    if (fragLightmapUV.z < 0.0)
        return vec4(0.0);
    return texture(lightmapVisibility, fragLightmapUV);
}

// Which light this channel names, or -1 for a channel that names none and for one
// naming a slot past the region the gather pass filled -- the light entity was
// deleted since the bake, and indexing the array by it would read a neighbour.
int lightmap_chart_slot(int channel)
{
    if (fragLightmapUV.z < 0.0)
        return -1;

    int slot = fragLightmapSlots[channel];
    return (slot < 0 || slot >= scene.baked_light_count) ? -1 : slot;
}

// The IRRADIANCE the atlas still stores, which after step 6 is exactly the
// residual: the lights this chart ranked below its four and dropped. Everything
// it kept is evaluated analytically above, so a texel is no longer a finished
// answer for any light the runtime can see.
//
// The 1/PI is the SHADER's for the reason lighting_def.md ss9 gives -- the solve
// wrote radiance * attenuation * N.L and stopped -- and this is what albedo
// (times kD, where there is one) multiplies.
//
// A negative page is UNLIT_LIGHTMAP_UV: this face matched no chart, and drawing
// it at the ambient floor is what makes a hole in the bake visible rather than
// plausible.
vec3 lightmap_residual_diffuse()
{
    if (fragLightmapUV.z < 0.0)
        return vec3(0.0);
    return texture(lightmapAtlas, fragLightmapUV).rgb / PI;
}

// The diffuse half of the four, for the paths with no BRDF to hand them to:
// grid, blend and the non-PBR arm of lit. Lambert against the SHADED normal with
// the real light direction, which is the whole point of storing a visibility --
// the same light through shade_direct on the PBR path composes to the same
// number at metallic 0 and no maps.
vec3 lightmap_direct_diffuse(vec3 N, vec3 world_position)
{
    vec3 diffuse  = vec3(0.0);
    vec4 coverage = lightmap_coverage();

    for (int channel = 0; channel < LIGHTMAP_LIGHTS_PER_CHART; ++channel)
    {
        int slot = lightmap_chart_slot(channel);
        if (slot < 0 || coverage[channel] <= 0.0)
            continue;

        Light         light   = scene.lights[slot];
        Light_Arrival arrival = light_arrival(light, world_position);

        float normal_dot_light = max(dot(N, arrival.direction), 0.0);
        diffuse += light.radiance.rgb *
                   (arrival.attenuation * coverage[channel] * normal_dot_light) / PI;
    }

    return diffuse;
}

#endif // LIGHTMAP_GLSL

#ifndef REFLECTION_GLSL
#define REFLECTION_GLSL

// Gate 6 step 5: the room's reflection, for every surface with a roughness.
// A capture photographed the LIT room once from its point; a fragment asks
// what is out there along its reflection direction, and the answer is a fetch
// along the direction the capture sees that same wall point in -- which is
// what the measured box is for (lighting_def.md gate 6, "the mechanism").
//
// Bindings 12..14 of the pass set; renderer.cpp static_asserts the table's
// layout against this block. The pick is find_captures_for's arithmetic
// (lightmap_reflections.cpp) over the eight corners of the lattice cell the
// point is in: the nearest four present, weighted 1 / max(distance, 1) and
// normalised, so a point ON a capture reads that capture almost alone.

#include "scene.glsl"

#define MAX_REFLECTION_CAPTURES    256
#define REFLECTION_BLEND_COUNT     4
#define REFLECTION_CUBE_FACE_COUNT 6

struct Reflection_Capture
{
    vec4 position_and_layer;     // xyz the capture point, w the first cube layer
    vec4 box_min_and_open_faces; // w the open-face bits
    vec4 box_max_and_overridden; // w 1 when an author's volume set the box
};

layout(set = 3, binding = 12) uniform samplerCubeArray reflectionCubes;
layout(std430, set = 3, binding = 13) readonly buffer ReflectionTable
{
    vec4               lattice_origin_and_spacing;    // xyz the lattice origin, w the spacing
    ivec4              lattice_count_and_capture_count; // xyz the lattice extent, w the capture count
    Reflection_Capture captures[MAX_REFLECTION_CAPTURES];
    int                cells[]; // one per lattice cell: a capture index, or -1
} reflection_table;
layout(set = 3, binding = 14) uniform sampler2D environmentBrdf;

struct Reflection_Pick
{
    int   count;
    int   indices[REFLECTION_BLEND_COUNT];
    float weights[REFLECTION_BLEND_COUNT];
};

// The nearest four captures among the eight corners of the cell around P,
// sorted nearest first. A clamped corner names the same cell twice and a
// dropped candidate names none; both are skipped, which is what makes the
// weights below sum to one over exactly the captures present.
Reflection_Pick pick_reflection_captures(vec3 P)
{
    Reflection_Pick pick;
    pick.count = 0;
    for (int slot = 0; slot < REFLECTION_BLEND_COUNT; ++slot)
    {
        pick.indices[slot] = -1;
        pick.weights[slot] = 0.0;
    }

    ivec3 lattice_count = reflection_table.lattice_count_and_capture_count.xyz;
    if (reflection_table.lattice_count_and_capture_count.w <= 0 || any(lessThanEqual(lattice_count, ivec3(0))))
        return pick;

    vec3  origin  = reflection_table.lattice_origin_and_spacing.xyz;
    float spacing = reflection_table.lattice_origin_and_spacing.w;
    ivec3 base    = ivec3(floor((P - origin) / spacing));

    float distances[REFLECTION_BLEND_COUNT];
    for (int corner = 0; corner < 8; ++corner)
    {
        ivec3 cell = clamp(base + ivec3(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1),
                           ivec3(0), lattice_count - ivec3(1));
        int index = reflection_table.cells[(cell.z * lattice_count.y + cell.y) * lattice_count.x + cell.x];
        if (index < 0)
            continue;

        bool already_picked = false;
        for (int slot = 0; slot < pick.count; ++slot)
            already_picked = already_picked || pick.indices[slot] == index;
        if (already_picked)
            continue;

        float distance_to_capture =
            length(reflection_table.captures[index].position_and_layer.xyz - P);
        if (pick.count == REFLECTION_BLEND_COUNT && distance_to_capture >= distances[pick.count - 1])
            continue;

        int slot = min(pick.count, REFLECTION_BLEND_COUNT - 1);
        while (slot > 0 && distances[slot - 1] > distance_to_capture)
        {
            distances[slot]    = distances[slot - 1];
            pick.indices[slot] = pick.indices[slot - 1];
            --slot;
        }
        distances[slot]    = distance_to_capture;
        pick.indices[slot] = index;
        if (pick.count < REFLECTION_BLEND_COUNT)
            ++pick.count;
    }

    float total = 0.0;
    for (int slot = 0; slot < pick.count; ++slot)
    {
        pick.weights[slot] = 1.0 / max(distances[slot], 1.0);
        total += pick.weights[slot];
    }
    for (int slot = 0; slot < pick.count; ++slot)
        pick.weights[slot] /= total;
    return pick;
}

// The parallax correction: where the ray (P, R) leaves the capture's box, seen
// from the capture point. A point outside the box (a fragment around a corner
// its capture never measured) has no exit ahead of it and reads along R
// uncorrected, which is the answer a capture at infinity would give.
vec3 reflection_fetch_direction(Reflection_Capture capture, vec3 P, vec3 R)
{
    vec3 safe_R  = mix(R, vec3(1e-6), lessThan(abs(R), vec3(1e-6)));
    vec3 inverse = 1.0 / safe_R;
    vec3 to_min  = (capture.box_min_and_open_faces.xyz - P) * inverse;
    vec3 to_max  = (capture.box_max_and_overridden.xyz - P) * inverse;
    vec3 exit    = max(to_min, to_max);
    float t      = min(exit.x, min(exit.y, exit.z));
    if (t <= 0.0)
        return R;
    return normalize(P + R * t - capture.position_and_layer.xyz);
}

// The radiance arriving at P along R at one roughness: the four captures,
// each parallax-corrected and fetched at the prefiltered mip that roughness
// names (prefilter_reflection_cube: mip m is roughness m / (mip_count - 1)),
// blended by weight. Black where no capture is present -- the stand-in cube
// is black too, so a map with no bake reads zero through either route.
vec3 environment_radiance(vec3 P, vec3 R, float roughness)
{
    Reflection_Pick pick = pick_reflection_captures(P);
    if (pick.count == 0)
        return vec3(0.0);

    float mip = clamp(roughness, 0.0, 1.0) * float(textureQueryLevels(reflectionCubes) - 1);
    vec3  radiance = vec3(0.0);
    for (int slot = 0; slot < pick.count; ++slot)
    {
        Reflection_Capture capture = reflection_table.captures[pick.indices[slot]];
        vec3 D = reflection_fetch_direction(capture, P, R);
        radiance += pick.weights[slot] *
                    textureLod(reflectionCubes, vec4(D, capture.position_and_layer.w / float(REFLECTION_CUBE_FACE_COUNT)), mip).rgb;
    }
    return radiance;
}

// The split-sum specular: the prefiltered radiance times the environment BRDF
// (F0 * scale + bias, environment_brdf.cpp's table, N.V across and roughness
// down). The same F0 shade_direct builds, so a metal's tint is one number on
// both halves of its specular.
vec3 environment_specular(vec3 P, vec3 N, vec3 V, float roughness, vec3 F0)
{
    vec3  R        = reflect(-V, N);
    vec3  radiance = environment_radiance(P, R, roughness);
    float n_dot_v  = max(dot(N, V), 0.0);
    vec2  lut      = texture(environmentBrdf, vec2(n_dot_v, roughness)).rg;
    return radiance * (F0 * lut.x + lut.y);
}

// r_debug_channel = reflection: the corrected fetch alone, before F0 and the
// BRDF, at the roughness the caller shades with -- black wherever there is no
// capture, which is what a map with none must show everywhere.
vec4 reflection_debug_color(vec3 P, vec3 N, vec3 V, float roughness)
{
    return vec4(environment_radiance(P, reflect(-V, N), roughness), 1.0);
}

// r_debug_channel = reflection_capture: the shaded result washed with a colour
// per WINNING capture (the nearest of the four, a hue off the golden ratio so
// lattice neighbours differ), the cascade debug's shape. Magenta where no
// capture covers the point, so "nothing picked" cannot pass for "one capture".
vec4 reflection_capture_debug(vec4 shaded, vec3 P)
{
    if ((scene.debug_flags & DEBUG_FLAG_RENDER_REFLECTION_CAPTURE) == 0)
        return shaded;

    Reflection_Pick pick = pick_reflection_captures(P);
    vec3 tint = vec3(1.0, 0.0, 1.0);
    if (pick.count > 0)
    {
        float hue = fract(float(pick.indices[0]) * 0.618034);
        tint = clamp(abs(fract(hue + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0, 0.0, 1.0);
    }
    return vec4(mix(shaded.rgb, tint, 0.5), shaded.a);
}

#endif // REFLECTION_GLSL

#ifndef REVEAL_GLSL
#define REVEAL_GLSL

#include "scene.glsl"
#include "shading_cel.glsl"

// renderer.hpp's light_cut_t. A revealed draw keeps only the fragments inside a cone that reveals;
// an erased draw loses the fragments inside a cone that erases (shared/reveal_light.hpp). A shadow_solid
// draw keeps only the fragments inside a shadow volume, a shadow_hole draw loses them (shared/shadow_volume.hpp).
layout(constant_id = 3) const int LIGHT_CUT = 0;

const int LIGHT_CUT_NONE         = 0;
const int LIGHT_CUT_REVEALED     = 1;
const int LIGHT_CUT_ERASED       = 2;
const int LIGHT_CUT_SHADOW_SOLID = 3;
const int LIGHT_CUT_SHADOW_HOLE  = 4;

// A shadow volume's rim, in world units inside its nearest plane.
const float SHADOW_RIM_UNITS = 12.0;

// The bright edge's width, as a cosine off the cone's side and as world units off its far end.
const float REVEAL_RIM_COSINE = 0.012;
const float REVEAL_RIM_UNITS  = 24.0;
const vec3  REVEAL_RIM_COLOR  = vec3(1.2, 2.6, 3.0);

// Under the cel look the edge is drawn, not lit: an ink line this many pixels wide along the cut,
// and dots that thicken towards it across the width the bright edge has.
const float REVEAL_LINE_PIXELS = 2.0;
const vec3  REVEAL_INK_COLOR   = vec3(0.0);

// How far inside the nearest boundary of cones [first, first + count), in rim widths; negative is outside every one, and never below -1.
float cone_margin(vec3 world_position, int first, int count)
{
    float margin = -1.0;

    for (int index = first; index < first + count; ++index) {
        const vec3  apex   = scene.reveal_cones[index].apex_range.xyz;
        const float range  = scene.reveal_cones[index].apex_range.w;
        const vec3  axis   = scene.reveal_cones[index].axis_cosine.xyz;
        const float edge   = scene.reveal_cones[index].axis_cosine.w;

        const vec3  to_fragment = world_position - apex;
        const float reach       = length(to_fragment);
        const float cosine      = dot(to_fragment, axis) / max(reach, 1e-4);

        const float inside_angle = (cosine - edge) / REVEAL_RIM_COSINE;
        const float inside_range = (range - reach) / REVEAL_RIM_UNITS;
        margin = max(margin, min(inside_angle, inside_range));
    }
    return margin;
}

float reveal_margin(vec3 world_position)
{
    return cone_margin(world_position, 0, int(scene.reveal_settings.x));
}

float erase_margin(vec3 world_position)
{
    return cone_margin(world_position, int(scene.reveal_settings.x), int(scene.reveal_settings.y));
}

// How deep in shadow, in rim widths: inside every side plane of a volume and past one of its back
// planes (shared/shadow_volume.hpp). Negative is in no volume's shadow, never below -1.
float shadow_margin(vec3 world_position)
{
    float margin = -1.0;
    const int volume_count = int(scene.shadow_volume_settings.x);
    for (int volume = 0; volume < volume_count; ++volume) {
        const int   first  = volume * MAX_SHADOW_VOLUME_PLANES;
        const ivec2 counts = shadow_volume_plane_counts(volume);
        float       inside = 1e9;
        for (int slot = 0; slot < counts.x; ++slot) {
            const vec4 side = scene.shadow_volumes[first + slot];
            inside = min(inside, (side.w - dot(side.xyz, world_position)) / SHADOW_RIM_UNITS);
        }
        float past_back = -1e9;
        for (int slot = SHADOW_VOLUME_SIDE_SLOTS; slot < SHADOW_VOLUME_SIDE_SLOTS + counts.y; ++slot) {
            const vec4 back = scene.shadow_volumes[first + slot];
            past_back = max(past_back, (dot(back.xyz, world_position) - back.w) / SHADOW_RIM_UNITS);
        }
        margin = max(margin, min(inside, past_back));
    }
    return max(margin, -1.0);
}

// Positive inside whatever this draw is kept by, negative outside it; the cut's one rule, which the discard and the rim share.
float cut_margin(vec3 world_position)
{
    if (LIGHT_CUT == LIGHT_CUT_REVEALED)
        return reveal_margin(world_position);
    if (LIGHT_CUT == LIGHT_CUT_ERASED)
        return -erase_margin(world_position);
    if (LIGHT_CUT == LIGHT_CUT_SHADOW_SOLID)
        return shadow_margin(world_position);
    if (LIGHT_CUT == LIGHT_CUT_SHADOW_HOLE)
        return -shadow_margin(world_position);
    return 1.0;
}

void discard_outside_reveal(vec3 world_position)
{
    if (LIGHT_CUT == LIGHT_CUT_REVEALED && reveal_margin(world_position) < 0.0)
        discard;
    if (LIGHT_CUT == LIGHT_CUT_ERASED && erase_margin(world_position) >= 0.0)
        discard;
    if (LIGHT_CUT == LIGHT_CUT_SHADOW_SOLID && shadow_margin(world_position) < 0.0)
        discard;
    if (LIGHT_CUT == LIGHT_CUT_SHADOW_HOLE && shadow_margin(world_position) >= 0.0)
        discard;
}

vec3 reveal_rim(vec3 color, vec3 world_position, vec3 geometric_normal)
{
    if (LIGHT_CUT == LIGHT_CUT_NONE)
        return color;
    float distance_from_edge = cut_margin(world_position);
    float rim = 1.0 - smoothstep(0.0, 1.0, distance_from_edge);
    if (scene.look.x <= 0.5)
        return mix(color, REVEAL_RIM_COLOR, rim);

    float pixels_from_edge = distance_from_edge / max(fwidth(distance_from_edge), 1e-6);
    float line             = 1.0 - smoothstep(REVEAL_LINE_PIXELS - 0.5, REVEAL_LINE_PIXELS + 0.5, pixels_from_edge);

    vec2            plane     = face_plane(world_position, geometric_normal);
    Pixel_Footprint footprint = pixel_footprint(world_position, geometric_normal);
    float           dots      = dither_coverage(plane, footprint, rim * DITHER_DARKEST_TONE);
    return mix(color, REVEAL_INK_COLOR, max(line, dots));
}

#endif

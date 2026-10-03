#ifndef REVEAL_GLSL
#define REVEAL_GLSL

#include "scene.glsl"

// renderer.hpp's light_cut_t. A revealed draw keeps only the fragments inside a cone that reveals;
// an erased draw loses the fragments inside a cone that erases (shared/reveal_light.hpp).
layout(constant_id = 3) const int LIGHT_CUT = 0;

const int LIGHT_CUT_NONE     = 0;
const int LIGHT_CUT_REVEALED = 1;
const int LIGHT_CUT_ERASED   = 2;

// The bright edge's width, as a cosine off the cone's side and as world units off its far end.
const float REVEAL_RIM_COSINE = 0.012;
const float REVEAL_RIM_UNITS  = 24.0;
const vec3  REVEAL_RIM_COLOR  = vec3(1.2, 2.6, 3.0);

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

void discard_outside_reveal(vec3 world_position)
{
    if (LIGHT_CUT == LIGHT_CUT_REVEALED && reveal_margin(world_position) < 0.0)
        discard;
    if (LIGHT_CUT == LIGHT_CUT_ERASED && erase_margin(world_position) >= 0.0)
        discard;
}

vec3 reveal_rim(vec3 color, vec3 world_position)
{
    if (LIGHT_CUT == LIGHT_CUT_NONE)
        return color;
    float distance_from_edge = LIGHT_CUT == LIGHT_CUT_REVEALED ? reveal_margin(world_position)
                                                               : -erase_margin(world_position);
    float rim = 1.0 - smoothstep(0.0, 1.0, distance_from_edge);
    return mix(color, REVEAL_RIM_COLOR, rim);
}

#endif

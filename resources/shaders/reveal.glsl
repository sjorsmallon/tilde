#ifndef REVEAL_GLSL
#define REVEAL_GLSL

#include "scene.glsl"

// A draw whose owner is revealed_by_light keeps only the fragments inside a reveal cone (shared/reveal_light.hpp).
layout(constant_id = 3) const bool REVEALED_BY_LIGHT = false;

// The bright edge's width, as a cosine off the cone's side and as world units off its far end.
const float REVEAL_RIM_COSINE = 0.012;
const float REVEAL_RIM_UNITS  = 24.0;
const vec3  REVEAL_RIM_COLOR  = vec3(1.2, 2.6, 3.0);

// How far inside the nearest cone's boundary, in rim widths; negative is outside every cone.
float reveal_margin(vec3 world_position)
{
    float margin = -1.0;

    const int count = int(scene.reveal_settings.x);
    for (int index = 0; index < count; ++index) {
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

void discard_outside_reveal(vec3 world_position)
{
    if (!REVEALED_BY_LIGHT)
        return;
    if (reveal_margin(world_position) < 0.0)
        discard;
}

vec3 reveal_rim(vec3 color, vec3 world_position)
{
    if (!REVEALED_BY_LIGHT)
        return color;
    float rim = 1.0 - smoothstep(0.0, 1.0, reveal_margin(world_position));
    return mix(color, REVEAL_RIM_COLOR, rim);
}

#endif

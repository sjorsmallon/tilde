#ifndef CLOCK_WIPE_GLSL
#define CLOCK_WIPE_GLSL

#include "mesh_push.glsl"

// A specialization constant, so the discard folds away on every other pipeline
// and early-Z survives -- alpha_cutout.glsl's rule. One bit for the clock wipe AND
// the dissolve (dissolve.glsl): both are the platform's, and a second pipeline
// per combination buys nothing.
layout(constant_id = 2) const bool DISCARD_EFFECTS = false;

void discard_inside_clock_wipe(vec3 world_position)
{
    if (!DISCARD_EFFECTS)
        return;

    const float TAU = 6.28318530718;

    vec3  offset = world_position - pc.clock_wipe_center.xyz;
    float angle  = atan(dot(offset, pc.clock_wipe_axis_y.xyz), dot(offset, pc.clock_wipe_axis_x.xyz));
    if (fract(angle / TAU) < pc.clock_wipe_center.w)
        discard;
}

#endif

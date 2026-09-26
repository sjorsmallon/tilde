#ifndef PEEL_GLSL
#define PEEL_GLSL

#include "clock_wipe.glsl"

// A balloon peel: one hole opens at pc.clock_wipe_center.xyz (a unit direction from the
// model's origin) and its front sweeps the sphere as pc.clock_wipe_axis_y.w runs 0 to pi,
// leaving a shrinking cap at the antipode. A fragment is inside the hole when its angle
// from the hole is under the front, tested as dot > cos(front) so no acos is taken. The
// front wobbles by a few sines of the direction, so it reads as a tear and not a clip
// plane. Rides the platform's DISCARD_EFFECTS bit like the dissolve.

const float PEEL_RIM_ANGLE  = 0.10;
const float PEEL_WAVE_ANGLE = 0.08;
const vec3  PEEL_RIM_COLOR  = vec3(2.5, 2.5, 3.0);
const float PEEL_PI         = 3.14159265;

float peel_front_angle()
{
    return pc.clock_wipe_axis_y.w;
}

// The front angle at this fragment: the pushed front plus the wobble, clamped to the far pole.
float peel_local_front(vec3 direction)
{
    float wave = sin(direction.x * 9.0 + direction.y * 4.0) * sin(direction.z * 7.0 - direction.x * 3.0);
    return clamp(peel_front_angle() + wave * PEEL_WAVE_ANGLE, 0.0, PEEL_PI);
}

vec3 peel_direction(vec3 world_position)
{
    return normalize(world_position - pc.model[3].xyz);
}

void discard_inside_peel(vec3 world_position)
{
    if (!DISCARD_EFFECTS || peel_front_angle() <= 0.0)
        return;
    vec3 direction = peel_direction(world_position);
    if (dot(direction, pc.clock_wipe_center.xyz) > cos(peel_local_front(direction)))
        discard;
}

// 1 at the torn edge, 0 a rim's width behind it, for the fragments the discard kept.
float peel_rim_factor(vec3 world_position)
{
    if (!DISCARD_EFFECTS || peel_front_angle() <= 0.0)
        return 0.0;
    vec3  direction = peel_direction(world_position);
    float front     = peel_local_front(direction);
    float edge      = cos(front);
    float inner     = cos(min(front + PEEL_RIM_ANGLE, PEEL_PI));
    float toward    = dot(direction, pc.clock_wipe_center.xyz);
    return 1.0 - smoothstep(inner, edge, toward);
}

vec3 peel_rim(vec3 color, vec3 world_position)
{
    return mix(color, PEEL_RIM_COLOR, peel_rim_factor(world_position));
}

#endif

#ifndef KEYHOLE_GLSL
#define KEYHOLE_GLSL

#include "mesh_push.glsl"

// The locked wall's keyhole (material_def.md ss7 step 4), read from the draw's clock-wipe frame:
// centred on the frame's centre, across along its x axis, up along the axis its two span. Shared
// by mesh_locked_wall.frag and the shadow pass's shadow_keyhole.frag, so the hole and its shadow
// are one shape.

// World units: the keyhole's height while locked, how far its edge has travelled when the timer
// ends (cubed, so it stays a keyhole for most of the wait), and the width of the dark rim.
const float KEYHOLE_SIZE  = 48.0;
const float KEYHOLE_REACH = 4096.0;
const float KEYHOLE_RIM   = 4.0;

float keyhole_size(float wiped)
{
    return KEYHOLE_SIZE + wiped * wiped * wiped * KEYHOLE_REACH;
}

// Signed distance to a keyhole of the given height, centred on the origin, upright along +y:
// a circle over a slot that widens downward.
float keyhole_distance(vec2 p, float size)
{
    float circle = length(p - vec2(0.0, 0.30 * size)) - 0.28 * size;

    float slot_top    =  0.30 * size;
    float slot_bottom = -0.50 * size;
    float down        = clamp((slot_top - p.y) / (slot_top - slot_bottom), 0.0, 1.0);
    float half_width  = mix(0.10 * size, 0.26 * size, down);
    float slot        = max(abs(p.x) - half_width, max(p.y - slot_top, slot_bottom - p.y));

    return min(circle, slot);
}

// A world position in the keyhole's plane. The up axis is world up for an owner that is only yawed.
vec2 keyhole_plane_position(vec3 world_position)
{
    vec3 across = pc.clock_wipe_axis_x.xyz;
    vec3 up     = cross(pc.clock_wipe_axis_y.xyz, across);
    if (dot(up, up) < 1e-6)
    {
        across = vec3(1.0, 0.0, 0.0);
        up     = vec3(0.0, 1.0, 0.0);
    }
    vec3 offset = world_position - pc.clock_wipe_center.xyz;
    return vec2(dot(offset, normalize(across)), dot(offset, normalize(up)));
}

// The signed distance at a world position, for the keyhole the frame's fraction has opened to.
float keyhole_at(vec3 world_position)
{
    return keyhole_distance(keyhole_plane_position(world_position), keyhole_size(pc.clock_wipe_center.w));
}

#endif

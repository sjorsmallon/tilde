#ifndef REVEAL_GLSL
#define REVEAL_GLSL

#include "scene.glsl"
#include "shading_cel.glsl"
#include "ripple.glsl"

// renderer.hpp's light_cut_t. A revealed draw keeps only the fragments inside a cone that reveals;
// an erased draw loses the fragments inside a cone that erases (shared/lighting/reveal_light.hpp). A shadow_solid
// draw keeps only the fragments inside a shadow volume, a shadow_hole draw loses them (shared/lighting/shadow_volume.hpp).
layout(constant_id = 3) const int LIGHT_CUT = 0;

const int LIGHT_CUT_NONE         = 0;
const int LIGHT_CUT_REVEALED     = 1;
const int LIGHT_CUT_ERASED       = 2;
const int LIGHT_CUT_SHADOW_SOLID = 3;
const int LIGHT_CUT_SHADOW_HOLE  = 4;
// A lensed draw keeps only the fragments seen through a lens that passes its colour (shared/lighting/lens.hpp);
// the three are one cut, the colour's channel bit being 1 << (LIGHT_CUT - LIGHT_CUT_LENS_RED).
const int LIGHT_CUT_LENS_RED     = 5;
const int LIGHT_CUT_LENS_GREEN   = 6;
const int LIGHT_CUT_LENS_BLUE    = 7;
// A lens-watched draw keeps the fragments ANY watcher sees through such a lens (lens.hpp's any_lens_watcher_sees_point):
// what is drawn is what is solid, on every screen. The colour is the same bit, 1 << (LIGHT_CUT - LIGHT_CUT_LENS_WATCHED_RED).
const int LIGHT_CUT_LENS_WATCHED_RED   = 8;
const int LIGHT_CUT_LENS_WATCHED_GREEN = 9;
const int LIGHT_CUT_LENS_WATCHED_BLUE  = 10;

bool light_cut_is_own_lens()
{
    return LIGHT_CUT >= LIGHT_CUT_LENS_RED && LIGHT_CUT <= LIGHT_CUT_LENS_BLUE;
}

bool light_cut_is_watched_lens()
{
    return LIGHT_CUT >= LIGHT_CUT_LENS_WATCHED_RED;
}

// A lens's rim, in world units inside its silhouette.
const float LENS_RIM_UNITS = 12.0;

// A shadow volume's rim, in world units inside its nearest plane.
const float SHADOW_RIM_UNITS = 12.0;
// A fragment this close to a front plane is ON the face the shadow begins at, not in it.
const float SHADOW_FRONT_SLACK_UNITS = 0.1;

// The bright edge's width, as a cosine off the cone's side and as world units off its far end.
const float REVEAL_RIM_COSINE = 0.012;
const float REVEAL_RIM_UNITS  = 24.0;
const vec3  REVEAL_RIM_COLOR  = vec3(1.2, 2.6, 3.0);

// Under the cel look the edge is drawn, not lit: an ink line this many pixels wide along the cut,
// and dots that thicken towards it across the width the bright edge has.
const float REVEAL_LINE_PIXELS = 2.0;
const vec3  REVEAL_INK_COLOR   = vec3(0.0);

// A cut draw keeps a fragment a ripple crest (ripple.glsl) is at least this high on, and reveal_rim
// draws it as the cut's edge: a rocket's ring on a wall you cannot see shows where the wall is.
const float RIPPLE_SHOWS_THROUGH_HEIGHT = 0.3;

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

// How deep in shadow, in rim widths inside the nearest side plane of a volume whose every front plane
// the fragment is behind (shared/lighting/shadow_volume.hpp). The front planes gate and never grade: they are a
// caster's own faces, and an erased caster's turned-away face lies exactly on the one its shadow begins at.
// Negative is in no volume's shadow, never below -1. A volume thrown for
// a beam alone is skipped, as collision skips it: the picture and the floor read the same volumes.
float shadow_margin(vec3 world_position)
{
    float margin = -1.0;
    const int volume_count = int(scene.shadow_volume_settings.x);
    for (int volume = 0; volume < volume_count; ++volume) {
        if (!shadow_volume_cuts_geometry(volume))
            continue;
        const int   first  = volume * MAX_SHADOW_VOLUME_PLANES;
        const ivec2 counts = shadow_volume_plane_counts(volume);
        float       inside = 1e9;
        for (int slot = 0; slot < counts.x; ++slot) {
            const vec4 side = scene.shadow_volumes[first + slot];
            inside = min(inside, (side.w - dot(side.xyz, world_position)) / SHADOW_RIM_UNITS);
        }
        bool behind_front = true;
        for (int slot = SHADOW_VOLUME_SIDE_SLOTS; slot < SHADOW_VOLUME_SIDE_SLOTS + counts.y; ++slot) {
            const vec4 front = scene.shadow_volumes[first + slot];
            behind_front = behind_front && front.w - dot(front.xyz, world_position) > SHADOW_FRONT_SLACK_UNITS;
        }
        if (behind_front)
            margin = max(margin, inside);
    }
    return max(margin, -1.0);
}

// `v` turned by the unit quaternion `q`, linalg.hpp's rotate.
vec3 rotate_by_quaternion(vec4 q, vec3 v)
{
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

// How far inside a lens's silhouette the fragment is seen from `eye`, in rim widths (shared/lighting/lens.hpp's
// lens_lies_on_line_of_sight): the shorter of the chord the eye's line cuts through the lens and how far
// behind the lens's far side the fragment lies, the eye's line taken into each lens's own frame. The channels
// of every lens on the line multiply, so a red and a green stacked pass nothing. -1 when no lens on the line
// passes `channel`. `held` is the channel bits of a lens held to the eye, on every line and with no rim.
float lens_margin_from_eye(vec3 eye, vec3 world_position, int channel, int held)
{
    const vec3  line    = world_position - eye;
    const float reach   = length(line);

    int   passed = held == 0 ? 7 : held;
    bool  seen   = held != 0;
    float margin = 1e9;
    const int lens_count = int(scene.lens_settings.x);
    for (int index = 0; index < lens_count; ++index) {
        const vec3 center       = scene.lenses[index].center_channels.xyz;
        const vec3 half_extents = scene.lenses[index].half_extents.xyz;
        const vec4 into_box     = scene.lenses[index].orientation * vec4(-1.0, -1.0, -1.0, 1.0);
        const vec3 origin       = rotate_by_quaternion(into_box, eye - center);
        const vec3 line_in_box  = rotate_by_quaternion(into_box, line);
        const vec3 inverse_line = 1.0 / mix(line_in_box, vec3(1e-5), lessThan(abs(line_in_box), vec3(1e-5)));
        const vec3 low          = (-half_extents - origin) * inverse_line;
        const vec3 high         = (half_extents - origin) * inverse_line;
        const vec3 entering     = min(low, high);
        const vec3 leaving      = max(low, high);
        const float enter       = max(max(entering.x, entering.y), max(entering.z, 0.0));
        const float exit        = min(min(leaving.x, leaving.y), leaving.z);
        if (enter >= exit || exit >= 1.0)
            continue;
        passed = passed & int(scene.lenses[index].center_channels.w + 0.5);
        seen   = true;
        margin = min(margin, min(exit - enter, 1.0 - exit) * reach / LENS_RIM_UNITS);
    }
    if (!seen || (passed & channel) == 0)
        return -1.0;
    return margin;
}

// The own-eye cut: seen through a lens from the camera.
float lens_margin(vec3 world_position)
{
    return lens_margin_from_eye(scene.camera_position.xyz, world_position, 1 << (LIGHT_CUT - LIGHT_CUT_LENS_RED),
                                int(scene.lens_settings.y + 0.5));
}

// The watched cut: the deepest any watcher with the fragment in view sees it (lens.hpp's any_lens_watcher_sees_point).
float lens_watched_margin(vec3 world_position)
{
    const int channel = 1 << (LIGHT_CUT - LIGHT_CUT_LENS_WATCHED_RED);
    float     margin  = -1.0;
    const int watcher_count = int(scene.lens_watcher_settings.x);
    for (int index = 0; index < watcher_count; ++index) {
        const vec3 eye   = scene.lens_watchers[index].eye.xyz;
        bool       faced = true;
        for (int side = 0; side < 4; ++side) {
            const vec4 plane = scene.lens_watchers[index].sides[side];
            faced = faced && dot(plane.xyz, world_position) <= plane.w;
        }
        if (!faced)
            continue;
        margin = max(margin, lens_margin_from_eye(eye, world_position, channel,
                                                  int(scene.lens_watchers[index].eye.w + 0.5)));
    }
    return margin;
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
    if (light_cut_is_own_lens())
        return lens_margin(world_position);
    if (light_cut_is_watched_lens())
        return lens_watched_margin(world_position);
    return 1.0;
}

void discard_outside_reveal(vec3 world_position)
{
    if (LIGHT_CUT == LIGHT_CUT_NONE)
        return;
    if (sample_ripples(world_position).height > RIPPLE_SHOWS_THROUGH_HEIGHT)
        return;
    if (LIGHT_CUT == LIGHT_CUT_REVEALED && reveal_margin(world_position) < 0.0)
        discard;
    if (LIGHT_CUT == LIGHT_CUT_ERASED && erase_margin(world_position) >= 0.0)
        discard;
    if (LIGHT_CUT == LIGHT_CUT_SHADOW_SOLID && shadow_margin(world_position) < 0.0)
        discard;
    if (LIGHT_CUT == LIGHT_CUT_SHADOW_HOLE && shadow_margin(world_position) >= 0.0)
        discard;
    if (light_cut_is_own_lens() && lens_margin(world_position) < 0.0)
        discard;
    if (light_cut_is_watched_lens() && lens_watched_margin(world_position) < 0.0)
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

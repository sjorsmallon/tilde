#version 450

// The beam pass (spot_beam_plan.md ss3 to ss5): one beam of the pass, over the HDR scene after it is drawn,
// inside the box beam.vert draws around its cone. A pixel's line of sight is clipped to the beam's round
// cone, its range sphere and the surface under the pixel, and that chord is measured through the beam's
// CARVE (shared/solid_beams.hpp): the lit part of the cone as disjoint convex pieces, each the cone cut by
// the planes in scene.beam_planes, so the lengths add. The fill is the one fixed alpha where any of the
// chord is lit, feathered one pixel. Lines in the light's colour, r_beam_edge_pixels wide, are drawn at the
// cone's silhouette where the chord there is lit, and where the lit length reaches zero on one surface: a
// hole's rim, a shadow's edge seen from beside it, the edge where the beam lands. Across a depth jump (a
// crate's silhouette inside the beam) only the cone's silhouette counts; the ink pass draws that edge. A
// beam with no carve (the editor's) is lit along its whole chord. Beams over one another compose through
// the pass's blend, one draw each, front to back. The drawn shadow volumes follow in the same pass, one box
// each (shadow_body.frag).

layout(set = 0, binding = 0) uniform sampler2D scene_depth;

#include "scene.glsl"
#include "shadow_chord.glsl"

layout(location = 0) flat in int in_beam;
layout(location = 0) out vec4 outColor;

// A dot's radius as a share of the spacing, and the 45 degree turn a printed screentone sits at.
const float BEAM_DOT_RADIUS  = 0.18;
const float BEAM_DOT_ANGLE   = 0.7853981634;
const float PI               = 3.14159265;

// One for a flat wash; for dots, the dot's own alpha is raised by its share of the cell so the
// average tint over the beam is the wash's.
float beam_fill_factor()
{
    if (int(scene.beam.x) == BEAM_FILL_TINT)
        return 1.0;
    float spacing     = scene.beam.z;
    mat2  turn        = mat2(cos(BEAM_DOT_ANGLE), sin(BEAM_DOT_ANGLE), -sin(BEAM_DOT_ANGLE), cos(BEAM_DOT_ANGLE));
    vec2  cell        = fract(turn * gl_FragCoord.xy / spacing) - 0.5;
    float from_centre = length(cell);
    float edge        = fwidth(from_centre);
    float inside_dot  = 1.0 - smoothstep(BEAM_DOT_RADIUS - edge, BEAM_DOT_RADIUS + edge, from_centre);
    float coverage    = PI * BEAM_DOT_RADIUS * BEAM_DOT_RADIUS;
    return inside_dot / coverage;
}

void main()
{
    vec2  position    = (gl_FragCoord.xy - scene.beam_viewport.xy) / scene.beam_viewport.zw;
    float stored      = texelFetch(scene_depth, ivec2(gl_FragCoord.xy), 0).r;
    float view_depth  = view_depth_from_stored(stored);
    vec3  ray         = view_ray(position);
    vec3  origin      = scene.camera_position.xyz;
    float fill        = beam_fill_factor();
    bool  one_surface = fwidth(view_depth) < DEPTH_JUMP_SHARE * view_depth;

    Beam beam      = scene.beams[in_beam];
    vec3 from_apex = origin - beam.apex_range.xyz;

    // The chord through the air alone, then the part of it in front of the surface under the pixel. The
    // chord stops a hair short of the surface: the depth's rounding can put the surface a hair past the
    // plane a piece ends on, which would let a sliver of the piece behind the surface into the sum (an
    // erased caster's lit body under its own shadowed face) and speckle the floor.
    float air_enter = 0.0;
    float air_leave = EMPTY_ENTER;
    clip_to_cone(air_enter, air_leave, from_apex, ray, beam.forward_cosine.xyz, beam.forward_cosine.w);
    clip_to_sphere(air_enter, air_leave, from_apex, ray, beam.apex_range.w);
    float enter     = air_enter;
    float leave     = min(air_leave, view_depth * (1.0 - SURFACE_BIAS_SHARE));
    float air_chord = max(air_leave - air_enter, 0.0);
    float chord     = max(leave - enter, 0.0);

    // The lit length: the chord clipped by each piece's cut planes, summed over the disjoint pieces.
    int   piece_first = int(beam.pieces.x);
    int   piece_count = int(beam.pieces.y);
    float lit         = piece_count > 0 ? 0.0 : chord;
    for (int piece = piece_first; piece < piece_first + piece_count; ++piece)
    {
        int   packed      = floatBitsToInt(scene.beam_pieces[piece >> 2][piece & 3]);
        int   first       = packed & 0xFFFF;
        int   count       = packed >> 16;
        float piece_enter = enter;
        float piece_leave = leave;
        for (int slot = first; slot < first + count; ++slot)
        {
            vec4 plane = scene.beam_planes[slot];
            clip_to_half_space(piece_enter, piece_leave, plane.xyz, plane.w, origin, ray);
        }
        lit += max(piece_leave - piece_enter, 0.0);
    }

    // Every derivative is read before any pixel is let go. The cone's and the range's silhouettes are
    // measured on the SQUARE of the air chord, which grows linearly from a quadric's silhouette where the
    // chord itself grows as a root and would put the line at half its width; a plane's cut and the surface's
    // clip are linear in the lit length and measured on it.
    float lit_pixels = pixels_to_zero(lit);
    float air_pixels = pixels_to_zero(air_chord * air_chord);
    float lit_share  = chord > 0.0 ? clamp(lit / chord, 0.0, 1.0) : 0.0;
    float lit_cover  = lit > 0.0 ? clamp(lit_pixels, 0.0, 1.0) : 0.0;
    float cut_line   = lit > 0.0 && one_surface ? outline_at(lit_pixels) : 0.0;
    float rim_line   = outline_at(air_pixels) * smoothstep(0.25, 0.75, lit_share);

    if (chord <= 0.0)
    {
        outColor = vec4(0.0);
        return;
    }
    if ((scene.debug_flags & DEBUG_FLAG_RENDER_BEAM_TERMS) != 0)
    {
        vec3 terms = mix(vec3(0.6, 0.0, 0.6), vec3(0.75), lit_cover);
        terms      = mix(terms, vec3(1.0, 0.0, 0.0), rim_line);
        terms      = mix(terms, vec3(0.0, 0.3, 1.0), cut_line);
        outColor   = vec4(terms, 1.0);
        return;
    }
    if ((scene.debug_flags & DEBUG_FLAG_RENDER_BEAM_SHADOW) != 0)
    {
        outColor = vec4(vec3(0.12 + 0.88 * lit_share), 1.0);
        return;
    }
    float wash  = scene.beam.y * fill * lit_cover;
    float alpha = clamp(max(wash, max(rim_line, cut_line)), 0.0, 1.0);

    outColor = vec4(beam.color_light.rgb, alpha);
}

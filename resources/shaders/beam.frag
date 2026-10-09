#version 450

// The beam pass (spot_beam_plan.md ss3 to ss5): one beam of the pass, over the HDR scene after it is drawn,
// inside the box beam.vert draws around its cone. A pixel's line of sight is clipped to the beam's cone, its
// range and the surface under the pixel; the shadow volumes the beam's own light throws are subtracted from
// that chord, and the tint is the fill's one alpha scaled by the share of the chord still lit, so an
// unshadowed beam is the same flat wash whatever its depth and a caster's shaft dims it in proportion; where
// the chord ends inside a shaft nothing is drawn at all, so a shaft seen from behind its caster is an empty
// hole. Lines in the light's colour, r_beam_edge_pixels wide, are drawn where the chord ends on screen (the
// cone's silhouette and where it lands) and along every shaft (its sides from beside it, the hole's rim from
// behind). Beams over one another compose through the pass's blend, one draw each, front to back.
// The drawn shadow volumes follow in the same pass, one box each (shadow_body.frag).

layout(set = 0, binding = 0) uniform sampler2D scene_depth;

#include "scene.glsl"
#include "shadow_chord.glsl"

layout(location = 0) flat in int in_beam;
layout(location = 0) out vec4 outColor;

// A dot's radius as a share of the spacing, and the 45 degree turn a printed screentone sits at.
const float BEAM_DOT_RADIUS  = 0.18;
const float BEAM_DOT_ANGLE   = 0.7853981634;
const float PI               = 3.14159265;

// r_debug_channel beam_shadow: one colour per scene volume index, cl_shadow_volume_debug's labels.
const vec3 DEBUG_VOLUME_COLORS[8] = vec3[8](vec3(0.0, 1.0, 1.0), vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 1.0),
                                            vec3(0.0, 1.0, 0.0), vec3(1.0, 0.5, 0.0), vec3(1.0, 1.0, 1.0),
                                            vec3(1.0, 0.0, 0.6), vec3(0.3, 0.3, 1.0));

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

    // The chord through the lit air alone, then the part of it in front of the surface under the pixel.
    float air_enter = 0.0;
    float air_leave = EMPTY_ENTER;
    clip_to_cone(air_enter, air_leave, from_apex, ray, beam.forward_cosine.xyz, beam.forward_cosine.w);
    clip_to_sphere(air_enter, air_leave, from_apex, ray, beam.apex_range.w);
    float enter     = air_enter;
    float leave     = min(air_leave, view_depth);
    float air_chord = max(air_leave - air_enter, 0.0);
    float chord     = max(leave - enter, 0.0);

    // The beam's outline: how many pixels this one is from where the chord ends on screen. The cone's
    // and the range's silhouettes are measured on the SQUARE of the air chord, which grows linearly
    // from a quadric's silhouette where the chord itself grows as a root and would put the line at half
    // its width; where the pixel's surface is one, the edge where the surface clips the chord is linear
    // in the chord and measured on it. Across a depth jump the air alone counts, so a crate's own
    // silhouette inside the beam draws no line. Every derivative below is read before any pixel is let
    // go, which only uniforms steer until then. The line is cut by whether the RIM is lit, measured at
    // the chord's midpoint, which is the rim where the chord is short; the chord's lit share would thin
    // the line a pixel in, where the chord already crosses a shaft behind the rim.
    float air_line  = outline_at(pixels_to_zero(air_chord * air_chord));
    float line      = one_surface ? max(air_line, outline_at(pixels_to_zero(chord))) : air_line;
    float rim_t     = one_surface ? 0.5 * (enter + leave) : 0.5 * (air_enter + air_leave);
    vec3  rim_point = origin + rim_t * ray;

    // The shafts: each volume of this light takes a length out of the chord, zero at the shaft's own
    // silhouette, which draws its sides from beside it and the hole's rim from behind. The point the
    // chord ends at is in a shaft or not; its signed distance to the nearest shaft's boundary draws
    // the hole's rim on whatever the beam lands on. Only a chord the surface ends lands anywhere: one
    // that leaves the cone or its range in the air has no hole and no rim, or every side of a shaft
    // would draw its cut through the range sphere across the sky. The surface bias is a tolerance on
    // the caster's lit faces at the chord's end, so the chord is measured whole and its end stays on
    // the surface. r_beam_surface_bias off is the old lift of every chord's end along the view ray,
    // which left a false lit share where a chord is shorter than the lift (the rim of a shaft against
    // the air) and let the end escape a shaft where its wedge is thinner than the lift (its far edge
    // on a floor).
    int   light_bits       = floatBitsToInt(beam.color_light.w);
    bool  lands            = view_depth <= air_leave;
    bool  lift_bias        = scene.beam_settings.w == 0.0;
    float bias             = SURFACE_BIAS_SHARE * view_depth;
    float lit_tolerance    = lift_bias ? 0.0 : bias;
    float shadow_leave     = lift_bias ? leave - bias : leave;
    float end_t            = chord > 0.0 ? shadow_leave : (lift_bias ? view_depth - bias : view_depth);
    vec3  end_point        = origin + end_t * ray;
    bool  one_end          = fwidth(end_t) < DEPTH_JUMP_SHARE * end_t;
    float shadowed         = 0.0;
    float end_margin       = EMPTY_ENTER;
    float rim_margin       = EMPTY_ENTER;
    float shaft_line       = 0.0;
    int   deepest_volume   = -1;
    float deepest_shaft    = 0.0;
    int   volume_count     = int(scene.shadow_volume_settings.x);
    for (int volume = 0; volume < volume_count; ++volume)
    {
        if (floatBitsToInt(scene.shadow_volume_lights[volume >> 2][volume & 3]) != light_bits)
            continue;
        float in_shaft = max(shadowed_length(volume, origin, ray, enter, shadow_leave), 0.0);
        shadowed      += in_shaft;
        if (in_shaft > deepest_shaft)
        {
            deepest_shaft  = in_shaft;
            deepest_volume = volume;
        }
        shaft_line     = max(shaft_line, in_shaft > 0.0 ? outline_at(pixels_to_zero(in_shaft)) : 0.0);
        end_margin     = min(end_margin, shadow_margin_at(volume, end_point, lit_tolerance));
        rim_margin     = min(rim_margin, shadow_margin_at(volume, rim_point, lit_tolerance));
    }
    float hole_pixels = pixels_to_zero(end_margin);
    float rim_pixels  = pixels_to_zero(rim_margin);
    float hole_line   = lands && end_margin > 0.0 && one_end ? outline_at(hole_pixels) : 0.0;

    if (chord <= 0.0)
    {
        outColor = vec4(0.0);
        return;
    }

    float lit     = 1.0 - clamp(shadowed / chord, 0.0, 1.0);
    float rim_lit = rim_margin > 0.0 ? clamp(rim_pixels, 0.0, 1.0) : 0.0;
    float arrives = lands && end_margin < 0.0 ? 0.0 : 1.0;

    if ((scene.debug_flags & DEBUG_FLAG_RENDER_BEAM_TERMS) != 0)
    {
        vec3 terms = arrives > 0.0 ? vec3(0.15 + 0.6 * lit) : vec3(0.6, 0.0, 0.6);
        terms      = mix(terms, vec3(1.0, 0.0, 0.0), line * rim_lit);
        terms      = mix(terms, vec3(0.0, 1.0, 0.0), shaft_line);
        terms      = mix(terms, vec3(0.0, 0.3, 1.0), hole_line);
        outColor   = vec4(terms, 1.0);
        return;
    }
    if ((scene.debug_flags & DEBUG_FLAG_RENDER_BEAM_SHADOW) != 0)
    {
        float share = clamp(deepest_shaft / chord, 0.0, 1.0);
        vec3  shown = deepest_volume < 0 ? vec3(0.12) : DEBUG_VOLUME_COLORS[deepest_volume & 7] * (0.25 + 0.75 * share);
        outColor    = vec4(shown, 1.0);
        return;
    }
    float wash    = scene.beam.y * fill * lit * arrives;
    float lines   = max(line * rim_lit, max(shaft_line, hole_line));
    float alpha   = clamp(max(wash, lines), 0.0, 1.0);

    outColor = vec4(beam.color_light.rgb, alpha);
}

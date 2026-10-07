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

    // The beam's outline: how many pixels this one is from where the chord ends on screen, measured on
    // the chord the surface clips where the pixel's surface is one, and on the air alone across a depth
    // jump, so a crate's own silhouette inside the beam draws no line. Every derivative below is read
    // before any pixel is let go, which only uniforms steer until then.
    float line = outline_at(one_surface ? pixels_to_zero(chord) : pixels_to_zero(air_chord));

    // The shafts: each volume of this light takes a length out of the chord, zero at the shaft's own
    // silhouette, which draws its sides from beside it and the hole's rim from behind. The point the
    // chord ends at is in a shaft or not; its signed distance to the nearest shaft's boundary draws
    // the hole's rim on whatever the beam lands on. Only a chord the surface ends lands anywhere: one
    // that leaves the cone or its range in the air has no hole and no rim, or every side of a shaft
    // would draw its cut through the range sphere across the sky.
    int   light_bits       = floatBitsToInt(beam.color_light.w);
    float short_of_surface = SURFACE_BIAS_SHARE * view_depth;
    float shadow_leave     = leave - short_of_surface;
    float end_t            = chord > 0.0 ? shadow_leave : view_depth - short_of_surface;
    vec3  end_point        = origin + end_t * ray;
    bool  lands            = view_depth <= air_leave;
    bool  one_end          = fwidth(end_t) < DEPTH_JUMP_SHARE * end_t;
    float shadowed         = 0.0;
    float end_margin       = EMPTY_ENTER;
    float shaft_line       = 0.0;
    int   volume_count     = int(scene.shadow_volume_settings.x);
    for (int volume = 0; volume < volume_count; ++volume)
    {
        if (floatBitsToInt(scene.shadow_volume_lights[volume >> 2][volume & 3]) != light_bits)
            continue;
        float in_shaft = max(shadowed_length(volume, origin, ray, enter, shadow_leave), 0.0);
        shadowed      += in_shaft;
        shaft_line     = max(shaft_line, in_shaft > 0.0 ? outline_at(pixels_to_zero(in_shaft)) : 0.0);
        end_margin     = min(end_margin, shadow_margin_at(volume, end_point));
    }
    float hole_pixels = pixels_to_zero(end_margin);
    float hole_line   = lands && end_margin > 0.0 && one_end ? outline_at(hole_pixels) : 0.0;

    if (chord <= 0.0)
    {
        outColor = vec4(0.0);
        return;
    }

    float lit     = 1.0 - clamp(shadowed / chord, 0.0, 1.0);
    float arrives = lands && end_margin < 0.0 ? 0.0 : 1.0;
    float wash    = scene.beam.y * fill * lit * arrives;
    float lines   = max(line * lit, max(shaft_line, hole_line));
    float alpha   = clamp(max(wash, lines), 0.0, 1.0);

    outColor = vec4(beam.color_light.rgb, alpha);
}

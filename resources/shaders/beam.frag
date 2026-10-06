#version 450

// The beam pass (spot_beam_plan.md ss3, ss5): every beam of the pass, over the HDR scene after it is drawn.
// A pixel's line of sight is clipped to each beam's pyramid and to the surface under the pixel; the
// shadow volumes the beam's own light throws are subtracted from that chord, and the tint is the
// fill's one alpha scaled by the share of the chord still lit, so an unshadowed beam is the same flat
// wash whatever its depth and a caster's shaft dims it in proportion.

layout(set = 0, binding = 0) uniform sampler2D scene_depth;

#include "scene.glsl"

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 outColor;

// A dot's radius as a share of the spacing, and the 45 degree turn a printed screentone sits at.
const float BEAM_DOT_RADIUS = 0.18;
const float BEAM_DOT_ANGLE  = 0.7853981634;
const float PI              = 3.14159265;
const float EMPTY_ENTER     = 1e9;
const float EMPTY_LEAVE     = -1e9;

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

// Keeps of [enter, leave] the part where dot(normal, origin + t * ray) <= limit.
void clip_to_half_space(inout float enter, inout float leave, vec3 normal, float limit, vec3 origin, vec3 ray)
{
    float at_origin = dot(normal, origin) - limit;
    float per_unit  = dot(normal, ray);
    if (abs(per_unit) < 1e-7)
    {
        if (at_origin > 0.0)
        {
            enter = EMPTY_ENTER;
            leave = EMPTY_LEAVE;
        }
        return;
    }
    float t = -at_origin / per_unit;
    if (per_unit > 0.0)
        leave = min(leave, t);
    else
        enter = max(enter, t);
}

// How much of [enter, leave] lies in shadow volume `volume`: inside every side plane and not still in
// front of the caster, which is inside every back plane (reveal.glsl's shadow_margin, as lengths).
float shadowed_length(int volume, vec3 origin, vec3 ray, float enter, float leave)
{
    int   first      = volume * MAX_SHADOW_VOLUME_PLANES;
    float side_enter = enter;
    float side_leave = leave;
    for (int slot = 0; slot < SHADOW_VOLUME_SIDE_SLOTS; ++slot)
    {
        vec4 side = scene.shadow_volumes[first + slot];
        clip_to_half_space(side_enter, side_leave, side.xyz, side.w, origin, ray);
    }
    if (side_leave <= side_enter)
        return 0.0;

    float front_enter = side_enter;
    float front_leave = side_leave;
    for (int slot = SHADOW_VOLUME_SIDE_SLOTS; slot < MAX_SHADOW_VOLUME_PLANES; ++slot)
    {
        vec4 back = scene.shadow_volumes[first + slot];
        clip_to_half_space(front_enter, front_leave, back.xyz, back.w, origin, ray);
    }
    return (side_leave - side_enter) - max(front_leave - front_enter, 0.0);
}

void main()
{
    vec2  position   = (gl_FragCoord.xy - scene.beam_viewport.xy) / scene.beam_viewport.zw;
    float stored     = texelFetch(scene_depth, ivec2(gl_FragCoord.xy), 0).r;
    float view_depth = 1.0 / ((1.0 - stored) * scene.beam_settings.y + scene.beam_settings.z);
    vec3  ray        = view_ray(position);
    vec3  origin     = scene.camera_position.xyz;
    float fill       = beam_fill_factor();

    vec3  premultiplied = vec3(0.0);
    float coverage      = 0.0;
    int   beam_count    = int(scene.beam_settings.x);
    int   volume_count  = int(scene.shadow_volume_settings.x);
    for (int index = 0; index < beam_count; ++index)
    {
        Beam  beam    = scene.beams[index];
        vec3  forward = beam.forward_tangent.xyz;
        float tangent = beam.forward_tangent.w;
        vec3  up      = beam.up.xyz;
        vec3  right   = beam.right.xyz;
        vec3  from_apex = origin - beam.apex_range.xyz;

        float enter = 0.0;
        float leave = view_depth;
        clip_to_half_space(enter, leave, up - tangent * forward, 0.0, from_apex, ray);
        clip_to_half_space(enter, leave, -up - tangent * forward, 0.0, from_apex, ray);
        clip_to_half_space(enter, leave, right - tangent * forward, 0.0, from_apex, ray);
        clip_to_half_space(enter, leave, -right - tangent * forward, 0.0, from_apex, ray);
        clip_to_half_space(enter, leave, forward, beam.apex_range.w, from_apex, ray);
        if (leave <= enter)
            continue;

        int   light_bits = floatBitsToInt(beam.color_light.w);
        float shadowed   = 0.0;
        for (int volume = 0; volume < volume_count; ++volume)
        {
            if (floatBitsToInt(scene.shadow_volume_lights[volume >> 2][volume & 3]) != light_bits)
                continue;
            shadowed += shadowed_length(volume, origin, ray, enter, leave);
        }
        float lit   = 1.0 - clamp(shadowed / (leave - enter), 0.0, 1.0);
        float alpha = clamp(scene.beam.y * fill * lit, 0.0, 1.0);

        premultiplied = premultiplied * (1.0 - alpha) + beam.color_light.rgb * alpha;
        coverage      = coverage * (1.0 - alpha) + alpha;
    }

    if (coverage <= 0.0)
    {
        outColor = vec4(0.0);
        return;
    }
    outColor = vec4(premultiplied / coverage, coverage);
}

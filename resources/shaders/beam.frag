#version 450

// The beam pass (spot_beam_plan.md ss3 to ss5): every beam of the pass, over the HDR scene after it is drawn.
// A pixel's line of sight is clipped to each beam's cone, its range and the surface under the pixel; the
// shadow volumes the beam's own light throws are subtracted from that chord, and the tint is the fill's
// one alpha scaled by the share of the chord still lit, so an unshadowed beam is the same flat wash
// whatever its depth and a caster's shaft dims it in proportion; where the chord ends inside a shaft
// nothing is drawn at all, so a shaft seen from behind its caster is an empty hole. Lines in the light's
// colour, r_beam_edge_pixels wide, are drawn where the chord ends on screen (the cone's silhouette and
// where it lands) and along every shaft (its sides from beside it, the hole's rim from behind).
// Then every shadow volume is drawn whole (shadow_volume_plan.md ss5): the collision's volume reaches
// past the beam (the cone's eight tangent planes and a flat far cap, not the cone and its sphere) and
// a point or directional light has no beam at all, so the volume is its own body: a dark wash of one
// fixed alpha over the line of sight's chord inside it, a black line where that chord ends on screen
// and a rim where it lands. The drawn body is only the part that lands on something: in the pyramid some
// touched piece spans from the light and behind no piece, so the body is a prism from the caster to the
// shadow on the wall, nothing shows beside the wall or behind it, and over a void there is no body.

layout(set = 0, binding = 0) uniform sampler2D scene_depth;

#include "scene.glsl"

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 outColor;

// A dot's radius as a share of the spacing, and the 45 degree turn a printed screentone sits at.
const float BEAM_DOT_RADIUS  = 0.18;
const float BEAM_DOT_ANGLE   = 0.7853981634;
const float PI               = 3.14159265;
const float EMPTY_ENTER      = 1e9;
const float EMPTY_LEAVE      = -1e9;
// Below this the cone quadratic's leading term is a line: the ray runs along the cone's surface.
const float FLAT_QUADRATIC   = 1e-6;
// A depth step larger than this share of the depth is a jump between two surfaces, not one surface's slope.
const float DEPTH_JUMP_SHARE = 0.1;
// The shadow terms stop this share of the depth short of the surface: a chord ending ON a caster's face
// ends on its own volume's boundary, where the depth's rounding would flicker the face in and out of the shaft.
const float SURFACE_BIAS_SHARE = 0.002;

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

// Keeps of [enter, leave] the part within `radius` of the point `from_centre` is measured from.
void clip_to_sphere(inout float enter, inout float leave, vec3 from_centre, vec3 ray, float radius)
{
    float a            = dot(ray, ray);
    float b            = 2.0 * dot(from_centre, ray);
    float c            = dot(from_centre, from_centre) - radius * radius;
    float discriminant = b * b - 4.0 * a * c;
    if (discriminant < 0.0)
    {
        enter = EMPTY_ENTER;
        leave = EMPTY_LEAVE;
        return;
    }
    float root = sqrt(discriminant);
    enter      = max(enter, (-b - root) / (2.0 * a));
    leave      = min(leave, (-b + root) / (2.0 * a));
}

// Keeps of [enter, leave] the part inside the cone about `forward` from the apex `from_apex` is measured
// from, whose half-angle has cosine `cosine`: dot(p, forward)^2 >= cosine^2 dot(p, p) on the apex's
// forward side. The quadratic this is in t holds between its roots when the ray points outside the
// cone's angle and outside them when it points inside, one piece per nappe either way.
void clip_to_cone(inout float enter, inout float leave, vec3 from_apex, vec3 ray, vec3 forward, float cosine)
{
    clip_to_half_space(enter, leave, -forward, 0.0, from_apex, ray);
    if (leave <= enter)
        return;

    float cosine_squared = cosine * cosine;
    float ray_along      = dot(ray, forward);
    float apex_along     = dot(from_apex, forward);
    float a              = ray_along * ray_along - cosine_squared * dot(ray, ray);
    float b              = 2.0 * (apex_along * ray_along - cosine_squared * dot(from_apex, ray));
    float c              = apex_along * apex_along - cosine_squared * dot(from_apex, from_apex);

    if (abs(a) < FLAT_QUADRATIC)
    {
        if (abs(b) < FLAT_QUADRATIC)
        {
            if (c < 0.0)
            {
                enter = EMPTY_ENTER;
                leave = EMPTY_LEAVE;
            }
            return;
        }
        float t = -c / b;
        if (b > 0.0)
            enter = max(enter, t);
        else
            leave = min(leave, t);
        return;
    }

    float discriminant = b * b - 4.0 * a * c;
    if (discriminant < 0.0)
    {
        if (a < 0.0)
        {
            enter = EMPTY_ENTER;
            leave = EMPTY_LEAVE;
        }
        return;
    }
    float root  = sqrt(discriminant);
    float first = (-b - root) / (2.0 * a);
    float last  = (-b + root) / (2.0 * a);
    if (a < 0.0)
    {
        enter = max(enter, min(first, last));
        leave = min(leave, max(first, last));
        return;
    }
    if (min(leave, first) > enter)
        leave = min(leave, first);
    else
        enter = max(enter, last);
}

// How much of [enter, leave] lies in shadow volume `volume`: inside every side plane and not still in
// front of the caster, which is inside every back plane (reveal.glsl's shadow_margin, as lengths).
float shadowed_length(int volume, vec3 origin, vec3 ray, float enter, float leave)
{
    int   first      = volume * MAX_SHADOW_VOLUME_PLANES;
    ivec2 counts     = shadow_volume_plane_counts(volume);
    float side_enter = enter;
    float side_leave = leave;
    for (int slot = 0; slot < counts.x; ++slot)
    {
        vec4 side = scene.shadow_volumes[first + slot];
        clip_to_half_space(side_enter, side_leave, side.xyz, side.w, origin, ray);
    }
    if (side_leave <= side_enter)
        return 0.0;

    float front_enter = side_enter;
    float front_leave = side_leave;
    for (int slot = SHADOW_VOLUME_SIDE_SLOTS; slot < SHADOW_VOLUME_SIDE_SLOTS + counts.y; ++slot)
    {
        vec4 back = scene.shadow_volumes[first + slot];
        clip_to_half_space(front_enter, front_leave, back.xyz, back.w, origin, ray);
    }
    return (side_leave - side_enter) - max(front_leave - front_enter, 0.0);
}

// The line of sight's length inside drawn volume `volume` that lands on a receiver: in the volume's shadow
// (inside its sides, not in front of its caster), on a ray from the light that hits some receiver the volume
// touches (inside that occluder's pyramid) and behind no piece at all (inside an occluder's pyramid and lit
// faces). Each of those is one interval of the line, and the answer is the length of their exact set algebra:
// every segment between two consecutive interval ends is tested at its middle, so a floor seen past a
// wall is drawn up to the wall and not between the wall and the floor. One walk gives both lengths
// wanted of it: x is the part in front of the surface at `surface_t`, y the whole line through the air.
// A line that misses the sphere around the body (its caster and receivers) has both zero and walks nothing.
const int MAX_DRAWN_INTERVALS = 2 + 2 * MAX_SHADOW_OCCLUDERS;

vec2 drawn_shadow_lengths(int volume, vec3 origin, vec3 ray, float surface_t)
{
    vec4  bounds        = scene.shadow_volume_bounds[volume];
    vec3  from_centre   = origin - bounds.xyz;
    float along         = dot(from_centre, ray);
    float line_distance = dot(from_centre, from_centre) - along * along / dot(ray, ray);
    if (line_distance >= bounds.w * bounds.w)
        return vec2(0.0);

    int   first      = volume * MAX_SHADOW_VOLUME_PLANES;
    ivec2 counts     = shadow_volume_plane_counts(volume);
    float side_enter = 0.0;
    float side_leave = EMPTY_ENTER;
    for (int slot = 0; slot < counts.x; ++slot)
    {
        vec4 side = scene.shadow_volumes[first + slot];
        clip_to_half_space(side_enter, side_leave, side.xyz, side.w, origin, ray);
    }
    if (side_leave <= side_enter)
        return vec2(0.0);
    float front_enter = side_enter;
    float front_leave = side_leave;
    for (int slot = SHADOW_VOLUME_SIDE_SLOTS; slot < SHADOW_VOLUME_SIDE_SLOTS + counts.y; ++slot)
    {
        vec4 back = scene.shadow_volumes[first + slot];
        clip_to_half_space(front_enter, front_leave, back.xyz, back.w, origin, ray);
    }

    // The ends: the side chord's, the caster-front chord's, then each landing's pyramid chord and behind chord.
    float ends[2 * MAX_DRAWN_INTERVALS];
    float pyramid_enter[MAX_SHADOW_OCCLUDERS];
    float pyramid_leave[MAX_SHADOW_OCCLUDERS];
    bool  receives[MAX_SHADOW_OCCLUDERS];
    float behind_enter[MAX_SHADOW_OCCLUDERS];
    float behind_leave[MAX_SHADOW_OCCLUDERS];
    int   landing_count = 0;
    int   end_count     = 0;
    ends[end_count++]   = side_enter;
    ends[end_count++]   = side_leave;
    if (front_leave > front_enter)
    {
        ends[end_count++] = front_enter;
        ends[end_count++] = front_leave;
    }
    int occluder_count = int(scene.shadow_volume_settings.w);
    for (int occluder = 0; occluder < occluder_count; ++occluder)
    {
        int bits = floatBitsToInt(scene.shadow_occluder_bits[occluder >> 2][occluder & 3]);
        if ((bits & (1 << volume)) == 0)
            continue;
        int   occluder_first  = occluder * MAX_SHADOW_VOLUME_PLANES;
        ivec2 occluder_counts = shadow_occluder_plane_counts(occluder);
        float p_enter         = side_enter;
        float p_leave         = side_leave;
        for (int slot = 0; slot < occluder_counts.x; ++slot)
        {
            vec4 plane = scene.shadow_occluders[occluder_first + slot];
            clip_to_half_space(p_enter, p_leave, plane.xyz, plane.w, origin, ray);
        }
        if (p_leave <= p_enter)
            continue;
        float b_enter = p_enter;
        float b_leave = p_leave;
        for (int slot = SHADOW_VOLUME_SIDE_SLOTS; slot < SHADOW_VOLUME_SIDE_SLOTS + occluder_counts.y; ++slot)
        {
            vec4 plane = scene.shadow_occluders[occluder_first + slot];
            clip_to_half_space(b_enter, b_leave, plane.xyz, plane.w, origin, ray);
        }
        pyramid_enter[landing_count] = p_enter;
        pyramid_leave[landing_count] = p_leave;
        receives[landing_count]      = (bits & SHADOW_OCCLUDER_RECEIVES) != 0;
        behind_enter[landing_count]  = b_enter;
        behind_leave[landing_count]  = b_leave;
        ends[end_count++]            = p_enter;
        ends[end_count++]            = p_leave;
        if (b_leave > b_enter)
        {
            ends[end_count++] = b_enter;
            ends[end_count++] = b_leave;
        }
        ++landing_count;
    }
    if (landing_count == 0)
        return vec2(0.0);

    vec2 drawn = vec2(0.0);
    for (int start = 0; start < end_count; ++start)
    {
        float from = ends[start];
        float to   = EMPTY_ENTER;
        for (int other = 0; other < end_count; ++other)
            if (ends[other] > from)
                to = min(to, ends[other]);
        if (to >= EMPTY_ENTER)
            continue;
        float middle = 0.5 * (from + to);
        if (middle < side_enter || middle > side_leave)
            continue;
        if (middle > front_enter && middle < front_leave)
            continue;
        bool lands  = false;
        bool behind = false;
        for (int landing = 0; landing < landing_count; ++landing)
        {
            lands  = lands || (receives[landing] && middle > pyramid_enter[landing] && middle < pyramid_leave[landing]);
            behind = behind || (middle > behind_enter[landing] && middle < behind_leave[landing]);
        }
        if (lands && !behind)
            drawn += vec2(max(min(to, surface_t) - from, 0.0), to - from);
    }
    return drawn;
}

// Signed distance of `point` to shadow volume `volume`'s boundary, negative inside: inside every side plane
// and behind the caster, which is outside at least one back plane.
float shadow_margin_at(int volume, vec3 point)
{
    int   first         = volume * MAX_SHADOW_VOLUME_PLANES;
    ivec2 counts        = shadow_volume_plane_counts(volume);
    float outside_sides = EMPTY_LEAVE;
    for (int slot = 0; slot < counts.x; ++slot)
    {
        vec4 side     = scene.shadow_volumes[first + slot];
        outside_sides = max(outside_sides, dot(side.xyz, point) - side.w);
    }
    float behind_caster = EMPTY_LEAVE;
    for (int slot = SHADOW_VOLUME_SIDE_SLOTS; slot < SHADOW_VOLUME_SIDE_SLOTS + counts.y; ++slot)
    {
        vec4 back     = scene.shadow_volumes[first + slot];
        behind_caster = max(behind_caster, dot(back.xyz, point) - back.w);
    }
    return max(outside_sides, -behind_caster);
}

// The line's coverage this many pixels inside an edge, r_beam_edge_pixels wide and feathered one pixel.
float outline_at(float pixels_from_edge)
{
    float width = scene.beam.w;
    return width > 0.0 ? 1.0 - smoothstep(width - 0.5, width + 0.5, pixels_from_edge) : 0.0;
}

// A continuous measure's distance to its zero in pixels, read from how fast it changes across the pixel.
float pixels_to_zero(float measure)
{
    return measure / max(fwidth(measure), 1e-6);
}

void main()
{
    vec2  position    = (gl_FragCoord.xy - scene.beam_viewport.xy) / scene.beam_viewport.zw;
    float stored      = texelFetch(scene_depth, ivec2(gl_FragCoord.xy), 0).r;
    float view_depth  = 1.0 / ((1.0 - stored) * scene.beam_settings.y + scene.beam_settings.z);
    vec3  ray         = view_ray(position);
    vec3  origin      = scene.camera_position.xyz;
    float fill        = beam_fill_factor();
    bool  one_surface = fwidth(view_depth) < DEPTH_JUMP_SHARE * view_depth;

    vec3  premultiplied = vec3(0.0);
    float coverage      = 0.0;
    int   beam_count    = int(scene.beam_settings.x);
    int   volume_count  = int(scene.shadow_volume_settings.x);
    for (int index = 0; index < beam_count; ++index)
    {
        Beam beam      = scene.beams[index];
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
        // before any pixel leaves the loop, which only uniforms steer until then.
        float line = outline_at(one_surface ? pixels_to_zero(chord) : pixels_to_zero(air_chord));

        // The shafts: each volume of this light takes a length out of the chord, zero at the shaft's own
        // silhouette, which draws its sides from beside it and the hole's rim from behind. The point the
        // chord ends at is in a shaft or not; its signed distance to the nearest shaft's boundary draws
        // the hole's rim on whatever the beam lands on.
        int   light_bits   = floatBitsToInt(beam.color_light.w);
        float short_of_surface = SURFACE_BIAS_SHARE * view_depth;
        float shadow_leave = leave - short_of_surface;
        float end_t        = chord > 0.0 ? shadow_leave : view_depth - short_of_surface;
        vec3  end_point    = origin + end_t * ray;
        bool  one_end    = fwidth(end_t) < DEPTH_JUMP_SHARE * end_t;
        float shadowed   = 0.0;
        float end_margin = EMPTY_ENTER;
        float shaft_line = 0.0;
        for (int volume = 0; volume < volume_count; ++volume)
        {
            if (floatBitsToInt(scene.shadow_volume_lights[volume >> 2][volume & 3]) != light_bits)
                continue;
            float in_shaft = max(shadowed_length(volume, origin, ray, enter, shadow_leave), 0.0);
            shadowed      += in_shaft;
            shaft_line     = max(shaft_line, in_shaft > 0.0 ? outline_at(pixels_to_zero(in_shaft)) : 0.0);
            end_margin     = min(end_margin, shadow_margin_at(volume, end_point));
        }
        float hole_line = end_margin > 0.0 && one_end ? outline_at(pixels_to_zero(end_margin)) : 0.0;

        if (chord <= 0.0)
            continue;

        float lit     = 1.0 - clamp(shadowed / chord, 0.0, 1.0);
        float arrives = end_margin < 0.0 ? 0.0 : 1.0;
        float wash    = scene.beam.y * fill * lit * arrives;
        float lines   = max(line * lit, max(shaft_line, hole_line));
        float alpha   = clamp(max(wash, lines), 0.0, 1.0);

        premultiplied = premultiplied * (1.0 - alpha) + beam.color_light.rgb * alpha;
        coverage      = coverage * (1.0 - alpha) + alpha;
    }

    if (scene.shadow_volume_settings.z > 0.0)
    {
        float short_of_surface = SURFACE_BIAS_SHARE * view_depth;
        float surface_t        = view_depth - short_of_surface;
        vec3  surface_point    = origin + surface_t * ray;
        for (int volume = 0; volume < volume_count; ++volume)
        {
            vec2  lengths      = drawn_shadow_lengths(volume, origin, ray, surface_t);
            float in_shadow    = lengths.x;
            float margin       = shadow_margin_at(volume, surface_point);
            float line_measure = one_surface ? in_shadow : lengths.y;
            float line_pixels  = pixels_to_zero(line_measure);
            float rim_pixels   = pixels_to_zero(margin);
            float line         = line_measure > 0.0 ? outline_at(line_pixels) : 0.0;
            float rim          = margin > 0.0 && one_surface ? outline_at(rim_pixels) : 0.0;
            float wash         = in_shadow > 0.0 ? scene.shadow_volume_settings.y : 0.0;
            float alpha      = clamp(max(wash, max(line, rim)), 0.0, 1.0);

            premultiplied = premultiplied * (1.0 - alpha);
            coverage      = coverage * (1.0 - alpha) + alpha;
        }
    }

    if (coverage <= 0.0)
    {
        outColor = vec4(0.0);
        return;
    }
    outColor = vec4(premultiplied / coverage, coverage);
}

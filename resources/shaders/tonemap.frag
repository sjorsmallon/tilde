#version 450

// lighting_def.md decision J: the scene renders HDR and linear, this pass
// applies exposure and the curve, and the sRGB attachment still owns the encode.

layout(set = 0, binding = 0) uniform sampler2D hdr_target;
layout(set = 0, binding = 1) uniform sampler2D scene_depth;
layout(set = 0, binding = 2) uniform sampler2D scene_normal;
layout(set = 0, binding = 3) uniform sampler3D fog_totals;

#include "surface_normal.glsl"

layout(push_constant) uniform Tonemap
{
    float exposure;
    float ink_strength;
    float ink_threshold;
    int   ink_width_pixels;
    float ink_crease_radians;
    float ink_tint;
    float rim_strength;
    int   rim_width_pixels;
    int   show_surface_normals;
    float ink_wobble_pixels;
    float ink_wobble_scale_pixels;
    float ink_boil_frame;
    float ink_weight_near;
    float ink_weight_slope;
    float ink_weight_offset;
    // The fogged pass's viewport in pixels; a width of 0 is no fog this frame.
    float fog_viewport_x;
    float fog_viewport_y;
    float fog_viewport_width;
    float fog_viewport_height;
    // The view depths the fog grid starts and ends at (scene.glsl's fog_settings).
    float fog_near;
    float fog_far;
    // One over a pixel's view depth is (1 - its stored depth) * slope + offset.
    float inverse_view_depth_slope;
    float inverse_view_depth_offset;
    float ink_on_black;
    float misprint_pixels;
    float misprint_distance;
} tonemap;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 fragment_color;

const vec3 RIM_COLOR          = vec3(1.0);
const vec3 INK_ON_BLACK_COLOR = vec3(1.0);

// The way the red plate slips; the blue slips the other way.
const vec2 MISPRINT_DIRECTION = vec2(0.8, 0.6);

// What three float depths near 1.0 can disagree by with no edge between them.
const float DEPTH_NOISE = 4.0 / 16777216.0;

// Khronos PBR Neutral, github.com/KhronosGroup/ToneMapping. In-gamut colour
// passes through unchanged; only what exceeds the display is compressed.
vec3 pbr_neutral_tonemap(vec3 color)
{
    const float start_compression = 0.8 - 0.04;
    const float desaturation      = 0.15;

    float darkest = min(color.r, min(color.g, color.b));
    float offset  = darkest < 0.08 ? darkest - 6.25 * darkest * darkest : 0.04;
    color -= offset;

    float peak = max(color.r, max(color.g, color.b));
    if (peak < start_compression)
        return color;

    const float d = 1.0 - start_compression;
    float new_peak = 1.0 - d * d / (peak + d - start_compression);
    color *= new_peak / peak;

    float g = 1.0 - 1.0 / (desaturation * (peak - new_peak) + 1.0);
    return mix(color, vec3(new_peak), g);
}

// x = how far the depth bends across `pixel` along `reach`, above zero where the pixel is nearer
// than the two either side of it, y = the nearest of the three depths.
vec2 depth_bend(ivec2 pixel, ivec2 reach, ivec2 last_pixel, float centre)
{
    ivec2 before = pixel - reach;
    ivec2 after  = pixel + reach;
    if (any(lessThan(before, ivec2(0))) || any(greaterThan(after, last_pixel)))
        return vec2(0.0, centre);

    float depth_before = texelFetch(scene_depth, before, 0).r;
    float depth_after  = texelFetch(scene_depth, after, 0).r;
    return vec2(depth_before + depth_after - 2.0 * centre,
                min(centre, min(depth_before, depth_after)));
}

// The angle between the surface normals `reach` either side of `pixel`, 0 where either side drew no surface.
float normal_turn(ivec2 pixel, ivec2 reach, ivec2 last_pixel)
{
    ivec2 before = pixel - reach;
    ivec2 after  = pixel + reach;
    if (any(lessThan(before, ivec2(0))) || any(greaterThan(after, last_pixel)))
        return 0.0;

    vec4 stored_before = texelFetch(scene_normal, before, 0);
    vec4 stored_after  = texelFetch(scene_normal, after, 0);
    if (!surface_was_drawn(stored_before) || !surface_was_drawn(stored_after))
        return 0.0;

    return acos(clamp(dot(stored_surface_normal(stored_before), stored_surface_normal(stored_after)),
                      -1.0, 1.0));
}

// A line where the surface turns by more than r_ink_crease_degrees between neighbouring pixels.
float crease_coverage(ivec2 pixel, int reach)
{
    ivec2 size = textureSize(scene_normal, 0);

    float turn = max(normal_turn(pixel, ivec2(reach, 0), size - 1),
                     normal_turn(pixel, ivec2(0, reach), size - 1));
    return smoothstep(tonemap.ink_crease_radians, tonemap.ink_crease_radians * 1.5, turn);
}

// Depth-buffer values are a straight line across a flat face, so the bend is
// zero there and non-zero at a crease or a silhouette.
// x = the bend where this pixel is the FAR side of a jump or the bottom of a hollow,
// y = the bend where it is the NEAR side or the top of a ridge, both in r_ink_threshold's units.
vec2 depth_edges(ivec2 pixel, int reach)
{
    ivec2 size   = textureSize(scene_depth, 0);
    float centre = texelFetch(scene_depth, pixel, 0).r;

    vec2 horizontal = depth_bend(pixel, ivec2(reach, 0), size - 1, centre);
    vec2 vertical   = depth_bend(pixel, ivec2(0, reach), size - 1, centre);

    vec2  bends   = vec2(-min(horizontal.x, vertical.x), max(horizontal.x, vertical.x));
    float nearest = min(horizontal.y, vertical.y);

    // Over the distance and the pixel size, so one threshold holds near and far and at any resolution.
    return max(bends - DEPTH_NOISE, 0.0) / max(1.0 - nearest, 1e-7) * float(size.y) / float(reach);
}

float depth_jump_coverage(float edge)
{
    return smoothstep(tonemap.ink_threshold, tonemap.ink_threshold * 2.0, edge);
}

float ink_coverage(ivec2 pixel, int reach)
{
    vec2 edges = depth_edges(pixel, reach);
    return max(depth_jump_coverage(max(edges.x, edges.y)), crease_coverage(pixel, reach));
}

// The outline's own depth jump, its near side alone.
float rim_coverage(ivec2 pixel, int reach)
{
    return depth_jump_coverage(depth_edges(pixel, reach).y);
}

vec2 wobble_hash(vec2 cell)
{
    vec3 mixed = fract(vec3(cell.xyx) * vec3(0.1031, 0.1030, 0.0973) + tonemap.ink_boil_frame * 0.7131);
    mixed += dot(mixed, mixed.yzx + 33.33);
    return fract((mixed.xx + mixed.yz) * mixed.zy) * 2.0 - 1.0;
}

// The pixel the lines are looked up around: this one, strayed by up to r_ink_wobble pixels along a
// waver r_ink_wobble_scale pixels long that r_ink_boil redraws.
ivec2 wobbled_pixel()
{
    if (tonemap.ink_wobble_pixels <= 0.0)
        return ivec2(gl_FragCoord.xy);

    vec2 point = gl_FragCoord.xy / max(tonemap.ink_wobble_scale_pixels, 1.0);
    vec2 cell  = floor(point);
    vec2 blend = smoothstep(0.0, 1.0, point - cell);
    vec2 stray = mix(mix(wobble_hash(cell), wobble_hash(cell + vec2(1.0, 0.0)), blend.x),
                     mix(wobble_hash(cell + vec2(0.0, 1.0)), wobble_hash(cell + vec2(1.0, 1.0)), blend.x),
                     blend.y);
    return clamp(ivec2(gl_FragCoord.xy + stray * tonemap.ink_wobble_pixels), ivec2(0),
                 textureSize(scene_depth, 0) - 1);
}

// How many times wider than its cvar a line is here: r_ink_weight_distance over the distance of the
// nearest surface a line of the widest reach could belong to, from 1 up to r_ink_weight_near.
float line_weight(ivec2 pixel)
{
    if (tonemap.ink_weight_near <= 1.0)
        return 1.0;

    ivec2 last_pixel = textureSize(scene_depth, 0) - 1;
    int   reach      = int(ceil(float(max(tonemap.ink_width_pixels, 1)) * tonemap.ink_weight_near));
    float nearest    = texelFetch(scene_depth, pixel, 0).r;
    for (int index = 0; index < 4; ++index)
    {
        ivec2 offset = index < 2 ? ivec2(index * 2 - 1, 0) : ivec2(0, index * 2 - 5);
        nearest = min(nearest, texelFetch(scene_depth, clamp(pixel + offset * reach, ivec2(0), last_pixel), 0).r);
    }
    return clamp((1.0 - nearest) * tonemap.ink_weight_slope + tonemap.ink_weight_offset, 1.0,
                 tonemap.ink_weight_near);
}

float view_depth_at(ivec2 pixel)
{
    float stored = texelFetch(scene_depth, pixel, 0).r;
    return 1.0 / ((1.0 - stored) * tonemap.inverse_view_depth_slope + tonemap.inverse_view_depth_offset);
}

// The scene's light at this pixel. Print misregistration: its red and blue are read r_misprint pixels
// to either side at r_misprint_distance and beyond, less the nearer the surface is.
vec3 scene_light()
{
    vec3 light = texture(hdr_target, in_uv).rgb;
    if (tonemap.misprint_pixels > 0.0)
    {
        float share  = clamp(view_depth_at(ivec2(gl_FragCoord.xy)) / tonemap.misprint_distance, 0.0, 1.0);
        vec2  offset = MISPRINT_DIRECTION * (tonemap.misprint_pixels * share) / vec2(textureSize(hdr_target, 0));
        light.r = texture(hdr_target, in_uv + offset).r;
        light.b = texture(hdr_target, in_uv - offset).b;
    }
    return max(light, vec3(0.0));
}

// How solid the ink (r_cel_black) is at `pixel` AND `reach` to each side of it: a line there is
// drawn in INK_ON_BLACK_COLOR, and one along the edge of the black stays ink.
float solid_ink_around(ivec2 pixel, int reach)
{
    ivec2 last_pixel = textureSize(scene_normal, 0) - 1;
    float solid      = stored_solid_ink(texelFetch(scene_normal, pixel, 0));
    for (int index = 0; index < 4; ++index)
    {
        ivec2 offset = index < 2 ? ivec2(index * 2 - 1, 0) : ivec2(0, index * 2 - 5);
        solid = min(solid, stored_solid_ink(texelFetch(scene_normal, clamp(pixel + offset * reach, ivec2(0), last_pixel), 0)));
    }
    return solid;
}

// The fog between the eye and this pixel's surface: the light it adds (rgb) and how much of the surface shows through (a),
// read from the fog grid's running totals (fog_totals.comp) at the surface's depth.
vec4 fog_in_front()
{
    vec2 position = (gl_FragCoord.xy - vec2(tonemap.fog_viewport_x, tonemap.fog_viewport_y)) /
                    max(vec2(tonemap.fog_viewport_width, tonemap.fog_viewport_height), vec2(1.0));
    if (tonemap.fog_viewport_width <= 0.0 || any(lessThan(position, vec2(0.0))) || any(greaterThan(position, vec2(1.0))))
        return vec4(0.0, 0.0, 0.0, 1.0);

    float view_depth = view_depth_at(ivec2(gl_FragCoord.xy));

    // A total is its slice's FAR side, so the surface reads half a slice back from its own place in the grid.
    float slice_count = float(textureSize(fog_totals, 0).z);
    float slice       = log(view_depth / tonemap.fog_near) / log(tonemap.fog_far / tonemap.fog_near) * slice_count;
    vec4  fog         = texture(fog_totals, vec3(position, (slice - 0.5) / slice_count));
    return mix(vec4(0.0, 0.0, 0.0, 1.0), fog, clamp(slice, 0.0, 1.0));
}

void main()
{
    if (tonemap.show_surface_normals != 0)
    {
        fragment_color = vec4(texelFetch(scene_normal, ivec2(gl_FragCoord.xy), 0).rgb, 1.0);
        return;
    }

    vec4 fog     = fog_in_front();
    vec3 hdr     = (scene_light() * fog.a + fog.rgb) * tonemap.exposure;
    vec3 surface = pbr_neutral_tonemap(hdr);
    vec3 color   = surface;

    ivec2 pixel  = wobbled_pixel();
    float weight = tonemap.rim_strength > 0.0 || tonemap.ink_strength > 0.0 ? line_weight(pixel) : 1.0;

    // A width between two whole reaches is the two lines blended, so a line thickens without a step.
    if (tonemap.rim_strength > 0.0)
    {
        float reach    = float(max(tonemap.rim_width_pixels, 1)) * weight;
        float coverage = rim_coverage(pixel, int(reach));
        if (fract(reach) > 0.0)
            coverage = mix(coverage, rim_coverage(pixel, int(reach) + 1), fract(reach));
        color = mix(color, RIM_COLOR, coverage * tonemap.rim_strength);
    }

    if (tonemap.ink_strength > 0.0)
    {
        float reach    = float(max(tonemap.ink_width_pixels, 1)) * weight;
        float coverage = ink_coverage(pixel, int(reach));
        if (fract(reach) > 0.0)
            coverage = mix(coverage, ink_coverage(pixel, int(reach) + 1), fract(reach));

        vec3 ink = surface * tonemap.ink_tint;
        if (tonemap.ink_on_black > 0.0 && coverage > 0.0)
        {
            int around = int(ceil(reach + tonemap.ink_wobble_pixels)) + 1;
            ink = mix(ink, INK_ON_BLACK_COLOR,
                      solid_ink_around(ivec2(gl_FragCoord.xy), around) * tonemap.ink_on_black);
        }
        color = mix(color, ink, coverage * tonemap.ink_strength);
    }

    fragment_color = vec4(color, 1.0);
}

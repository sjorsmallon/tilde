#version 450

// lighting_def.md decision J: the scene renders HDR and linear, this pass
// applies exposure and the curve, and the sRGB attachment still owns the encode.

layout(set = 0, binding = 0) uniform sampler2D hdr_target;
layout(set = 0, binding = 1) uniform sampler2D scene_depth;
layout(set = 0, binding = 2) uniform sampler2D scene_normal;

#include "surface_normal.glsl"

layout(push_constant) uniform Tonemap
{
    float exposure;
    float ink_strength;
    float ink_threshold;
    int   ink_width_pixels;
    float ink_crease_radians;
    int   show_surface_normals;
} tonemap;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 fragment_color;

const vec3 INK_COLOR = vec3(0.0);

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

// x = how far the depth bends across `pixel` along `reach`, y = the nearest of the three depths.
vec2 depth_bend(ivec2 pixel, ivec2 reach, ivec2 last_pixel, float centre)
{
    ivec2 before = pixel - reach;
    ivec2 after  = pixel + reach;
    if (any(lessThan(before, ivec2(0))) || any(greaterThan(after, last_pixel)))
        return vec2(0.0, centre);

    float depth_before = texelFetch(scene_depth, before, 0).r;
    float depth_after  = texelFetch(scene_depth, after, 0).r;
    return vec2(abs(depth_before + depth_after - 2.0 * centre),
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
float crease_coverage()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    ivec2 size  = textureSize(scene_normal, 0);
    int   reach = max(tonemap.ink_width_pixels, 1);

    float turn = max(normal_turn(pixel, ivec2(reach, 0), size - 1),
                     normal_turn(pixel, ivec2(0, reach), size - 1));
    return smoothstep(tonemap.ink_crease_radians, tonemap.ink_crease_radians * 1.5, turn);
}

// Depth-buffer values are a straight line across a flat face, so the bend is
// zero there and non-zero at a crease or a silhouette.
float outline_coverage()
{
    ivec2 pixel      = ivec2(gl_FragCoord.xy);
    ivec2 size       = textureSize(scene_depth, 0);
    int   reach      = max(tonemap.ink_width_pixels, 1);
    float centre     = texelFetch(scene_depth, pixel, 0).r;

    vec2 horizontal = depth_bend(pixel, ivec2(reach, 0), size - 1, centre);
    vec2 vertical   = depth_bend(pixel, ivec2(0, reach), size - 1, centre);

    float bend    = max(max(horizontal.x, vertical.x) - DEPTH_NOISE, 0.0);
    float nearest = min(horizontal.y, vertical.y);

    // Over the distance and the pixel size, so one threshold holds near and far and at any resolution.
    float edge = bend / max(1.0 - nearest, 1e-7) * float(size.y) / float(reach);
    return smoothstep(tonemap.ink_threshold, tonemap.ink_threshold * 2.0, edge);
}

void main()
{
    if (tonemap.show_surface_normals != 0)
    {
        fragment_color = vec4(texelFetch(scene_normal, ivec2(gl_FragCoord.xy), 0).rgb, 1.0);
        return;
    }

    vec3 hdr   = max(texture(hdr_target, in_uv).rgb, vec3(0.0)) * tonemap.exposure;
    vec3 color = pbr_neutral_tonemap(hdr);

    if (tonemap.ink_strength > 0.0)
        color = mix(color, INK_COLOR,
                    max(outline_coverage(), crease_coverage()) * tonemap.ink_strength);

    fragment_color = vec4(color, 1.0);
}

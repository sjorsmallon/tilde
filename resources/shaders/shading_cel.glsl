#ifndef SHADING_CEL_GLSL
#define SHADING_CEL_GLSL

#ifndef PI
#define PI 3.14159265359
#endif

#include "scene.glsl"
#include "surface.glsl"

// Every number is a cvar, so it is tuned from the console and a map can carry its own.
float cel_terminator()  { return scene.look.y; }          // r_cel_terminator
float cel_shadow_edge() { return scene.look.z; }          // r_cel_shadow_edge
float cel_softness()    { return scene.look.w; }          // r_cel_softness
vec3  cel_shadow_tint() { return scene.cel_shadow_tint.rgb; } // r_cel_shadow_red, _green, _blue

float cel_band(float value, float edge)
{
    return smoothstep(edge - cel_softness(), edge + cel_softness(), value);
}

// Lit or not, never in between: flat brightness off the geometric normal, no highlight.
//
// NOT BUILT, a hard highlight: take pbr_lighting.glsl's GGX specular, threshold it to a
// solid dot, and add it only where surface.roughness is low, tinted by
// mix(vec3(0.04), albedo, metallic). It needs V passed in here, and surface.normal
// kept normal-mapped for it. Left out because a flat face shows either no dot or a whole-face one.
vec3 shade_light_cel(Surface surface, Incoming_Light light)
{
    float facing   = dot(surface.geometric_normal, light.direction);
    float strength = max(light.visibility.r, max(light.visibility.g, light.visibility.b));
    vec3  tint     = strength > 0.0 ? light.visibility / strength : vec3(0.0);

    float lit = cel_band(facing, cel_terminator()) * cel_band(strength, cel_shadow_edge());

    return surface.albedo * light.radiance * tint * (light.attenuation * lit) / PI;
}

// The shadow side: the baked light and the floor, tinted.
vec3 shade_ambient_cel(Surface surface, vec3 baked_irradiance, vec3 ambient_floor)
{
    return surface.albedo * (baked_irradiance + ambient_floor) * cel_shadow_tint();
}

float cel_hatch_strength() { return scene.cel_hatch.x; } // r_cel_hatch
float cel_hatch_spacing()  { return scene.cel_hatch.y; } // r_cel_hatch_spacing, pixels
float cel_hatch_edge()     { return scene.cel_hatch.z; } // r_cel_hatch_edge
float cel_hatch_width()    { return scene.cel_hatch.w; } // r_cel_hatch_width, pixels

const vec3 CEL_HATCH_COLOR = vec3(0.0);

// How far along the hatching's direction a point is, in world units: pinned to
// the world on the face's dominant axis, so it does not slide when the camera moves.
float hatch_coordinate(vec3 world_position, vec3 N)
{
    vec3 facing = abs(N);
    vec2 plane  = facing.x >= facing.y && facing.x >= facing.z ? world_position.yz
                  : facing.y >= facing.z                       ? world_position.xz
                                                               : world_position.xy;
    return (plane.x + plane.y) * 0.70710678;
}

float hatch_line(float cell, float pixels_per_cell)
{
    float pixels_to_line = abs(fract(cell - 0.5) - 0.5) * pixels_per_cell;
    float half_width     = cel_hatch_width() * 0.5;
    return 1.0 - smoothstep(half_width - 0.5, half_width + 0.5, pixels_to_line);
}

// Lines a fixed distance apart ON SCREEN: the spacing doubles as a face recedes,
// every other line fading out as it does, so nothing pops.
float hatch_coverage(vec3 world_position, vec3 N)
{
    float coordinate      = hatch_coordinate(world_position, N);
    float world_per_pixel = max(fwidth(coordinate), 1e-6);

    float level   = log2(world_per_pixel * cel_hatch_spacing());
    float octave  = floor(level);
    float fade    = level - octave;
    float spacing = exp2(octave);

    float fine   = hatch_line(coordinate / spacing, spacing / world_per_pixel);
    float coarse = hatch_line(coordinate / (2.0 * spacing), 2.0 * spacing / world_per_pixel);
    return max(coarse, fine * (1.0 - fade));
}

float cel_speckle_strength() { return scene.cel_speckle.x; } // r_cel_speckle
float cel_speckle_spacing()  { return scene.cel_speckle.y; } // r_cel_speckle_spacing, pixels
float cel_speckle_density()  { return scene.cel_speckle.z; } // r_cel_speckle_density
float cel_speckle_radius()   { return scene.cel_speckle.w; } // r_cel_speckle_radius, of a cell

const vec3 CEL_SPECKLE_COLOR = vec3(0.0);

vec2 speckle_plane(vec3 world_position, vec3 N)
{
    vec3 facing = abs(N);
    return facing.x >= facing.y && facing.x >= facing.z ? world_position.yz
           : facing.y >= facing.z                       ? world_position.xz
                                                        : world_position.xy;
}

vec2 speckle_hash(vec2 cell_id)
{
    vec3 p = fract(vec3(cell_id.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yzx + 33.33);
    return fract((p.xx + p.yz) * p.zy);
}

// At most one dot per cell, at a random spot, present in r_cel_speckle_density of the cells.
float speckle_dot(vec2 plane, float cell_size, float pixels_per_cell)
{
    vec2  cell_id = floor(plane / cell_size);
    float present = step(speckle_hash(cell_id + 31.0).x, cel_speckle_density());
    vec2  centre  = cell_id + mix(vec2(cel_speckle_radius()), vec2(1.0 - cel_speckle_radius()),
                                  speckle_hash(cell_id + 17.0));
    float pixels_to_edge = (length(plane / cell_size - centre) - cel_speckle_radius()) * pixels_per_cell;
    return present * (1.0 - smoothstep(-0.5, 0.5, pixels_to_edge));
}

// Cells a fixed size ON SCREEN, pinned to the world like the hatching: the cell
// doubles as a face recedes, the finer set fading out as it does.
float speckle_coverage(vec3 world_position, vec3 N)
{
    vec2  plane           = speckle_plane(world_position, N);
    float world_per_pixel = max(max(fwidth(plane.x), fwidth(plane.y)), 1e-6);

    float level     = log2(world_per_pixel * cel_speckle_spacing());
    float octave    = floor(level);
    float fade      = level - octave;
    float cell_size = exp2(octave);

    float fine   = speckle_dot(plane, cell_size, cell_size / world_per_pixel);
    float coarse = speckle_dot(plane, 2.0 * cell_size, 2.0 * cell_size / world_per_pixel);
    return max(coarse, fine * (1.0 - fade));
}

float luminance(vec3 color)
{
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

// `direct` and `ambient` are the light alone, shaded against a white surface.
// Hatched where the direct light adds less than r_cel_hatch_edge times the ambient.
vec3 compose_cel(Surface surface, vec3 direct, vec3 ambient, vec3 world_position)
{
    vec3 albedo = surface.albedo;
    if (cel_speckle_strength() > 0.0)
        albedo = mix(albedo, CEL_SPECKLE_COLOR,
                     speckle_coverage(world_position, surface.geometric_normal) * cel_speckle_strength());

    vec3 color = albedo * (direct + ambient);
    if (cel_hatch_strength() <= 0.0)
        return color;

    float direct_share = luminance(direct) / max(luminance(ambient), 0.0001);
    float in_shadow    = 1.0 - cel_band(direct_share, cel_hatch_edge());
    float hatch        = hatch_coverage(world_position, surface.geometric_normal) * in_shadow *
                         cel_hatch_strength();
    return mix(color, CEL_HATCH_COLOR, hatch);
}

#endif // SHADING_CEL_GLSL

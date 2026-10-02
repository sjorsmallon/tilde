#ifndef SHADING_CEL_GLSL
#define SHADING_CEL_GLSL

#ifndef PI
#define PI 3.14159265359
#endif

#include "scene.glsl"
#include "surface.glsl"
#include "dither3d.glsl"

// Every number is a cvar, so it is tuned from the console and a map can carry its own.
float cel_terminator()  { return scene.look.y; }          // r_cel_terminator
float cel_shadow_edge() { return scene.look.z; }          // r_cel_shadow_edge
float cel_softness()    { return scene.look.w; }          // r_cel_softness
vec3  cel_shadow_tint() { return scene.cel_shadow_tint.rgb; } // r_cel_shadow_red, _green, _blue

float cel_band(float value, float edge)
{
    return smoothstep(edge - cel_softness(), edge + cel_softness(), value);
}

float luminance(vec3 color)
{
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

// Lit or not, never in between: flat brightness off the geometric normal, no highlight.
// rgb is the light; a is its luminance times how squarely it arrives, which compose_cel
// turns into the lit side's tone.
//
// NOT BUILT, a hard highlight: take pbr_lighting.glsl's GGX specular, threshold it to a
// solid dot, and add it only where surface.roughness is low, tinted by
// mix(vec3(0.04), albedo, metallic). It needs V passed in here, and surface.normal
// kept normal-mapped for it. Left out because a flat face shows either no dot or a whole-face one.
vec4 shade_light_cel(Surface surface, Incoming_Light light)
{
    float facing   = dot(surface.geometric_normal, light.direction);
    float strength = max(light.visibility.r, max(light.visibility.g, light.visibility.b));
    vec3  tint     = strength > 0.0 ? light.visibility / strength : vec3(0.0);

    float lit = cel_band(facing, cel_terminator()) * cel_band(strength, cel_shadow_edge());

    vec3 color = surface.albedo * light.radiance * tint * (light.attenuation * lit) / PI;
    return vec4(color, luminance(color) * max(facing, 0.0));
}

float cel_bands() { return scene.cel_shadow_tint.a; } // r_cel_bands

// Light in flat steps a fixed ratio apart, r_cel_bands of them per doubling, its hue kept. compose_cel bands the total.
vec3 band_light(vec3 light)
{
    float bands = cel_bands();
    float level = luminance(light);
    if (bands <= 0.0 || level <= 1e-6)
        return light;

    float position = log2(level) * bands;
    float banded   = floor(position) + cel_band(fract(position), 0.5);
    return light * (exp2(banded / bands) / level);
}

// The shadow side: the baked light and the floor, tinted.
vec3 shade_ambient_cel(Surface surface, vec3 baked_irradiance, vec3 ambient_floor)
{
    return surface.albedo * (baked_irradiance + ambient_floor) * cel_shadow_tint();
}

int   cel_fill_pattern()  { return int(scene.cel_fill_pattern.x); } // r_cel_fill, one of CEL_FILL_*
float cel_fill_strength() { return scene.cel_fill.x; }              // r_cel_fill_strength
float cel_fill_spacing()  { return scene.cel_fill.y; }              // r_cel_fill_spacing, pixels
float cel_fill_edge()     { return scene.cel_fill.z; }              // r_cel_fill_edge
float cel_fill_tone()     { return scene.cel_fill_pattern.y; }      // r_cel_fill_tone
float cel_hatch_width()   { return scene.cel_fill.w; }              // r_cel_hatch_width, pixels

float cel_fill_tone_light()    { return scene.cel_fill_tone_range.x; } // r_cel_fill_tone_light
float cel_fill_ambient_dark()  { return scene.cel_fill_tone_range.y; } // r_cel_fill_ambient_dark
float cel_fill_ambient_light() { return scene.cel_fill_tone_range.z; } // r_cel_fill_ambient_light
float cel_fill_tone_lit()      { return scene.cel_fill_tone_range.w; } // r_cel_fill_tone_lit
float cel_fill_material()      { return scene.cel_fill_pattern.z; }    // r_cel_fill_material

const vec3 CEL_FILL_COLOR = vec3(0.0);

// The two world axes a face lies along most: what pins a pattern to the world,
// so it does not slide when the camera moves.
vec2 face_plane(vec3 value, vec3 N)
{
    vec3 facing = abs(N);
    return facing.x >= facing.y && facing.x >= facing.z ? value.yz
           : facing.y >= facing.z                       ? value.xz
                                                        : value.xy;
}

float hatch_line(float cell, float pixels_per_cell)
{
    float pixels_to_line = abs(fract(cell - 0.5) - 0.5) * pixels_per_cell;
    float half_width     = cel_hatch_width() * 0.5;
    return 1.0 - smoothstep(half_width - 0.5, half_width + 0.5, pixels_to_line);
}

// Lines a fixed distance apart ON SCREEN: the spacing doubles as a face recedes,
// every other line fading out as it does, so nothing pops.
float hatch_coverage(vec2 plane)
{
    float coordinate      = (plane.x + plane.y) * 0.70710678;
    float world_per_pixel = max(fwidth(coordinate), 1e-6);

    float level   = log2(world_per_pixel * cel_fill_spacing());
    float octave  = floor(level);
    float fade    = level - octave;
    float spacing = exp2(octave);

    float fine   = hatch_line(coordinate / spacing, spacing / world_per_pixel);
    float coarse = hatch_line(coordinate / (2.0 * spacing), 2.0 * spacing / world_per_pixel);
    return max(coarse, fine * (1.0 - fade));
}

// What one pixel covers of the face, in world units along face_plane's axes.
struct Pixel_Footprint
{
    vec2  across;          // the step to the pixel on the right
    vec2  down;            // the step to the pixel below
    float world_per_pixel; // the mean of its long and its short side
    float squash;          // how many times longer its long side is than its short one
};

Pixel_Footprint pixel_footprint(vec3 world_position, vec3 N)
{
    Pixel_Footprint footprint;
    footprint.across = face_plane(dFdx(world_position), N);
    footprint.down   = face_plane(dFdy(world_position), N);

    float area    = abs(footprint.across.x * footprint.down.y - footprint.across.y * footprint.down.x);
    float squares = dot(footprint.across, footprint.across) + dot(footprint.down, footprint.down);
    float spread  = sqrt(max(squares * squares - 4.0 * area * area, 0.0));

    footprint.world_per_pixel = max(sqrt(area), 1e-6);
    footprint.squash          = 0.5 * (squares + spread) / max(area, 1e-12);
    return footprint;
}

// A lattice finer than 2^-10 or coarser than 2^16 world units is never asked for.
const int LATTICE_FINEST_OCTAVE   = -10;
const int LATTICE_COARSEST_OCTAVE = 16;

// On a face seen this edge-on a dot is a sliver thinner than a pixel, so the pattern gives way to its mean.
const float LATTICE_SQUASH_FADE_START = 4.0;
const float LATTICE_SQUASH_FADE_END   = 8.0;

float lattice_visibility(Pixel_Footprint footprint)
{
    return 1.0 - smoothstep(LATTICE_SQUASH_FADE_START, LATTICE_SQUASH_FADE_END, footprint.squash);
}

uint lattice_hash(ivec2 id)
{
    uint hash = uint(id.x) * 0x8da6b343u + uint(id.y) * 0xd8163841u;
    hash ^= hash >> 15;
    hash *= 0x2c1b3c6du;
    hash ^= hash >> 12;
    hash *= 0x297a2d39u;
    hash ^= hash >> 15;
    return hash;
}

// Where a lattice point comes in the Bayer order of its 8x8 tile, 0 to 63. The first 16
// are the points of the lattice twice as coarse, in that lattice's own order.
int bayer_rank(ivec2 point)
{
    int rank = 0;
    for (int bit = 0; bit < 3; ++bit)
    {
        ivec2 parity = (point >> bit) & 1;
        rank = rank * 4 + 2 * (parity.x ^ parity.y) + parity.x;
    }
    return rank;
}

// Dither3D's nesting (runevision, surface-stable fractal dithering): dots a fixed size and
// distance apart ON SCREEN that stay pinned to the world. The lattice of twice the spacing
// is a subset of this one, so as a face comes closer dots are only ever ADDED between the
// ones already there, one Bayer rank at a time, and none of them moves or fades.
// `radius` and `stray` are fractions of the spacing and sum to at most 0.5, which is what
// keeps a dot within reach of the four lattice points around the pixel.
float nested_dot_coverage(vec2 plane, Pixel_Footprint footprint, float spacing_pixels, float radius,
                          float stray, float kept_share)
{
    float level   = clamp(log2(footprint.world_per_pixel * spacing_pixels), float(LATTICE_FINEST_OCTAVE),
                          float(LATTICE_COARSEST_OCTAVE));
    float octave  = floor(level);
    float cell    = exp2(octave);
    float spacing = exp2(level);

    // The share of this lattice's own points showing, the coarser lattice's quarter
    // excluded, picked so the dots per screen area stay the same across the octave.
    float new_share = (exp2(2.0 * (1.0 - (level - octave))) - 1.0) / 3.0;

    ivec2 corner   = ivec2(floor(plane / cell));
    int   id_scale = 1 << (int(octave) - LATTICE_FINEST_OCTAVE);

    float coverage = 0.0;
    for (int index = 0; index < 4; ++index)
    {
        ivec2 point = corner + ivec2(index & 1, index >> 1);
        uint  hash  = lattice_hash(point * id_scale);
        vec2  nudge = vec2(hash & 1023u, (hash >> 10) & 1023u) / 1023.0 * 2.0 - 1.0;
        float kept  = float(hash >> 20) / 4096.0 < kept_share ? 1.0 : 0.0;
        float shown = clamp(new_share * 48.0 - float(bayer_rank(point) - 16), 0.0, 1.0);

        vec2  from_centre     = plane - (vec2(point) * cell + nudge * (stray * spacing));
        float centre_distance = length(from_centre);
        vec2  outward         = from_centre / max(centre_distance, 1e-9);
        float world_per_pixel = length(vec2(dot(footprint.across, outward), dot(footprint.down, outward)));
        float pixels_to_edge  = (centre_distance - radius * spacing) / max(world_per_pixel, 1e-9);

        coverage = max(coverage, kept * shown * (1.0 - smoothstep(-0.5, 0.5, pixels_to_edge)));
    }
    return coverage;
}

float cel_speckle_strength() { return scene.cel_speckle.x; } // r_cel_speckle
float cel_speckle_spacing()  { return scene.cel_speckle.y; } // r_cel_speckle_spacing, pixels
float cel_speckle_density()  { return scene.cel_speckle.z; } // r_cel_speckle_density
float cel_speckle_radius()   { return scene.cel_speckle.w; } // r_cel_speckle_radius, of the spacing

const vec3 CEL_SPECKLE_COLOR = vec3(0.0);

// Sand: r_cel_speckle_density of the lattice points hold a dot, each strayed from its point.
float speckle_coverage(vec2 plane, Pixel_Footprint footprint)
{
    float radius = cel_speckle_radius();
    return nested_dot_coverage(plane, footprint, cel_speckle_spacing(), radius, 0.5 - radius,
                               cel_speckle_density()) *
           lattice_visibility(footprint);
}

// Discs on a square lattice touch at pi / 4 of the face; a darker tone than this is not a dot pattern.
const float DITHER_DARKEST_TONE = 0.75;

// Ink dots of ONE size covering `tone` of the face: a lighter tone spreads them further
// apart, by the same nesting that keeps them apart on screen. At r_cel_fill_tone they
// are r_cel_fill_spacing pixels apart.
float dither_coverage(vec2 plane, Pixel_Footprint footprint, float tone)
{
    tone = min(tone, DITHER_DARKEST_TONE);
    if (tone <= 0.001)
        return 0.0;

    float full_tone = clamp(cel_fill_tone(), 0.001, DITHER_DARKEST_TONE);
    float spacing   = cel_fill_spacing() * sqrt(full_tone / tone);
    float dots      = nested_dot_coverage(plane, footprint, spacing, sqrt(tone / PI), 0.0, 1.0);
    return mix(tone, dots, lattice_visibility(footprint));
}

// runevision's own shader and 3D texture (dither3d.glsl), fed the same tone. At half tone its dots are
// r_cel_fill_spacing pixels apart.
float dither3d_original_coverage(vec2 plane, Pixel_Footprint footprint, float tone)
{
    if (tone <= 0.001)
        return 0.0;

    Dither3d_Settings settings;
    settings.scale              = log2(8.0 * cel_fill_spacing());
    settings.size_variability   = scene.cel_dither3d.x; // r_cel_dither3d_size_variability
    settings.contrast           = scene.cel_dither3d.y; // r_cel_dither3d_contrast
    settings.stretch_smoothness = scene.cel_dither3d.z; // r_cel_dither3d_stretch_smoothness
    return dither3d(plane, footprint.across, footprint.down, tone, settings);
}

// Tone as dot DENSITY (Return of the Obra Dinn): the dimmer a shadow's ambient light, the more of it the dots cover.
float shadow_tone(vec3 ambient)
{
    float lightness = clamp((luminance(ambient) - cel_fill_ambient_dark()) /
                                (cel_fill_ambient_light() - cel_fill_ambient_dark()),
                            0.0, 1.0);
    return mix(cel_fill_tone(), cel_fill_tone_light(), lightness);
}

// How far the material's own maps put this spot from a flat, open surface: 1 in a crack its
// occlusion map closes or where its normal map leans a right angle off the face, 0 with neither map.
float material_relief(Surface surface)
{
    float lean = length(cross(surface.normal, surface.geometric_normal));
    return clamp(max(1.0 - surface.occlusion, lean), 0.0, 1.0);
}

// `direct` and `ambient` are the light alone, shaded against a white surface; direct.a is
// shade_light_cel's. Shadow is where the direct light adds less than r_cel_fill_edge times the
// ambient. The dithers fill it by shadow_tone, the lit side by how far its light is from
// arriving square-on, and both by the material's relief; the hatch fills the shadow alone.
vec3 compose_cel(Surface surface, vec4 direct, vec3 ambient, vec3 world_position)
{
    vec2            plane     = face_plane(world_position, surface.geometric_normal);
    Pixel_Footprint footprint = pixel_footprint(world_position, surface.geometric_normal);

    vec3 albedo = surface.albedo;
    if (cel_speckle_strength() > 0.0)
        albedo = mix(albedo, CEL_SPECKLE_COLOR, speckle_coverage(plane, footprint) * cel_speckle_strength());

    // Direct and ambient are banded TOGETHER, so a light's own falloff steps with the shadow it fades into.
    vec3 color = albedo * band_light(direct.rgb + ambient);
    if (cel_fill_pattern() == CEL_FILL_NONE)
        return color;

    float direct_luminance = luminance(direct.rgb);
    float in_shadow        = 1.0 - cel_band(direct_luminance / max(luminance(ambient), 0.0001), cel_fill_edge());

    float facing = clamp(direct.a / max(direct_luminance, 0.0001), 0.0, 1.0);
    float tone   = in_shadow * shadow_tone(band_light(ambient)) +
                   (1.0 - in_shadow) * (1.0 - facing) * cel_fill_tone_lit() +
                   material_relief(surface) * cel_fill_material();
    tone         = min(tone, DITHER_DARKEST_TONE);

    float ink;
    if (cel_fill_pattern() == CEL_FILL_HATCH)
        ink = hatch_coverage(plane) * in_shadow;
    else if (cel_fill_pattern() == CEL_FILL_DITHER3D_ORIGINAL)
        ink = dither3d_original_coverage(plane, footprint, tone);
    else
        ink = dither_coverage(plane, footprint, tone);
    return mix(color, CEL_FILL_COLOR, ink * cel_fill_strength());
}

#endif // SHADING_CEL_GLSL

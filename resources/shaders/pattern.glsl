#ifndef PATTERN_GLSL
#define PATTERN_GLSL

#ifndef PI
#define PI 3.14159265359
#endif

#include "scene.glsl"

// Pattern.kind -- renderer.cpp's get_pattern_shader_kind_for_pattern_kind.
#define PATTERN_NONE     0
#define PATTERN_STRIPES  1
#define PATTERN_GRID     2
#define PATTERN_CHECKS   3
#define PATTERN_BRICKS   4
#define PATTERN_CHEVRONS 5
#define PATTERN_DOTS     6

// face_uv_at's default scale: on a face nobody rescaled, uv times this is world units.
const float PATTERN_WORLD_UNITS_PER_UV = 128.0;

// Stripes repeat, chevrons point and a scroll travels ALONG (x); brick courses run along it.
struct Pattern {
    int   kind;
    vec2  spacing;         // world units per repeat, along and across
    float angle;           // radians
    float scroll_speed;    // world units per second, along
    float coverage;        // how much of a repeat is ink, 0 to 1
    float shape_parameter; // the one number only this kind reads
    vec3  ink_color;
    float ink_strength;
};

// A repeat this many pixels long is drawn in full; at half that it is its mean tone.
const float PATTERN_FADE_START_PIXELS = 4.0;

float pattern_detail(float cells_per_pixel)
{
    return 1.0 - smoothstep(1.0 / PATTERN_FADE_START_PIXELS, 2.0 / PATTERN_FADE_START_PIXELS, cells_per_pixel);
}

// Ink `width` of a cell wide, centred on every whole number of `cell`.
float pattern_line(float cell, float width, float cells_per_pixel)
{
    float to_edge = abs(fract(cell - 0.5) - 0.5) - width * 0.5;
    float line    = 1.0 - smoothstep(-0.5, 0.5, to_edge / cells_per_pixel);
    return mix(width, line, pattern_detail(cells_per_pixel));
}

float pattern_stripes(vec2 cell, float width, float waviness)
{
    float along = cell.x + waviness * 0.25 * sin(2.0 * PI * cell.y);
    return pattern_line(along, width, max(fwidth(along), 1e-6));
}

float pattern_grid(vec2 cell, vec2 width, vec2 cells_per_pixel)
{
    return max(pattern_line(cell.x, width.x, cells_per_pixel.x), pattern_line(cell.y, width.y, cells_per_pixel.y));
}

float pattern_checks(vec2 cell, vec2 cells_per_pixel)
{
    vec2 to_edge = (abs(fract(cell * 0.5 + 0.25) - 0.5) - 0.25) * 2.0;
    vec2 side    = clamp(to_edge / cells_per_pixel, vec2(-0.5), vec2(0.5)) * 2.0;
    side        *= vec2(pattern_detail(cells_per_pixel.x), pattern_detail(cells_per_pixel.y));
    return 0.5 - 0.5 * side.x * side.y;
}

float pattern_bricks(vec2 cell, vec2 width, float row_offset, vec2 cells_per_pixel)
{
    float along = cell.x + row_offset * floor(cell.y);
    return max(pattern_line(along, width.x, cells_per_pixel.x), pattern_line(cell.y, width.y, cells_per_pixel.y));
}

float pattern_chevrons(vec2 cell, float width, float pointedness)
{
    float along = cell.x - pointedness * abs(fract(cell.y) - 0.5);
    return pattern_line(along, width, max(fwidth(along), 1e-6));
}

float pattern_dots(vec2 cell, vec2 spacing, float diameter, float stagger, vec2 cells_per_pixel)
{
    float along       = cell.x + stagger * floor(cell.y);
    vec2  from_centre = (fract(vec2(along, cell.y)) - 0.5) * spacing;
    float radius      = diameter * 0.5 * min(spacing.x, spacing.y);

    float world_per_pixel = length(cells_per_pixel * spacing) * 0.70710678;
    float dot_coverage    = 1.0 - smoothstep(-0.5, 0.5, (length(from_centre) - radius) / world_per_pixel);
    float mean            = PI * radius * radius / (spacing.x * spacing.y);
    return mix(mean, dot_coverage, pattern_detail(max(cells_per_pixel.x, cells_per_pixel.y)));
}

// How inked this spot of the face is, 0 to 1.
float pattern_coverage(Pattern pattern, vec2 uv)
{
    vec2 direction = vec2(cos(pattern.angle), sin(pattern.angle));
    vec2 position  = uv * PATTERN_WORLD_UNITS_PER_UV;
    vec2 turned    = vec2(dot(position, direction), dot(position, vec2(-direction.y, direction.x)));
    turned.x      -= pattern.scroll_speed * scene.clock.x;

    vec2 cell            = turned / pattern.spacing;
    vec2 cells_per_pixel = max(fwidth(cell), vec2(1e-6));
    // A line is as thick across as it is along, whatever the two spacings are.
    vec2 width           = pattern.coverage * min(pattern.spacing.x, pattern.spacing.y) / pattern.spacing;

    switch (pattern.kind)
    {
    case PATTERN_STRIPES:  return pattern_stripes(cell, pattern.coverage, pattern.shape_parameter);
    case PATTERN_GRID:     return pattern_grid(cell, width, cells_per_pixel);
    case PATTERN_CHECKS:   return pattern_checks(cell, cells_per_pixel);
    case PATTERN_BRICKS:   return pattern_bricks(cell, width, pattern.shape_parameter, cells_per_pixel);
    case PATTERN_CHEVRONS: return pattern_chevrons(cell, pattern.coverage, pattern.shape_parameter);
    case PATTERN_DOTS:
        return pattern_dots(cell, pattern.spacing, pattern.coverage, pattern.shape_parameter, cells_per_pixel);
    }
    return 0.0;
}

vec3 apply_pattern(vec3 albedo, Pattern pattern, vec2 uv)
{
    return mix(albedo, pattern.ink_color, pattern_coverage(pattern, uv) * pattern.ink_strength);
}

// The r_pattern_preview_* cvars: one pattern over every surface.
vec3 apply_pattern_preview(vec3 albedo, vec2 uv)
{
    Pattern pattern;
    pattern.kind            = int(scene.pattern_preview_cells.x);
    pattern.spacing         = scene.pattern_preview_cells.yz;
    pattern.angle           = scene.pattern_preview_cells.w;
    pattern.coverage        = scene.pattern_preview_shape.x;
    pattern.shape_parameter = scene.pattern_preview_shape.y;
    pattern.scroll_speed    = scene.pattern_preview_shape.z;
    pattern.ink_color       = scene.pattern_preview_ink.rgb;
    pattern.ink_strength    = scene.pattern_preview_ink.a;
    return apply_pattern(albedo, pattern, uv);
}

#endif // PATTERN_GLSL

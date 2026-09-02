#version 450

// mesh_lit.frag with a world-space grid ruled onto the surface. Brushes are
// blockout geometry, so what has to be readable off a face is its SIZE.
//
// It needs no parameters, because fragUV is not an unwrap: generate_brush_mesh
// projects the world position onto the face's dominant axis and divides by the
// 128-unit cell, so fragUV already IS the world position measured in grid cells.
// A grid is a function of world position, never of a surface parameterization --
// against a real unwrap this same code would draw a grid stretched and sheared
// by whatever the unwrap did, and broken at every seam.
//
// Projecting on a WORLD axis rather than on the face's own tangent basis is what
// makes a line continue across a corner onto the next face and onto the editor's
// floor grid. The cost is that a slanted face is ruled at 1/cos(theta); the
// continuity is worth more, and it is what makes this read as the world's grid
// rather than as a texture on one brush.

#include "scene.glsl"

layout(location = 0) in vec3       fragWorldNormal;
layout(location = 1) in vec3       fragColor;
layout(location = 2) in vec2       fragUV;
layout(location = 3) in flat float fragAlpha;
layout(location = 6) in vec3       fragWorldPosition;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D albedo;

#ifdef LIGHTMAP
#include "lightmap.glsl"
#endif

const float MINOR_SUBDIVISIONS = 8.0;  // 128 / 8 = one 16-unit minor cell
const vec3  GRID_COLOR         = vec3(0.06, 0.06, 0.08);
const float MAJOR_STRENGTH     = 0.45;
const float MINOR_STRENGTH     = 0.18;

// Line coverage at `cell`, antialiased and a constant ~1px wide, faded out per
// axis as a cell shrinks towards a couple of pixels.
//
// The fade is not optional. Dividing by fwidth is what pins the line to one
// pixel at any distance, which is the whole appeal up close -- but the cell
// SPACING keeps shrinking, so coverage climbs to 100% and a far wall turns into
// a sheet of grid colour. Correct filtering says coverage should fall to zero
// there (it is what a mip chain would do to a grid texture); this is anti-mip by
// construction, so the fade is where it is handed back.
//
// Per axis and not one scalar over both: at a grazing angle one axis is
// unresolvable while the other is perfectly readable, and dropping only the
// first is what keeps a floor legible out to the horizon.
float grid_coverage(vec2 cell)
{
    vec2 width   = max(fwidth(cell), vec2(1e-8));
    vec2 to_line = abs(fract(cell - 0.5) - 0.5) / width;
    vec2 fade    = smoothstep(vec2(0.5), vec2(0.1), width);
    vec2 line    = (vec2(1.0) - min(to_line, vec2(1.0))) * fade;
    return max(line.x, line.y);
}

void main() {
    vec3  sunDir  = normalize(vec3(0.4, -0.8, 0.3));
    vec3  ambient = scene.ambient.rgb;
    float diffuse = max(dot(normalize(fragWorldNormal), -sunDir), 0.0);
#ifdef LIGHTMAP
    // The four lights this face's chart kept, shaded analytically against the
    // real light direction, plus the residual irradiance of the ones it dropped.
    vec3  lighting = lightmap_direct_diffuse(normalize(fragWorldNormal), fragWorldPosition) +
                     lightmap_residual_diffuse() + ambient;
#else
    vec3  lighting = ambient + vec3(diffuse * 0.85);
#endif
    vec3  color   = texture(albedo, fragUV).rgb * fragColor * lighting;

    // Two levels, 8x apart. The minor one fades as it stops being resolvable and
    // the major one -- still 8x larger on screen -- carries on, so backing away
    // costs subdivisions rather than the grid.
    float ink = max(grid_coverage(fragUV) * MAJOR_STRENGTH,
                    grid_coverage(fragUV * MINOR_SUBDIVISIONS) * MINOR_STRENGTH);

    outColor = vec4(mix(color, GRID_COLOR, ink), fragAlpha);
}

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
#include "surface.glsl"
#include "surface_normal.glsl"
#include "light_gather.glsl"
#include "debug_channels.glsl"
#include "alpha_cutout.glsl"
#include "dissolve.glsl"
#include "peel.glsl"

layout(location = 0) in vec3       fragWorldNormal;
layout(location = 1) in vec3       fragColor;
layout(location = 2) in vec2       fragUV;
layout(location = 3) in flat float fragAlpha;
layout(location = 6) in vec3       fragWorldPosition;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outSurfaceNormal;

layout(set = 0, binding = 0) uniform sampler2D albedo;

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
    float surfaceAlpha = fragAlpha * texture(albedo, fragUV).a;
    discard_below_alpha_cutoff(surfaceAlpha);
    discard_inside_clock_wipe(fragWorldPosition);
    discard_below_dissolve(fragUV);
    discard_inside_peel(fragWorldPosition);

    vec3 N = normalize(fragWorldNormal);
    vec3 V = normalize(scene.camera_position.xyz - fragWorldPosition);
    outSurfaceNormal = store_surface_normal(N);

    // A blockout face has no roughness: r_debug_channel = reflection shows the captures as a MIRROR.
    Surface surface;
    surface.albedo    = texture(albedo, fragUV).rgb * fragColor;
    surface.normal    = N;
    surface.geometric_normal = N;
    surface.uv        = fragUV;
    surface.roughness = 0.0;
    surface.metallic  = 0.0;
    surface.occlusion = 1.0;
    surface.emissive  = vec3(0.0);

    if (showing_debug_channel())
    {
        outColor = debug_channel_color(surface, N, fragWorldPosition, V, albedo);
        return;
    }

    vec3 color = light_surface(LOOK_LAMBERT, surface, fragWorldPosition, V);

    // Two levels, 8x apart. The minor one fades as it stops being resolvable and
    // the major one -- still 8x larger on screen -- carries on, so backing away
    // costs subdivisions rather than the grid.
    float ink = max(grid_coverage(fragUV) * MAJOR_STRENGTH,
                    grid_coverage(fragUV * MINOR_SUBDIVISIONS) * MINOR_STRENGTH);

    outColor = reflection_capture_debug(
        shadow_cascade_debug(vec4(mix(color, GRID_COLOR, ink), surfaceAlpha), fragWorldPosition),
        fragWorldPosition);
    outColor.rgb = dissolve_rim(outColor.rgb, fragUV);
    outColor.rgb = peel_rim(outColor.rgb, fragWorldPosition);
}

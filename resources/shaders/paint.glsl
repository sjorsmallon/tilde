#ifndef PAINT_GLSL
#define PAINT_GLSL

#include "scene.glsl"
#include "simplex_noise.glsl"

// Painterly strokes (r_paint_*): space is cut into Worley cells, stretched along
// r_paint_angle and bent by noise so they read as brush strokes, and every cell
// leans the shading normal by one fixed, hashed amount. Inside a stroke the
// lighting is flat; at its border it jumps, which is the stroke edge.

float paint_strength()    { return scene.paint.x; }             // r_paint
float paint_stroke_width(){ return scene.paint.y; }             // r_paint_stroke, world units
float paint_stretch()     { return scene.paint.z; }             // r_paint_stretch
float paint_warp()        { return scene.paint.w; }             // r_paint_warp, strokes
vec3  paint_direction()   { return scene.paint_direction.xyz; } // unit, the way strokes run

// A stroke narrower than this many pixels across is drawn flat: it would only shimmer.
const float PAINT_FADE_START_PIXELS = 3.0;

uint paint_hash(ivec3 id)
{
    uint hash = uint(id.x) * 0x8da6b343u + uint(id.y) * 0xd8163841u + uint(id.z) * 0xcb1ab31fu;
    hash ^= hash >> 15;
    hash *= 0x2c1b3c6du;
    hash ^= hash >> 12;
    hash *= 0x297a2d39u;
    hash ^= hash >> 15;
    return hash;
}

vec3 paint_hash_unit_cube(ivec3 id)
{
    uint hash = paint_hash(id);
    return vec3(hash & 1023u, (hash >> 10) & 1023u, (hash >> 20) & 1023u) / 1023.0;
}

// The lattice cube whose feature point is nearest to `p`, p in cells.
ivec3 paint_nearest_cell(vec3 p)
{
    ivec3 base    = ivec3(floor(p));
    ivec3 nearest = base;
    float best    = 1e9;
    for (int z = -1; z <= 1; ++z)
    for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x)
    {
        ivec3 cell     = base + ivec3(x, y, z);
        vec3  feature  = vec3(cell) + paint_hash_unit_cube(cell);
        float distance = dot(feature - p, feature - p);
        if (distance < best)
        {
            best    = distance;
            nearest = cell;
        }
    }
    return nearest;
}

// World position in stroke cells: squeezed along the stroke direction so cells stretch into
// strokes, then bent by noise so no two borders are parallel.
vec3 paint_cell_space(vec3 world_position)
{
    vec3  along  = paint_direction();
    vec3  p      = world_position / paint_stroke_width();
    p           -= along * dot(p, along) * (1.0 - 1.0 / paint_stretch());
    float warp   = paint_warp();
    if (warp > 0.0)
    {
        vec3 q = p * 0.5;
        p += warp * vec3(simplex_noise(q.yz), simplex_noise(q.zx + 17.3), simplex_noise(q.xy + 41.7));
    }
    return p;
}

// How far this pixel's stroke leans the normal off `face_normal`, in the face's plane.
vec3 paint_stroke_lean(vec3 face_normal, vec3 world_position)
{
    float strength = paint_strength();
    if (strength <= 0.0)
        return vec3(0.0);
    vec3  p               = paint_cell_space(world_position);
    float cells_per_pixel = length(fwidth(p));
    float detail          = 1.0 - smoothstep(1.0 / PAINT_FADE_START_PIXELS, 2.0 / PAINT_FADE_START_PIXELS, cells_per_pixel);
    if (detail <= 0.0)
        return vec3(0.0);
    vec3 lean = paint_hash_unit_cube(paint_nearest_cell(p) + ivec3(101, 7, 53)) * 2.0 - 1.0;
    lean     -= face_normal * dot(lean, face_normal);
    return lean * strength * detail;
}

#endif // PAINT_GLSL

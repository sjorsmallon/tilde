#ifndef PROCEDURAL_BLENDING_GLSL
#define PROCEDURAL_BLENDING_GLSL

#include "scene.glsl"
#include "mesh_push.glsl"
#include "simplex_noise.glsl"

// https://www.shadertoy.com/view/Xd3XDr, the trick published in https://hal.inria.fr/inria-00537472.
// Drifting sprites of a BASE noise are blended and the pattern is taken from the blend (the
// Shadertoy's deferred third), so a line stays sharp where sprites overlap. The blend keeps the
// noise's variance, where a linear one averages it to grey.
// Laid over the model's own axes in WORLD units, so a resized box shows more of it, never a stretch.
// Every cell of that plane hashes its OWN sprite and a fragment blends the cells around it, so
// nothing wraps and nothing repeats.

const float PROCEDURAL_BLENDING_TAU           = 6.28318530718;
const float PROCEDURAL_BLENDING_CELL_SIZE     = 64.0; // world units; a cell holds one sprite
const int   PROCEDURAL_BLENDING_CELL_REACH    = 2;    // cells blended to each side
const float PROCEDURAL_BLENDING_SPRITE_RADIUS = 1.6;  // in cells; radius + orbit stays within the reach
const float PROCEDURAL_BLENDING_ORBIT_RADIUS  = 0.4;  // in cells
const float PROCEDURAL_BLENDING_ORBIT_SPEED   = 0.3;  // radians per second
const float PROCEDURAL_BLENDING_NOISE_CELLS   = 1.5;  // noise lattice cells per cell
const float PROCEDURAL_BLENDING_NOISE_MEAN    = 0.5;
const float PROCEDURAL_BLENDING_STRIPES       = 0.25; // per cell
const float PROCEDURAL_BLENDING_WARP          = 6.0;  // radians the noise bends a stripe by
const float PROCEDURAL_BLENDING_LINE_WIDTH    = 0.1;
const float PROCEDURAL_BLENDING_FRINGE        = 0.03; // radians between the colour channels

// The face's two axes in the model's frame with the scale divided out, picked by where the normal points.
vec2 procedural_blending_coordinates(vec3 world_position, vec3 world_normal)
{
    mat3 rotation = mesh_normal_matrix();
    vec3 local    = (world_position - pc.model[3].xyz) * rotation;
    vec3 facing   = abs(world_normal * rotation);

    vec2 on_face = local.xy;
    if (facing.x >= facing.y && facing.x >= facing.z)
        on_face = local.zy;
    else if (facing.y >= facing.z)
        on_face = local.xz;
    return on_face / PROCEDURAL_BLENDING_CELL_SIZE;
}

// Hash without sine (Dave Hoskins, MIT): three numbers in 0..1 per cell.
vec3 procedural_blending_cell_hash(vec2 cell)
{
    vec3 mixed = fract(cell.xyx * vec3(0.1031, 0.1030, 0.0973));
    mixed     += dot(mixed, mixed.yxz + 33.33);
    return fract((mixed.xxy + mixed.yzz) * mixed.zyx);
}

float procedural_blending_base_noise(vec2 coordinates)
{
    vec2  home               = floor(coordinates);
    float weight_sum         = 0.0;
    float weight_squared_sum = 0.0;
    float blended            = 0.0;
    for (int row = -PROCEDURAL_BLENDING_CELL_REACH; row <= PROCEDURAL_BLENDING_CELL_REACH; ++row)
    {
        for (int column = -PROCEDURAL_BLENDING_CELL_REACH; column <= PROCEDURAL_BLENDING_CELL_REACH; ++column)
        {
            vec2 cell   = home + vec2(column, row);
            vec3 random = procedural_blending_cell_hash(cell);
            vec2 orbit  = PROCEDURAL_BLENDING_ORBIT_RADIUS
                        * cos(PROCEDURAL_BLENDING_TAU * random.z
                              + scene.clock.x * PROCEDURAL_BLENDING_ORBIT_SPEED + vec2(0.0, 1.6));
            vec2 offset = coordinates - (cell + random.xy + orbit);

            float weight = 1.0 - smoothstep(0.0, PROCEDURAL_BLENDING_SPRITE_RADIUS, length(offset));
            if (weight <= 0.0)
                continue;
            float noise = 0.5 + 0.5 * simplex_noise(offset * PROCEDURAL_BLENDING_NOISE_CELLS + random.yz * 64.0);

            weight_sum         += weight;
            weight_squared_sum += weight * weight;
            blended            += weight * noise;
        }
    }

    return PROCEDURAL_BLENDING_NOISE_MEAN
         + (blended - weight_sum * PROCEDURAL_BLENDING_NOISE_MEAN) / sqrt(max(weight_squared_sum, 1e-6));
}

vec3 procedural_blending_pattern(vec3 world_position, vec3 world_normal)
{
    vec2 coordinates = procedural_blending_coordinates(world_position, world_normal);
    vec3 phase       = PROCEDURAL_BLENDING_TAU * PROCEDURAL_BLENDING_STRIPES * coordinates.x
                     + PROCEDURAL_BLENDING_WARP * procedural_blending_base_noise(coordinates)
                     + PROCEDURAL_BLENDING_FRINGE * vec3(0.0, 1.0, 2.0);
    return 1.0 - smoothstep(0.0, PROCEDURAL_BLENDING_LINE_WIDTH, abs(sin(phase) - 0.5));
}

#endif

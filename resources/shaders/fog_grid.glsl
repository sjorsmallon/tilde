#ifndef FOG_GRID_GLSL
#define FOG_GRID_GLSL

// The fog grid: cells laid over the pass's view, x and y across the screen and z away from the eye, so the cells that share
// an (x, y) are one line of sight cut into slices. Needs scene.glsl.

// The view depth slice number `slice` starts at; each slice is the same fraction thicker than the one before it.
float fog_slice_depth(float slice, float slice_count)
{
    return scene.fog_settings.y * pow(scene.fog_settings.z / scene.fog_settings.y, slice / slice_count);
}

// The world point at `cell` of a grid `size` cells along each axis; cell + 0.5 is a cell's centre.
vec3 fog_cell_point(vec3 cell, vec3 size)
{
    return scene.camera_position.xyz + view_ray(cell.xy / size.xy) * fog_slice_depth(cell.z, size.z);
}

#endif // FOG_GRID_GLSL

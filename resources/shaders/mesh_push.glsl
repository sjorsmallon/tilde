#ifndef MESH_PUSH_GLSL
#define MESH_PUSH_GLSL

// renderer.cpp's mesh_push_constants_t: 128 bytes, the guaranteed Vulkan minimum.
layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color;             // material base colour * draw tint; a is the output alpha
    vec4 clock_wipe_center; // xyz world, w = fraction wiped; xyz = the peel's hole direction (peel.glsl)
    vec4 clock_wipe_axis_x; // w = dissolve threshold, 0 off (dissolve.glsl)
    vec4 clock_wipe_axis_y; // w = peel front angle, 0 off (peel.glsl)
} pc;

// Rotation with the scale divided out; the caller normalizes the result.
mat3 mesh_normal_matrix()
{
    return mat3(normalize(pc.model[0].xyz), normalize(pc.model[1].xyz), normalize(pc.model[2].xyz));
}

#endif

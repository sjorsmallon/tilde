#ifndef MESH_PUSH_GLSL
#define MESH_PUSH_GLSL

// renderer.cpp's mesh_push_constants_t: 128 bytes, the guaranteed Vulkan minimum.
layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color;             // material base colour * draw tint; a is the output alpha
    vec4 clock_wipe_center; // xyz world, w = fraction wiped
    vec4 clock_wipe_axis_x;
    vec4 clock_wipe_axis_y;
} pc;

// Rotation with the scale divided out; the caller normalizes the result.
mat3 mesh_normal_matrix()
{
    return mat3(normalize(pc.model[0].xyz), normalize(pc.model[1].xyz), normalize(pc.model[2].xyz));
}

#endif

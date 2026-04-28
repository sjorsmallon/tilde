#version 450
#include "preview/shader_tool_common.glsl"

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texture_coordinates;

layout(push_constant) uniform PushConstants {
    mat4 model;
} push;

layout(location = 0) out vec3 world_space_position;
layout(location = 1) out vec3 world_space_normal;
layout(location = 2) out vec2 uv;

void main()
{
    vec4 world_position = push.model * vec4(position, 1.0);
    world_space_position = world_position.xyz;
    world_space_normal   = normalize(mat3(push.model) * normal);
    uv = texture_coordinates;
    gl_Position = scene.view_projection * world_position;
}

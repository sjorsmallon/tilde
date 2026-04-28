#version 450
#include "shader_tool_common.glsl"

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;

layout(location = 0) out vec3 frag_world_position;
layout(location = 1) out vec3 frag_world_normal;
layout(location = 2) out vec2 frag_uv;

layout(push_constant) uniform PushConstants {
    mat4 model;
} push;

void main() {
    vec4 world_position = push.model * vec4(in_position, 1.0);
    frag_world_position = world_position.xyz;
    frag_world_normal = normalize(mat3(push.model) * in_normal);
    frag_uv = in_uv;
    gl_Position = scene.view_projection * world_position;
}

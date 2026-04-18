#version 450
#include "shader_tool_common.glsl"

layout(location = 0) in vec3 frag_world_position;
layout(location = 1) in vec3 frag_world_normal;
layout(location = 2) in vec2 frag_uv;

layout(location = 0) out vec4 out_color;

void main() {
    vec3 normal = normalize(frag_world_normal);
    vec3 base_color = scene.param_color[0].rgb;
    if (base_color == vec3(0.0))
        base_color = vec3(0.8, 0.75, 0.7); // default clay color

    // Ambient
    vec3 ambient = base_color * 0.1;

    // Diffuse from scene lights
    vec3 diffuse = compute_lighting(frag_world_position, normal) * base_color;

    // Simple rim lighting for clay feel
    vec3 view_direction = normalize(scene.camera_position.xyz - frag_world_position);
    float rim = 1.0 - max(dot(view_direction, normal), 0.0);
    rim = pow(rim, 3.0) * 0.3;

    vec3 color = ambient + diffuse + vec3(rim);
    out_color = vec4(color, 1.0);
}

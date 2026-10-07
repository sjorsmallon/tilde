#version 450

// The box around one drawn shadow volume's body (shadow_volume_plan.md ss5), volume gl_InstanceIndex (box_corner.glsl).

#include "scene.glsl"
#include "box_corner.glsl"

layout(location = 0) flat out int out_volume;

void main()
{
    int volume  = gl_InstanceIndex;
    out_volume  = volume;
    gl_Position = padded_box_corner_clip_position(scene.shadow_volume_boxes[volume * 2].xyz,
                                                  scene.shadow_volume_boxes[volume * 2 + 1].xyz, gl_VertexIndex);
}

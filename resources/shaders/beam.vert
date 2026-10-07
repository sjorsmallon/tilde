#version 450

// The box around one beam's cone (spot_beam_plan.md ss4), beam gl_InstanceIndex (box_corner.glsl).

#include "scene.glsl"
#include "box_corner.glsl"

layout(location = 0) flat out int out_beam;

void main()
{
    int beam    = gl_InstanceIndex;
    out_beam    = beam;
    gl_Position = padded_box_corner_clip_position(scene.beam_boxes[beam * 2].xyz, scene.beam_boxes[beam * 2 + 1].xyz,
                                                  gl_VertexIndex);
}

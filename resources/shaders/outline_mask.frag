#version 450

// RG = selected (covered, depth-visible), BA = hovered; the pipeline's write mask picks one.
layout(location = 0) out vec4 out_mask;

void main()
{
    out_mask = vec4(1.0);
}

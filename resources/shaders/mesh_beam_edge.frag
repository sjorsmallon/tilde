#version 450

// A spot beam's edge (spot_beam_plan.md ss4): the light's colour, r_beam_edge_pixels wide, feathered
// over one pixel. Like the fill it reads no depth and writes no normal; the depth test clips it.

layout(location = 1) in vec3       fragColor;
layout(location = 3) in flat float fragAlpha;
layout(location = 4) in float      fragAcrossPixels;

layout(location = 0) out vec4 outColor;

#include "scene.glsl"

void main() {
    float width_pixels = scene.beam.w;
    float coverage     = width_pixels > 0.0 ? clamp(0.5 * width_pixels + 0.5 - abs(fragAcrossPixels), 0.0, 1.0) : 0.0;
    outColor           = vec4(fragColor, fragAlpha * coverage);
}

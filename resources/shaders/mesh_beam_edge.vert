#version 450

// One of a spot beam's four apex-to-corner edges (spot_beam_plan.md ss4) as a quad r_beam_edge_pixels
// wide on screen, half a pixel wider each side for the feather mesh_beam_edge.frag draws. inPosition
// is this end of the edge, inNormal its other end, inUV.x the side of the line this vertex sits on
// and inUV.y which end it is. Each end is pulled up to the near plane before the divide, so an edge
// passing beside the camera keeps its direction instead of flipping through infinity.

#include "scene.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(location = 1) out vec3       fragColor;
layout(location = 3) out flat float fragAlpha;
layout(location = 4) out float      fragAcrossPixels;

#include "mesh_push.glsl"

const float NEAR_CLIP_W = 1e-3;

void main() {
    vec4 this_clip  = scene.view_projection * (pc.model * vec4(inPosition, 1.0));
    vec4 other_clip = scene.view_projection * (pc.model * vec4(inNormal, 1.0));

    fragColor = pc.color.rgb;
    fragAlpha = pc.color.a;

    if (this_clip.w < NEAR_CLIP_W && other_clip.w < NEAR_CLIP_W)
    {
        gl_Position      = vec4(0.0, 0.0, -1.0, 1.0);
        fragAcrossPixels = 0.0;
        return;
    }
    if (this_clip.w < NEAR_CLIP_W)
        this_clip = mix(this_clip, other_clip, (NEAR_CLIP_W - this_clip.w) / (other_clip.w - this_clip.w));
    if (other_clip.w < NEAR_CLIP_W)
        other_clip = mix(other_clip, this_clip, (NEAR_CLIP_W - other_clip.w) / (this_clip.w - other_clip.w));

    vec2  half_viewport = 0.5 * scene.beam_viewport.xy;
    vec2  this_pixels   = this_clip.xy / this_clip.w * half_viewport;
    vec2  other_pixels  = other_clip.xy / other_clip.w * half_viewport;
    vec2  along         = (other_pixels - this_pixels) * (inUV.y < 0.5 ? 1.0 : -1.0);
    float along_length  = length(along);
    vec2  across        = along_length > 1e-3 ? vec2(-along.y, along.x) / along_length : vec2(0.0);

    float half_width     = 0.5 * scene.beam.w + 0.5;
    vec2  offset_pixels  = across * inUV.x * half_width;
    gl_Position          = this_clip + vec4(offset_pixels / half_viewport * this_clip.w, 0.0, 0.0);
    fragAcrossPixels     = inUV.x * half_width;
}

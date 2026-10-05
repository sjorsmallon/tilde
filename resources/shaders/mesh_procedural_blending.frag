#version 450

// Unlit: the tint times procedural_blending.glsl's pattern, which reads no texture at all.

layout(location = 0) in vec3       fragWorldNormal;
layout(location = 1) in vec3       fragColor;
layout(location = 2) in vec2       fragUV;
layout(location = 3) in flat float fragAlpha;
layout(location = 6) in vec3       fragWorldPosition;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outSurfaceNormal;

#include "surface_normal.glsl"
#include "procedural_blending.glsl"
#include "dissolve.glsl"
#include "peel.glsl"
#include "reveal.glsl"

void main() {
    discard_inside_clock_wipe(fragWorldPosition);
    discard_below_dissolve(fragUV);
    discard_inside_peel(fragWorldPosition);
    discard_outside_reveal(fragWorldPosition);

    vec3 pattern = procedural_blending_pattern(fragWorldPosition, normalize(fragWorldNormal));
    outColor     = vec4(fragColor * pattern, fragAlpha);
    outSurfaceNormal = store_surface_normal(fragWorldNormal);
    outColor.rgb = dissolve_rim(outColor.rgb, fragUV);
    outColor.rgb = peel_rim(outColor.rgb, fragWorldPosition);
    outColor.rgb = reveal_rim(outColor.rgb, fragWorldPosition, normalize(fragWorldNormal));
}

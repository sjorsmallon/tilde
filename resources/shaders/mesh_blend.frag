#version 450

// mesh_lit.frag, composing BLEND_LAYER_COUNT material layers by the per-vertex
// weights. Written as a weighted SUM rather than a mix() so a third layer is
// another sampler, another weight and another term -- the same shape, not a
// different one. Layer 0's weight is what the others leave.

#include "scene.glsl"
#include "surface.glsl"
#include "surface_normal.glsl"
#include "light_gather.glsl"
#include "debug_channels.glsl"
#include "alpha_cutout.glsl"
#include "dissolve.glsl"
#include "peel.glsl"
#include "reveal.glsl"
#include "pattern.glsl"

layout(location = 0) in vec3       fragWorldNormal;
layout(location = 1) in vec3       fragColor;
layout(location = 2) in vec2       fragUV;
layout(location = 3) in flat float fragAlpha;
layout(location = 4) in float      fragBlendWeight1;
layout(location = 6) in vec3       fragWorldPosition;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outSurfaceNormal;

layout(set = 0, binding = 0) uniform sampler2D albedo;

// Binding 4 of the material set. An absent emissive map resolves to the internal
// 1x1 BLACK, which is why this is a fetch and never a branch: a material that
// does not glow adds zero, exactly as the tracer's null emissive contributes
// nothing (lighting_def.md gate 4).
layout(set = 0, binding = 4) uniform sampler2D emissiveMap;
// Set 2 is the layers above the base -- one set per layer, all through the same
// single-sampler layout set 0 uses, so a blended material costs no new
// descriptor machinery.
layout(set = 2, binding = 0) uniform sampler2D blendAlbedo1;

void main() {
    float surfaceAlpha = fragAlpha * texture(albedo, fragUV).a;
    discard_below_alpha_cutoff(surfaceAlpha);
    discard_inside_clock_wipe(fragWorldPosition);
    discard_below_dissolve(fragUV);
    discard_inside_peel(fragWorldPosition);
    discard_outside_reveal(fragWorldPosition);

    vec3 N = normalize(fragWorldNormal);
    vec3 V = normalize(scene.camera_position.xyz - fragWorldPosition);
    outSurfaceNormal = store_surface_normal(N);

    float weight1 = clamp(fragBlendWeight1, 0.0, 1.0);
    float weight0 = clamp(1.0 - weight1, 0.0, 1.0);

    vec3 layers = cel_flat_albedo(albedo, fragUV, texture(albedo, fragUV).rgb) * weight0 +
                  cel_flat_albedo(blendAlbedo1, fragUV, texture(blendAlbedo1, fragUV).rgb) * weight1;

    Surface surface;
    surface.albedo    = apply_pattern_preview(layers * fragColor, fragUV);
    surface.normal    = N;
    surface.geometric_normal = N;
    surface.uv        = fragUV;
    surface.roughness = 0.0;
    surface.metallic  = 0.0;
    surface.occlusion = 1.0;
    // LAYER 0's emissive only, weighted by its own coverage -- so where layer 1
    // covers the surface, layer 0 stops glowing. That is also the layer the bake
    // reads (surface_at resolves layer 0), so the two agree.
    surface.emissive  = texture(emissiveMap, fragUV).rgb * weight0;

    if (showing_debug_channel())
    {
        outColor = debug_channel_color(surface, N, fragWorldPosition, V, albedo);
        return;
    }

    float solid_ink;
    vec3  color = light_surface(LOOK_LAMBERT, surface, fragWorldPosition, V, solid_ink);
    outSurfaceNormal = store_solid_ink(outSurfaceNormal, solid_ink);

    outColor = reflection_capture_debug(
        shadow_cascade_debug(vec4(color, surfaceAlpha), fragWorldPosition), fragWorldPosition);
    outColor.rgb = dissolve_rim(outColor.rgb, fragUV);
    outColor.rgb = peel_rim(outColor.rgb, fragWorldPosition);
    outColor.rgb = reveal_rim(outColor.rgb, fragWorldPosition, N);
}

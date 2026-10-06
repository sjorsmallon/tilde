#version 450

// The lit half of the mesh family. Every mesh pipeline binds a material set, so
// there is no untextured variant to keep in sync: a material with no albedo
// resolves to the renderer's internal 1x1 white at registration and the colour
// multiplies out of the sample below.

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
layout(location = 6) in vec3       fragWorldPosition;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outSurfaceNormal;

layout(set = 0, binding = 0) uniform sampler2D albedo;

// Binding 4 of the material set. An absent emissive map resolves to the internal
// 1x1 BLACK, which is why this is a fetch and never a branch: a material that
// does not glow adds zero, exactly as the tracer's null emissive contributes
// nothing (lighting_def.md gate 4).
layout(set = 0, binding = 4) uniform sampler2D emissiveMap;

#ifdef PBR
layout(set = 0, binding = 1) uniform sampler2D normalMap;
layout(set = 0, binding = 2) uniform sampler2D ormMap;   // R occlusion, G roughness, B metallic
layout(set = 0, binding = 3) uniform sampler2D heightMap;

const int LOOK = LOOK_PBR;
#else
const int LOOK = LOOK_LAMBERT;
#endif

Surface read_surface(vec3 geometric_normal, vec3 V)
{
    Surface surface;
    surface.geometric_normal = geometric_normal;
#ifdef PBR
    mat3 tangent_frame = cotangent_frame(geometric_normal, fragWorldPosition, fragUV);
    // The cel look is flat: no march, and the albedo stays where the face put it.
    surface.uv         = frame_look(LOOK) == LOOK_CEL
                             ? fragUV
                             : parallax_occlusion(heightMap,
                                                  view_direction_in_tangent_space(tangent_frame, V),
                                                  fragUV);
    vec3 tangent_normal = texture(normalMap, surface.uv).xyz * 2.0 - 1.0;
    surface.normal      = apply_normal_map(tangent_frame, tangent_normal);

    vec3 orm          = texture(ormMap, surface.uv).rgb;
    surface.occlusion = orm.r;
    surface.roughness = orm.g;
    surface.metallic  = orm.b;
#else
    // No roughness on this arm, so r_debug_channel = reflection shows the captures as a MIRROR.
    surface.uv        = fragUV;
    surface.normal    = geometric_normal;
    surface.occlusion = 1.0;
    surface.roughness = 0.0;
    surface.metallic  = 0.0;
#endif
    // fragColor is the material's base colour times the draw's tint, so it tints rather than replaces.
    surface.albedo   = cel_flat_albedo(albedo, surface.uv, texture(albedo, surface.uv).rgb) * fragColor;
    surface.albedo   = apply_pattern_preview(surface.albedo, fragUV);
    // Straight through, tinted by nothing: the tracer collects this same texel (lighting_def.md ss11).
    surface.emissive = texture(emissiveMap, surface.uv).rgb;
    return surface;
}

void main() {
    float surfaceAlpha = fragAlpha * texture(albedo, fragUV).a;
    discard_below_alpha_cutoff(surfaceAlpha);
    discard_inside_clock_wipe(fragWorldPosition);
    discard_below_dissolve(fragUV);
    discard_inside_peel(fragWorldPosition);
    discard_outside_reveal(fragWorldPosition);

    // A double-sided material draws unculled; its back is lit along the flipped normal.
    vec3 geometric_normal = normalize(fragWorldNormal) * (gl_FrontFacing ? 1.0 : -1.0);
    vec3 V                = normalize(scene.camera_position.xyz - fragWorldPosition);

    Surface surface = read_surface(geometric_normal, V);
    // The ink's crease is where two FACES meet; inking a normal map traces every bump in the texture.
    outSurfaceNormal = store_surface_normal(geometric_normal);

    if (showing_debug_channel())
    {
        outColor = debug_channel_color(surface, geometric_normal, fragWorldPosition, V, albedo);
        return;
    }

    float solid_ink;
    vec3  lit = light_surface(LOOK, surface, fragWorldPosition, V, solid_ink);
    outSurfaceNormal = store_solid_ink(outSurfaceNormal, solid_ink);

    outColor = reflection_capture_debug(shadow_cascade_debug(vec4(lit, surfaceAlpha), fragWorldPosition),
                                        fragWorldPosition);
    outColor.rgb = dissolve_rim(outColor.rgb, fragUV);
    outColor.rgb = peel_rim(outColor.rgb, fragWorldPosition);
    outColor.rgb = reveal_rim(outColor.rgb, fragWorldPosition, geometric_normal);
}

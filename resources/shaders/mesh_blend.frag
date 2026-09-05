#version 450

// mesh_lit.frag, composing BLEND_LAYER_COUNT material layers by the per-vertex
// weights. Written as a weighted SUM rather than a mix() so a third layer is
// another sampler, another weight and another term -- the same shape, not a
// different one. Layer 0's weight is what the others leave.

#include "scene.glsl"
#include "direct_light.glsl"

layout(location = 0) in vec3       fragWorldNormal;
layout(location = 1) in vec3       fragColor;
layout(location = 2) in vec2       fragUV;
layout(location = 3) in flat float fragAlpha;
layout(location = 4) in float      fragBlendWeight1;
layout(location = 6) in vec3       fragWorldPosition;

layout(location = 0) out vec4 outColor;

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

#ifdef LIGHTMAP
#include "lightmap.glsl"
#endif

void main() {
    vec3 N = normalize(fragWorldNormal);

    if ((scene.debug_flags & DEBUG_FLAGS_SHOWING_VISIBILITY) != 0)
    {
        outColor = shadow_visibility_debug_color(fragWorldPosition, N);
        return;
    }
    if ((scene.debug_flags & DEBUG_FLAG_RENDER_DIRECT_LIGHT) != 0)
    {
        vec3 direct = analytic_tail_diffuse(N, fragWorldPosition);
#ifdef LIGHTMAP
        direct += lightmap_direct_diffuse(N, fragWorldPosition);
#endif
        outColor = vec4(direct, 1.0);
        return;
    }
    if ((scene.debug_flags & DEBUG_FLAG_RENDER_BAKED_LIGHT) != 0)
    {
#ifdef LIGHTMAP
        vec3 baked = lightmap_residual_diffuse() + lightmap_indirect_diffuse(N);
#else
        vec3 baked = vec3(0.0); // this path reads no probes; its fill is the fixed fake sun
#endif
        outColor = vec4(baked, 1.0);
        return;
    }

    float weight1 = clamp(fragBlendWeight1, 0.0, 1.0);
    float weight0 = clamp(1.0 - weight1, 0.0, 1.0);

    vec3 layers = texture(albedo, fragUV).rgb * weight0 +
                  texture(blendAlbedo1, fragUV).rgb * weight1;

    vec3  sunDir  = normalize(vec3(0.4, -0.8, 0.3));
    vec3  ambient = scene.ambient.rgb;
    float diffuse = max(dot(N, -sunDir), 0.0);

#ifdef LIGHTMAP
    // The four lights this face's chart kept, shaded analytically against the
    // real light direction, the residual irradiance of the ones it dropped, and
    // the path-traced bounce.
    vec3 lighting = lightmap_direct_diffuse(N, fragWorldPosition) +
                    lightmap_residual_diffuse() + lightmap_indirect_diffuse(N) + ambient;
#else
    vec3 lighting = ambient + vec3(diffuse * 0.85);
#endif
    // The tail through its shadow maps, as mesh_grid.frag.
    lighting += analytic_tail_diffuse(N, fragWorldPosition);

    // LAYER 0's emissive only, weighted by its own coverage -- so where layer 1
    // covers the surface, layer 0 stops glowing. That is also the layer the bake
    // reads (surface_at resolves layer 0), so the two agree.
    outColor = shadow_cascade_debug(vec4(layers * fragColor * lighting +
                                             texture(emissiveMap, fragUV).rgb * weight0,
                                         fragAlpha),
                                    fragWorldPosition);
}

#version 450

// mesh_lit.frag, composing BLEND_LAYER_COUNT material layers by the per-vertex
// weights. Written as a weighted SUM rather than a mix() so a third layer is
// another sampler, another weight and another term -- the same shape, not a
// different one. Layer 0's weight is what the others leave.

#include "scene.glsl"

layout(location = 0) in vec3       fragWorldNormal;
layout(location = 1) in vec3       fragColor;
layout(location = 2) in vec2       fragUV;
layout(location = 3) in flat float fragAlpha;
layout(location = 4) in float      fragBlendWeight1;
layout(location = 6) in vec3       fragWorldPosition;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D albedo;
// Set 2 is the layers above the base -- one set per layer, all through the same
// single-sampler layout set 0 uses, so a blended material costs no new
// descriptor machinery.
layout(set = 2, binding = 0) uniform sampler2D blendAlbedo1;

#ifdef LIGHTMAP
#include "lightmap.glsl"
#endif

void main() {
    float weight1 = clamp(fragBlendWeight1, 0.0, 1.0);
    float weight0 = clamp(1.0 - weight1, 0.0, 1.0);

    vec3 layers = texture(albedo, fragUV).rgb * weight0 +
                  texture(blendAlbedo1, fragUV).rgb * weight1;

    vec3  sunDir  = normalize(vec3(0.4, -0.8, 0.3));
    vec3  ambient = scene.ambient.rgb;
    float diffuse = max(dot(normalize(fragWorldNormal), -sunDir), 0.0);

#ifdef LIGHTMAP
    // The four lights this face's chart kept, shaded analytically against the
    // real light direction, plus the residual irradiance of the ones it dropped.
    vec3 lighting = lightmap_direct_diffuse(normalize(fragWorldNormal), fragWorldPosition) +
                    lightmap_residual_diffuse() + ambient;
#else
    vec3 lighting = ambient + vec3(diffuse * 0.85);
#endif

    outColor = vec4(layers * fragColor * lighting, fragAlpha);
}

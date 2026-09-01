#version 450

// The lit half of the mesh family. Every mesh pipeline binds a material set, so
// there is no untextured variant to keep in sync: a material with no albedo
// resolves to the renderer's internal 1x1 white at registration and the colour
// multiplies out of the sample below.

#include "scene.glsl"

layout(location = 0) in vec3       fragWorldNormal;
layout(location = 1) in vec3       fragColor;
layout(location = 2) in vec2       fragUV;
layout(location = 3) in flat float fragAlpha;
layout(location = 6) in vec3       fragWorldPosition;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D albedo;

#ifdef PBR
#include "pbr_lighting.glsl"

layout(set = 0, binding = 1) uniform sampler2D normalMap;
layout(set = 0, binding = 2) uniform sampler2D ormMap;   // R occlusion, G roughness, B metallic
layout(set = 0, binding = 3) uniform sampler2D heightMap;

#define DEBUG_FLAG_RENDER_NORMALS     (1 << 0)
#define DEBUG_FLAG_RENDER_UV          (1 << 1)
#define DEBUG_FLAG_RENDER_PARALLAX_UV (1 << 2)
#endif

#ifdef LIGHTMAP
#include "lightmap.glsl"
#endif

void main() {
    vec3 ambient = scene.ambient.rgb;

#ifdef PBR
    vec3 N = normalize(fragWorldNormal);
    vec3 V = normalize(scene.camera_position.xyz - fragWorldPosition);

    vec2 uv = parallax_occlusion(
        heightMap, view_direction_in_tangent_space(N, V, fragWorldPosition, fragUV), fragUV);

    N = apply_normal_map(N, texture(normalMap, uv).xyz * 2.0 - 1.0, fragWorldPosition, uv);

    vec3  surface   = texture(albedo, uv).rgb * fragColor;
    vec3  orm       = texture(ormMap, uv).rgb;
    float occlusion = orm.r;
    float roughness = orm.g;
    float metallic  = orm.b;

    if ((scene.debug_flags & DEBUG_FLAG_RENDER_NORMALS) != 0)
    {
        outColor = vec4(N * 0.5 + 0.5, 1.0);
        return;
    }
    if ((scene.debug_flags & DEBUG_FLAG_RENDER_UV) != 0)
    {
        outColor = vec4(uv, 0.0, 1.0);
        return;
    }
    if ((scene.debug_flags & DEBUG_FLAG_RENDER_PARALLAX_UV) != 0)
    {
        outColor = vec4(texture(albedo, uv).rgb, 1.0);
        return;
    }

    vec3 lit = vec3(0.0);
    for (int index = 0; index < scene.light_count; ++index)
    {
        Light light = scene.lights[index];

#ifdef LIGHTMAP
        // This surface already has the Mixed light out of the atlas. Evaluating
        // it here too is the double-count lighting_def.md ss2 exists to prevent.
        if (LIGHT_IS_ALSO_BAKED(light))
            continue;
#endif

        vec4  arrival = light_arrival(int(light.spot_params.w), light.position.xyz,
                                      light.direction.xyz, light.spot_params.x,
                                      light.spot_params.y, light.spot_params.z,
                                      fragWorldPosition);

        lit += shade_direct(N, V, arrival.xyz, surface, roughness, metallic,
                            light.radiance.rgb, arrival.w);
    }

#ifdef LIGHTMAP
    lit += (1.0 - metallic) * surface * lightmap_diffuse();
#endif

    lit += ambient * surface * occlusion;

    outColor = vec4(lit, fragAlpha);
#else
    // Hardcoded directional sun light
    vec3  sunDir  = normalize(vec3(0.4, -0.8, 0.3));
    float diffuse = max(dot(normalize(fragWorldNormal), -sunDir), 0.0);
    // fragColor is the material's base colour times the draw's tint, so it tints
    // rather than replaces.
#ifdef LIGHTMAP
    vec3 lighting = lightmap_diffuse() + ambient;
#else
    vec3 lighting = ambient + vec3(diffuse * 0.85);
#endif
    vec3 color = texture(albedo, fragUV).rgb * fragColor * lighting;
    outColor   = vec4(color, fragAlpha);
#endif
}

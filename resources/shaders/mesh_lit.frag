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

#ifdef LIGHTMAP
    // The four this face's chart kept, and nothing else in the level. The atlas
    // did the culling at bake time (lightmap.glsl says how), so this loop is
    // four iterations whether the map holds two lights or sixty -- which is what
    // unblocked making every Baked light analytic (lighting_def.md ss14 step 6).
    //
    // Analytic means the material's other three maps finally do something on a
    // brush face: the real light direction against the normal-mapped normal,
    // with the bake contributing only the shadow.
    vec4 coverage = lightmap_coverage();
    for (int channel = 0; channel < LIGHTMAP_LIGHTS_PER_CHART; ++channel)
    {
        int slot = lightmap_chart_slot(channel);
        if (slot < 0 || coverage[channel] <= 0.0)
            continue;

        Light         light   = scene.lights[slot];
        Light_Arrival arrival = light_arrival(light, fragWorldPosition);

        lit += shade_direct(N, V, arrival.direction, surface, roughness, metallic,
                            light.radiance.rgb, arrival.attenuation * coverage[channel],
                            light.direction.w, arrival.distance);
    }
#endif

    // The tail: the lights no bake saw, plus a second copy of every Mixed one so
    // a surface with no chart still gets it. A lightmapped surface SKIPS that
    // second copy -- it shaded the light through its chart above, with the
    // shadow, and shading it here as well is the ss2 double-count.
    for (int index = scene.baked_light_count; index < scene.light_count; ++index)
    {
        Light light = scene.lights[index];

#ifdef LIGHTMAP
        if (LIGHT_BAKED_SLOT(light) >= 0)
            continue;
#endif

        Light_Arrival arrival = light_arrival(light, fragWorldPosition);

        lit += shade_direct(N, V, arrival.direction, surface, roughness, metallic,
                            light.radiance.rgb, arrival.attenuation,
                            light.direction.w, arrival.distance);
    }

#ifdef LIGHTMAP
    // What the atlas still holds: the lights this chart ranked below its four.
    lit += (1.0 - metallic) * surface * lightmap_residual_diffuse();
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
    vec3 lighting = lightmap_direct_diffuse(normalize(fragWorldNormal), fragWorldPosition) +
                    lightmap_residual_diffuse() + ambient;
#else
    vec3 lighting = ambient + vec3(diffuse * 0.85);
#endif
    vec3 color = texture(albedo, fragUV).rgb * fragColor * lighting;
    outColor   = vec4(color, fragAlpha);
#endif
}

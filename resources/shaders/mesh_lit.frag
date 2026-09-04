#version 450

// The lit half of the mesh family. Every mesh pipeline binds a material set, so
// there is no untextured variant to keep in sync: a material with no albedo
// resolves to the renderer's internal 1x1 white at registration and the colour
// multiplies out of the sample below.

#include "scene.glsl"
#include "probes.glsl"
#include "direct_light.glsl"

layout(location = 0) in vec3       fragWorldNormal;
layout(location = 1) in vec3       fragColor;
layout(location = 2) in vec2       fragUV;
layout(location = 3) in flat float fragAlpha;
layout(location = 6) in vec3       fragWorldPosition;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D albedo;

// Binding 4 of the material set. An absent emissive map resolves to the internal
// 1x1 BLACK, which is why this is a fetch and never a branch: a material that
// does not glow adds zero, exactly as the tracer's null emissive contributes
// nothing (lighting_def.md gate 4).
layout(set = 0, binding = 4) uniform sampler2D emissiveMap;

#ifdef PBR
#include "pbr_lighting.glsl"

layout(set = 0, binding = 1) uniform sampler2D normalMap;
layout(set = 0, binding = 2) uniform sampler2D ormMap;   // R occlusion, G roughness, B metallic
layout(set = 0, binding = 3) uniform sampler2D heightMap;
#endif

#ifdef LIGHTMAP
#include "lightmap.glsl"
#endif

void main() {
    vec3 ambient = scene.ambient.rgb;

    // Ahead of every arm: a shadow that is wrong looks exactly like lighting
    // that is wrong, and this is how the two are told apart.
    if ((scene.debug_flags & DEBUG_FLAG_RENDER_SHADOW_VISIBILITY) != 0)
    {
        outColor = shadow_visibility_debug_color(fragWorldPosition, normalize(fragWorldNormal));
        return;
    }

#ifdef PBR
    vec3 N = normalize(fragWorldNormal);
    vec3 V = normalize(scene.camera_position.xyz - fragWorldPosition);

    mat3 tangent_frame = cotangent_frame(N, fragWorldPosition, fragUV);
    vec2 uv            = parallax_occlusion(
        heightMap, view_direction_in_tangent_space(tangent_frame, V), fragUV);

    N = apply_normal_map(tangent_frame, texture(normalMap, uv).xyz * 2.0 - 1.0);

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

        // Atlas visibility TIMES the shadow map (decision K): the bake holds the
        // static occluders, a Mixed light's map only the dynamic ones, so the
        // two are independent blockers and the product counts nothing twice.
        float visibility = coverage[channel] * shadow_visibility(light, arrival, fragWorldPosition, N);

        lit += shade_direct(N, V, arrival.direction, surface, roughness, metallic,
                            light.radiance.rgb, arrival.attenuation * visibility,
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

        Light_Arrival arrival    = light_arrival(light, fragWorldPosition);
        float         visibility = shadow_visibility(light, arrival, fragWorldPosition, N);

        lit += shade_direct(N, V, arrival.direction, surface, roughness, metallic,
                            light.radiance.rgb, arrival.attenuation * visibility,
                            light.direction.w, arrival.distance);
    }

#ifdef LIGHTMAP
    // What the atlas still holds: the lights this chart ranked below its four,
    // flat -- plus the bounce, which is convolved against N and therefore is the
    // one baked term a normal map can move.
    lit += (1.0 - metallic) * surface *
           (lightmap_residual_diffuse() + lightmap_indirect_diffuse(N));
#else
    // No chart: the probe volume is where this surface's baked light lives, the
    // direct Baked lights and the bounce both (lighting_def.md gate 5).
    lit += (1.0 - metallic) * surface * probe_indirect_diffuse(fragWorldPosition, N);
#endif

    lit += ambient * surface * occlusion;

    // Straight through, tinted by nothing: the tracer collects this same texel
    // and applies no base colour, so a factor here would light the room from one
    // number and draw it from another -- ss11.
    lit += texture(emissiveMap, uv).rgb;

    outColor = vec4(lit, fragAlpha);
#else
    // The non-PBR arm is Lambert against the SAME light list the PBR arm shades:
    // the analytic tail here, the chart's four slots and the atlas on a
    // lightmapped face, the probes on everything else. It used to be a
    // hardcoded sun from a fixed direction plus the floor, which is a second
    // lighting model (ss11) -- and the one every physics body and untextured
    // prop drew through, so gate 5's probes landed under a sun that ignored
    // them.
    vec3 N = normalize(fragWorldNormal);

    // The tail: the lights no bake saw, plus a second copy of every Mixed one,
    // each through its shadow map. A lightmapped face skips that copy -- it
    // shades the light through its chart below -- exactly as the PBR arm does.
    vec3 lighting = analytic_tail_diffuse(N, fragWorldPosition);

#ifdef LIGHTMAP
    lighting += lightmap_direct_diffuse(N, fragWorldPosition) + lightmap_residual_diffuse() +
                lightmap_indirect_diffuse(N);
#else
    lighting += probe_indirect_diffuse(fragWorldPosition, N);
#endif
    lighting += ambient;

    // fragColor is the material's base colour times the draw's tint, so it tints
    // rather than replaces.
    vec3 color = texture(albedo, fragUV).rgb * fragColor * lighting +
                 texture(emissiveMap, fragUV).rgb;
    outColor   = vec4(color, fragAlpha);
#endif
}

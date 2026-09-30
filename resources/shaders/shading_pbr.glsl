#ifndef SHADING_PBR_GLSL
#define SHADING_PBR_GLSL

#include "surface.glsl"
#include "pbr_lighting.glsl"
#include "reflection.glsl"

vec3 shade_light_pbr(Surface surface, vec3 V, Incoming_Light light)
{
    return shade_direct(surface.normal, V, light.direction, surface.albedo, surface.roughness,
                        surface.metallic, light.radiance * light.visibility, light.attenuation,
                        light.source_radius, light.distance);
}

vec3 shade_ambient_pbr(Surface surface, vec3 V, vec3 world_position, vec3 baked_irradiance,
                       vec3 ambient_floor)
{
    vec3 diffuse    = (1.0 - surface.metallic) * surface.albedo * baked_irradiance;
    vec3 reflection = environment_specular(world_position, surface.normal, V, surface.roughness,
                                           mix(vec3(0.04), surface.albedo, surface.metallic));
    return diffuse + reflection + ambient_floor * surface.albedo * surface.occlusion;
}

#endif // SHADING_PBR_GLSL

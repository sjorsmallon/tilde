#ifndef PBR_LIGHTING_GLSL
#define PBR_LIGHTING_GLSL

// Guarded so this file and lightmap.glsl can both be included, in either
// order, by mesh_lit.frag's -DPBR -DLIGHTMAP variant.
#ifndef PI
#define PI 3.14159265359
#endif

// The arrival maths -- the falloff, the cone and the type dispatch -- is one
// layer down, so the shader tool's vertex shader can reach it without the
// screen-space derivatives below. lighting_def.md ss11.
#include "light_arrival.glsl"

float distribution_GGX(vec3 N, vec3 H, float a)
{
    float a2     = a * a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float nom   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom       = PI * denom * denom;

    return nom / denom;
}

float geometry_schlick_ggx(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometry_smith(vec3 N, vec3 V, vec3 L, float roughness)
{
    return geometry_schlick_ggx(max(dot(N, V), 0.0), roughness) *
           geometry_schlick_ggx(max(dot(N, L), 0.0), roughness);
}

vec3 fresnel_schlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 shade_direct(vec3 N, vec3 V, vec3 L, vec3 albedo, float roughness, float metallic,
                  vec3 radiance, float attenuation)
{
    vec3 H  = normalize(L + V);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    float NDF = distribution_GGX(N, H, roughness);
    float G   = geometry_smith(N, V, L, roughness);
    vec3  F   = fresnel_schlick(max(dot(H, V), 0.0), F0);

    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

    float denom    = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3  specular = (NDF * G * F) / denom;

    float NdotL = max(dot(N, L), 0.0);
    return (kD * albedo / PI + specular) * radiance * attenuation * NdotL;
}

vec2 parallax_occlusion(sampler2D height_map, vec3 view_direction_tangent, vec2 uv)
{
    const float height_scale = 0.01;

    const float minimum_layer_count = 8.0;
    const float maximum_layer_count = 32.0;

    vec3  view_direction   = normalize(view_direction_tangent);
    float layer_count      = mix(maximum_layer_count, minimum_layer_count, abs(view_direction.z));
    float layer_depth_step = 1.0 / layer_count;

    vec2 uv_step = (view_direction.xy / view_direction.z) * height_scale * layer_depth_step;

    vec2  current_uv            = uv;
    float current_ray_depth     = 0.0;
    float current_surface_depth = texture(height_map, current_uv).r;

    while (current_ray_depth < current_surface_depth)
    {
        current_uv            -= uv_step;
        current_ray_depth     += layer_depth_step;
        current_surface_depth  = texture(height_map, current_uv).r;
    }

    vec2  previous_uv            = current_uv + uv_step;
    float previous_surface_depth = texture(height_map, previous_uv).r;
    float previous_ray_depth     = current_ray_depth - layer_depth_step;

    float overshoot  = current_ray_depth - current_surface_depth;
    float undershoot = previous_surface_depth - previous_ray_depth;

    return mix(current_uv, previous_uv, overshoot / (overshoot + undershoot));
}

vec3 apply_normal_map(vec3 N, vec3 tangent_normal, vec3 world_position, vec2 uv)
{
    vec3 Q1  = dFdx(world_position);
    vec3 Q2  = dFdy(world_position);
    vec2 st1 = dFdx(uv);
    vec2 st2 = dFdy(uv);

    vec3 T = normalize(Q1 * st2.t - Q2 * st1.t);
    vec3 B = cross(N, T);

    return normalize(mat3(T, B, N) * tangent_normal);
}

vec3 view_direction_in_tangent_space(vec3 N, vec3 V, vec3 world_position, vec2 uv)
{
    vec3 Q1  = dFdx(world_position);
    vec3 Q2  = dFdy(world_position);
    vec2 st1 = dFdx(uv);
    vec2 st2 = dFdy(uv);

    vec3 T = normalize(Q1 * st2.t - Q2 * st1.t);
    vec3 B = cross(N, T);

    return normalize(transpose(mat3(T, B, N)) * V);
}

#endif // PBR_LIGHTING_GLSL

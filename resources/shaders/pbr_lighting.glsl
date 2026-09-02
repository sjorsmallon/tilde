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

// Karis' representative point (SIGGRAPH 2013 course notes): a sphere's specular
// highlight is approximated by a PUNCTUAL light placed at the point of the sphere
// closest to the reflection ray. That is what makes an area light's highlight
// broad and its shape readable instead of the pinprick a point source gives.
//
// It moves the SPECULAR direction only. Diffuse keeps pointing at the sphere's
// centre, which is what a diffuse integral over the sphere reduces to for
// anything not touching it -- and moving both would swing the terminator around
// with the camera.
vec3 sphere_representative_direction(vec3 N, vec3 V, vec3 L, float distance_to_light,
                                     float source_radius)
{
    vec3 R             = reflect(-V, N);
    vec3 to_centre     = L * distance_to_light;
    vec3 centre_to_ray = dot(to_centre, R) * R - to_centre;
    vec3 closest_point = to_centre +
                         centre_to_ray * clamp(source_radius / max(length(centre_to_ray), 0.0001),
                                               0.0, 1.0);
    return normalize(closest_point);
}

// `source_radius` is the emitter's size and `distance_to_light` what it is
// measured against, both straight off Light_Arrival -- a directional light
// reports a unit distance and a radius already divided by it, so there is no
// light-type branch here. A radius of ZERO is the punctual shading this had
// before, expression for expression.
vec3 shade_direct(vec3 N, vec3 V, vec3 L, vec3 albedo, float roughness, float metallic,
                  vec3 radiance, float attenuation, float source_radius, float distance_to_light)
{
    // The lobe width the specular is evaluated at, widened below by the solid
    // angle the emitter covers. Floored because the energy term divides by it.
    float alpha         = max(roughness, 0.001);
    float energy        = 1.0;
    vec3  L_specular    = L;

    if (source_radius > 0.0)
    {
        L_specular = sphere_representative_direction(N, V, L, distance_to_light, source_radius);

        // The normalization that keeps the widened lobe carrying the SAME total
        // energy: spreading a highlight without it makes an area light brighter
        // than the point light it replaced, which reads as a tuning bug.
        float widened = clamp(alpha + source_radius / (2.0 * max(distance_to_light, 0.0001)),
                              0.0, 1.0);
        energy        = (alpha / widened) * (alpha / widened);
        alpha         = widened;
    }

    vec3 H  = normalize(L_specular + V);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    float NDF = distribution_GGX(N, H, alpha) * energy;
    float G   = geometry_smith(N, V, L_specular, alpha);
    vec3  F   = fresnel_schlick(max(dot(H, V), 0.0), F0);

    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

    float NdotL_diffuse  = max(dot(N, L), 0.0);
    float NdotL_specular = max(dot(N, L_specular), 0.0);

    float denom    = 4.0 * max(dot(N, V), 0.0) * NdotL_specular + 0.0001;
    vec3  specular = (NDF * G * F) / denom;

    return (kD * albedo / PI * NdotL_diffuse + specular * NdotL_specular) *
           radiance * attenuation;
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

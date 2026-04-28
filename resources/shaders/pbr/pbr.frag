#version 450
#include "preview/shader_tool_common.glsl"

layout(location = 0) in vec3 world_space_position;
layout(location = 1) in vec3 world_space_normal;
layout(location = 2) in vec2 texture_coordinates;

layout(location = 0) out vec4 fragment_color;

layout(set = 0, binding = 1) uniform sampler2D albedo_texture_map;
layout(set = 0, binding = 2) uniform sampler2D normal_texture_map;
layout(set = 0, binding = 3) uniform sampler2D roughness_texture_map;
layout(set = 0, binding = 4) uniform sampler2D ambient_occlusion_texture_map;
layout(set = 0, binding = 5) uniform sampler2D metallic_texture_map;
layout(set = 0, binding = 6) uniform sampler2D height_texture_map;

const float PI = 3.14159265359;

vec3 parallax_occlusion(vec3 view_direction_tangent, vec2 texture_coordinates)
{
    float height = texture(height_texture_map, texture_coordinates).r;
    float height_scale = 0.05;
    vec3 V = normalize(view_direction_tangent);

    vec2 offset_direction_in_uv_space = V.xy / max(V.z, 0.1);

    return texture_coordinates - offset_direction_in_uv_space * (height * height_scale);
}


vec3 construct_surface_normal(vec3 N)
{
    // remap from [0,1] to [-1,1].
    vec3 tangent_normal = texture(normal_texture_map, texture_coordinates).xyz * 2.0 - 1.0;

    // Q1/Q2 are the world-space directions you travel when you move one pixel in U/V.
    // st1/st2 are how much the UV coordinates shift for those same moves.
    // Solving the 2x2 system gives T (world-space U axis) and B (world-space V axis).
    vec3 Q1  = dFdx(world_space_position);
    vec3 Q2  = dFdy(world_space_position);
    vec2 st1 = dFdx(texture_coordinates);
    vec2 st2 = dFdy(texture_coordinates);

    vec3 T  = normalize(Q1 * st2.t - Q2 * st1.t);
    vec3 B  = cross(N, T);
    mat3 TBN = mat3(T, B, N);

    return normalize(TBN * tangent_normal);
}

float distribution_GGX(vec3 N, vec3 H, float a)
{
    float a2     = a * a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float nom    = a2;
    float denom  = (NdotH2 * (a2 - 1.0) + 1.0);
    denom        = PI * denom * denom;

    return nom / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float ggx1 = GeometrySchlickGGX(max(dot(N, V), 0.0), roughness);
    float ggx2 = GeometrySchlickGGX(max(dot(N, L), 0.0), roughness);
    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main()
{
    vec2 parallax_uv = parallax_occlusion(world_space_position - scene.camera_position.xyz, texture_coordinates);

    vec3 albedo   = pow(texture(albedo_texture_map, texture_coordinates).rgb, vec3(2.2));
    float metallic  = texture(metallic_texture_map, texture_coordinates).r;
    float roughness = texture(roughness_texture_map, texture_coordinates).r;
    float ambient_occlusion = texture(ambient_occlusion_texture_map, texture_coordinates).r;

    vec3 N = construct_surface_normal(normalize(world_space_normal));
    vec3 V = normalize(scene.camera_position.xyz - world_space_position);

    vec3 Lo = vec3(0.0);

    if ((scene.debug_flags & 1) != 0) {
        fragment_color = vec4(N * 0.5 + 0.5, 1.0);
        return;
    }


    for (int idx = 0; idx < scene.light_count; idx++)
    {
        Light light = scene.lights[idx];
        int light_type = int(light.spot_params.w);

        vec3 L;
        float attenuation = 1.0;

        if (light_type == 0) // point light
        {
            vec3 to_light = light.position.xyz - world_space_position;
            float distance = length(to_light);
            L = to_light / max(distance, 0.001);
            attenuation = 1.0 / (distance * distance);
        }
        else if (light_type == 2) // directional
        {
            L = -normalize(light.direction.xyz);
        }
        else if (light_type == 1)
        {
            // Point or spot
            vec3 to_light = light.position.xyz - world_space_position;
            float distance = length(to_light);
            vec3 light_direction = to_light / max(distance, 0.001);
            // Spot cone
            float cos_angle = dot(-light_direction, normalize(light.direction.xyz));
            float cos_inner = light.spot_params.x;
            float cos_outer = light.spot_params.y;
            float spot_factor = clamp((cos_angle - cos_outer) / max(cos_inner - cos_outer, 0.001), 0.0, 1.0);
            attenuation *= spot_factor;
        }

        vec3 H  = normalize(L + V);
        vec3 F0 = mix(vec3(0.04), albedo, metallic);

        float NDF = distribution_GGX(N, H, roughness);
        float G   = GeometrySmith(N, V, L, roughness);
        vec3  F   = fresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 kS = F;
        vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

        float denom  = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
        vec3 specular = (NDF * G * F) / denom;

        float NdotL  = max(dot(N, L), 0.0);
        vec3 radiance = light.color_intensity.rgb * light.color_intensity.a;

        Lo += (kD * albedo / PI + specular) * radiance * attenuation * NdotL;
    }

    // Ambient — use param_color[1] as a tunable ambient scale (default vec4(0) → falls back to 0.03)
    vec3 ambient_color = scene.param_color[1].rgb;
    if (dot(ambient_color, ambient_color) < 0.0001)
        ambient_color = vec3(0.03);
    vec3 ambient = ambient_color * albedo * ambient_occlusion;
    Lo += ambient;

    // Reinhard tone mapping
    Lo = Lo / (Lo + vec3(1.0));

    // Gamma correction
    Lo = pow(Lo, vec3(1.0 / 2.2));

    fragment_color = vec4(Lo, 1.0);
}

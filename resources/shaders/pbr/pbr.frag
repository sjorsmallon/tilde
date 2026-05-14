#version 450
#include "preview/shader_tool_common.glsl"

#define DEBUG_FLAG_RENDER_NORMALS     (1 << 0)
#define DEBUG_FLAG_RENDER_UV          (1 << 1)
#define DEBUG_FLAG_RENDER_PARALLAX_UV (1 << 2)

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


// tangent space is a per-fragment local coordinate space.
// it is called tangent because T and B are _TANGENT_  to the surface at that point.
// every fragment has its own. as you walk across a curved surface, the tangent space rotates to stay aligned with the surface normal.

vec2 parallax_mapping(vec3 view_direction_tangent, vec2 uv)
{
    float height = texture(height_texture_map, uv).r;
    float height_scale = 0.005;
    vec3 V = normalize(view_direction_tangent);

    vec2 offset_direction_in_uv_space = V.xy / max(V.z, 0.1);

    return uv - offset_direction_in_uv_space * (height * height_scale);
}

vec2 parallax_occlusion(vec3 view_direction_tangent, vec2 uv)
{
    const float height_scale = 0.01;

    // why the double bounds on layer count?
    //  with too few layers, the intersection is very inaccurate and can cause swimming and other artifacts. 
     //with too many layers, performance suffers. this range was found through trial and error to give good results 
     // across a wide range of angles while keeping the cost reasonable. (this is bullshit but whatever)
    const float minimum_layer_count = 8.0;
    const float maximum_layer_count = 32.0;

    vec3 view_direction = normalize(view_direction_tangent);

    // More layers at grazing angles: the ray slides far across UV per unit of
    // depth there, so we need finer steps to catch the intersection.
    float layer_count      = mix(maximum_layer_count, minimum_layer_count, abs(view_direction.z));
    float layer_depth_step = 1.0 / layer_count;

    // view_direction.xy / view_direction.z = "UV slide per unit of depth gained" (similar triangles).
    // Times height_scale = total UV offset for a full traversal from depth 0 to depth 1.
    // Times layer_depth_step (= 1 / layer_count) = the UV nudge per marching step.
    vec2 slope_uv_per_depth         = view_direction.xy / view_direction.z;
    vec2 total_uv_offset_full_depth = slope_uv_per_depth * height_scale;
    vec2 uv_step                    = total_uv_offset_full_depth * layer_depth_step;

    // Start at the original UV, sitting on the polygon surface (depth 0).
    vec2  current_uv            = uv;
    float current_ray_depth     = 0.0;
    float current_surface_depth = texture(height_texture_map, current_uv).r;

    // Walk the ray deeper into the surface until it punches through the heightfield.
    // Each iteration: take one UV nudge, go one layer deeper, resample the heightmap.
    while (current_ray_depth < current_surface_depth)
    {
        current_uv            -= uv_step;
        current_ray_depth     += layer_depth_step;
        current_surface_depth  = texture(height_texture_map, current_uv).r;
    }

    // The true intersection lies between the previous step (still above surface)
    // and the current step (already below). Linearly interpolate between the two UVs
    // by how far each one missed the surface, to smooth out the discrete layer banding.
    vec2  previous_uv            = current_uv + uv_step;
    float previous_surface_depth = texture(height_texture_map, previous_uv).r;
    float previous_ray_depth     = current_ray_depth - layer_depth_step;

    float overshoot            = current_ray_depth - current_surface_depth;     // how far past the surface "current" is
    float undershoot           = previous_surface_depth - previous_ray_depth;   // how far above the surface "previous" was
    float interpolation_weight = overshoot / (overshoot + undershoot);

    return mix(current_uv, previous_uv, interpolation_weight);
}

vec3 construct_surface_normal(vec3 N, vec2 uv)
{
    // remap from [0,1] to [-1,1].
    vec3 tangent_normal = texture(normal_texture_map, uv).xyz * 2.0 - 1.0;

    // Q1/Q2 are the world-space directions you travel when you move one pixel in U/V.
    // st1/st2 are how much the UV coordinates shift for those same moves.
    // Solving the 2x2 system gives T (world-space U axis) and B (world-space V axis).
    vec3 Q1  = dFdx(world_space_position);
    vec3 Q2  = dFdy(world_space_position);
    vec2 st1 = dFdx(uv);
    vec2 st2 = dFdy(uv);

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

// Frostbite-style windowed inverse-square falloff (Lagarde 2014).
// True 1/d² in the interior, smoothly cut to zero at `range` so lights are cullable
// and avoid the d→0 singularity. Intensity is expected in lumens-ish units.
float distance_attenuation(float squared_distance, float range)
{
    float inv_sqr_radius = 1.0 / max(range * range, 0.0001);
    float factor = squared_distance * inv_sqr_radius;
    float smooth_factor = clamp(1.0 - factor * factor, 0.0, 1.0);
    float window = smooth_factor * smooth_factor;
    return window / max(squared_distance, 0.01 * 0.01);
}

void main()
{
    vec2 uv = texture_coordinates;
    vec3 V = normalize(scene.camera_position.xyz - world_space_position);

   // @NOTE(SMIA): we need a better solution for this TBN matrix but this will do for now.
    vec3 Q1  = dFdx(world_space_position);
    vec3 Q2  = dFdy(world_space_position);
    vec2 st1 = dFdx(uv);
    vec2 st2 = dFdy(uv);


    vec3 T  = normalize(Q1 * st2.t - Q2 * st1.t);
    vec3 B  = cross(world_space_normal, T);
    mat3 TBN = mat3(T, B, world_space_normal);

    vec3 view_dir_in_fragment_tangent_space = normalize(transpose(TBN) * V);

    vec2 parallax_uv = parallax_occlusion(view_dir_in_fragment_tangent_space, uv);
    uv = parallax_uv;
    // vec2 parallax_uv = parallax_mapping(view_dir_in_fragment_tangent_space, uv);
    // uv = parallax_uv;
   

    vec3 N = construct_surface_normal(normalize(world_space_normal), uv);

    vec3 albedo   = pow(texture(albedo_texture_map, uv).rgb, vec3(2.2));
    float metallic  = texture(metallic_texture_map, uv).r;
    float roughness = texture(roughness_texture_map, uv).r;
    float ambient_occlusion = texture(ambient_occlusion_texture_map, uv).r;


    vec3 Lo = vec3(0.0);

    if ((scene.debug_flags & DEBUG_FLAG_RENDER_NORMALS) != 0)
    {
        fragment_color = vec4(N * 0.5 + 0.5, 1.0);
        return;
    }

    if ((scene.debug_flags & DEBUG_FLAG_RENDER_UV) != 0)
    {
        fragment_color = vec4(uv, 0.0, 1.0);
        return;
    }
    if ((scene.debug_flags & DEBUG_FLAG_RENDER_PARALLAX_UV) != 0)
    {
        fragment_color = vec4(texture(albedo_texture_map, parallax_uv).rgb, 1.0);
        return;
    }

    for (int idx = 0; idx < scene.light_count; idx++)
    {
        Light light = scene.lights[idx];
        int light_type = int(light.spot_params.w);

        vec3 L;
        float attenuation = 1.0;

        if (light_type == 0) // point
        {
            vec3 to_light = light.position.xyz - world_space_position;
            float squared_distance = dot(to_light, to_light);
            L = to_light / max(sqrt(squared_distance), 0.001);
            attenuation = distance_attenuation(squared_distance, light.spot_params.z);
        }
        else if (light_type == 2) // directional
        {
            L = -normalize(light.direction.xyz);
        }
        else if (light_type == 1) // spot
        {
            vec3 to_light = light.position.xyz - world_space_position;
            float squared_distance = dot(to_light, to_light);
            L = to_light / max(sqrt(squared_distance), 0.001);
            float cos_angle = dot(-L, normalize(light.direction.xyz));
            float cos_inner = light.spot_params.x;
            float cos_outer = light.spot_params.y;
            float spot_factor = clamp((cos_angle - cos_outer) / max(cos_inner - cos_outer, 0.001), 0.0, 1.0);
            attenuation = spot_factor * distance_attenuation(squared_distance, light.spot_params.z);
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
    {
        ambient_color = vec3(0.03);
    }
    vec3 ambient = ambient_color * albedo * ambient_occlusion;
    Lo += ambient;

    // Reinhard tone mapping
    Lo = Lo / (Lo + vec3(1.0));
    // Gamma correction
    Lo = pow(Lo, vec3(1.0 / 2.2));

    fragment_color = vec4(Lo, 1.0);
}

#version 450
#include "preview/shader_tool_common.glsl"

#define DEBUG_FLAG_RENDER_NORMALS     (1 << 0)
#define DEBUG_FLAG_RENDER_UV          (1 << 1)
#define DEBUG_FLAG_RENDER_PARALLAX_UV (1 << 2)

layout(location = 0) in vec3 world_space_position;
layout(location = 1) in vec3 world_space_normal;
layout(location = 2) in vec2 texture_coordinates;

layout(location = 0) out vec4 fragment_color;

// FOUR samplers, not six: occlusion, roughness and metallic are single-channel
// and ride one RGB texture in glTF's order (lighting_def.md decision G).
layout(set = 0, binding = 1) uniform sampler2D albedo_texture_map;
layout(set = 0, binding = 2) uniform sampler2D normal_texture_map;
layout(set = 0, binding = 3) uniform sampler2D orm_texture_map;
layout(set = 0, binding = 4) uniform sampler2D height_texture_map;

#include "pbr_lighting.glsl"

void main()
{
    vec2 uv = texture_coordinates;
    vec3 V = normalize(scene.camera_position.xyz - world_space_position);
    vec3 N = normalize(world_space_normal);

    mat3 tangent_frame = cotangent_frame(N, world_space_position, uv);
    vec2 parallax_uv   = parallax_occlusion(
        height_texture_map, view_direction_in_tangent_space(tangent_frame, V), uv);
    uv = parallax_uv;

    N = apply_normal_map(tangent_frame, texture(normal_texture_map, uv).xyz * 2.0 - 1.0);

    // Albedo is uploaded SRGB, so the hardware sampler decodes it -- the manual
    // pow that used to be here was the input half of the same double-encode the
    // trailing one was (lighting_def.md decision F). ORM is DATA and is uploaded
    // UNORM, which is why it can be read raw beside it.
    vec3 albedo = texture(albedo_texture_map, uv).rgb;

    vec3  orm               = texture(orm_texture_map, uv).rgb;
    float ambient_occlusion = orm.r;
    float roughness         = orm.g;
    float metallic          = orm.b;

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

    vec3 Lo = vec3(0.0);
    for (int idx = 0; idx < scene.light_count; idx++)
    {
        Light         light   = scene.lights[idx];
        Light_Arrival arrival = light_arrival(light, world_space_position);

        Lo += shade_direct(N, V, arrival.direction, albedo, roughness, metallic,
                           light.radiance.rgb, arrival.attenuation,
                           light.direction.w, arrival.distance);
    }

    // Ambient -- use param_color[1] as a tunable ambient scale (default vec4(0) -> falls back to 0.03)
    vec3 ambient_color = scene.param_color[1].rgb;
    if (dot(ambient_color, ambient_color) < 0.0001)
    {
        ambient_color = vec3(0.03);
    }
    Lo += ambient_color * albedo * ambient_occlusion;

    // Linear and unmapped: the tonemap pass owns the curve (decision J) and the
    // sRGB attachment owns the encode (decision F), so the preview and the game
    // get the same response out of the same place.
    fragment_color = vec4(Lo, 1.0);
}

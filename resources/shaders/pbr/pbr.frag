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

    vec2 parallax_uv = parallax_occlusion(
        height_texture_map, view_direction_in_tangent_space(N, V, world_space_position, uv), uv);
    uv = parallax_uv;

    N = apply_normal_map(N, texture(normal_texture_map, uv).xyz * 2.0 - 1.0,
                         world_space_position, uv);

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
        Light light = scene.lights[idx];
        vec4  arrival = light_arrival(int(light.spot_params.w), light.position.xyz,
                                      light.direction.xyz, light.spot_params.x,
                                      light.spot_params.y, light.spot_params.z,
                                      world_space_position);

        Lo += shade_direct(N, V, arrival.xyz, albedo, roughness, metallic,
                           light.radiance.rgb, arrival.w);
    }

    // Ambient -- use param_color[1] as a tunable ambient scale (default vec4(0) -> falls back to 0.03)
    vec3 ambient_color = scene.param_color[1].rgb;
    if (dot(ambient_color, ambient_color) < 0.0001)
    {
        ambient_color = vec3(0.03);
    }
    Lo += ambient_color * albedo * ambient_occlusion;

    // Reinhard tone mapping. The sRGB encode that used to follow it is GONE --
    // the attachment owns that now (lighting_def.md decision F), so this shader
    // writes linear. Deleting the encode must not take the tonemap with it: what
    // operator runs here, and whether it belongs in a post-process pass at all,
    // is a separate open question.
    Lo = Lo / (Lo + vec3(1.0));

    fragment_color = vec4(Lo, 1.0);
}

#ifndef DEBUG_CHANNELS_GLSL
#define DEBUG_CHANNELS_GLSL

// r_debug_channel's views that REPLACE the shaded result. The two that tint it
// (shadow_cascades, reflection_capture) stay on the shaded colour's way out.

#include "light_gather.glsl"
#include "reflection.glsl"

#define DEBUG_FLAGS_REPLACING_SHADING                                         \
    (DEBUG_FLAGS_SHOWING_VISIBILITY | DEBUG_FLAG_RENDER_DIRECT_LIGHT |        \
     DEBUG_FLAG_RENDER_BAKED_LIGHT | DEBUG_FLAG_RENDER_NORMALS |              \
     DEBUG_FLAG_RENDER_UV | DEBUG_FLAG_RENDER_PARALLAX_UV | DEBUG_FLAG_RENDER_REFLECTION)

bool showing_debug_channel()
{
    return (scene.debug_flags & DEBUG_FLAGS_REPLACING_SHADING) != 0;
}

vec4 debug_channel_color(Surface surface, vec3 geometric_normal, vec3 world_position, vec3 V,
                         sampler2D albedo_map)
{
    if ((scene.debug_flags & DEBUG_FLAGS_SHOWING_VISIBILITY) != 0)
        return shadow_visibility_debug_color(world_position, geometric_normal);

    // Each half of the lighting alone and before albedo, off the geometric normal.
    if ((scene.debug_flags & DEBUG_FLAG_RENDER_DIRECT_LIGHT) != 0)
    {
        Surface white;
        white.albedo    = vec3(1.0);
        white.normal    = geometric_normal;
        white.geometric_normal = geometric_normal;
        white.uv        = surface.uv;
        white.roughness = 1.0;
        white.metallic  = 0.0;
        white.occlusion = 1.0;
        white.emissive  = vec3(0.0);
        return vec4(gather_direct_light(LOOK_LAMBERT, white, world_position, V).rgb, 1.0);
    }
    if ((scene.debug_flags & DEBUG_FLAG_RENDER_BAKED_LIGHT) != 0)
        return vec4(baked_irradiance(world_position, geometric_normal), 1.0);

    if ((scene.debug_flags & DEBUG_FLAG_RENDER_NORMALS) != 0)
        return vec4(surface.normal * 0.5 + 0.5, 1.0);
    if ((scene.debug_flags & DEBUG_FLAG_RENDER_UV) != 0)
        return vec4(surface.uv, 0.0, 1.0);
    if ((scene.debug_flags & DEBUG_FLAG_RENDER_PARALLAX_UV) != 0)
        return vec4(texture(albedo_map, surface.uv).rgb, 1.0);

    return reflection_debug_color(world_position, surface.normal, V, surface.roughness);
}

#endif // DEBUG_CHANNELS_GLSL

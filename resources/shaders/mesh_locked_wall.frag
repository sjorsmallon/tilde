#version 450

// A locked wall: lambert-lit, with a keyhole cut through it in the draw's clock-wipe frame
// (geometry_wipe_frame). The hole opens on the wipe fraction, so a wall whose owner has a
// wipe_timer grows its keyhole until the timer's Elapsed disables the owner and the wall is
// gone. The clock wipe's own radial discard is replaced, not composed: the keyhole IS this
// wall's timed shape.

#include "scene.glsl"
#include "surface.glsl"
#include "surface_normal.glsl"
#include "light_gather.glsl"
#include "debug_channels.glsl"
#include "keyhole.glsl"
#include "reveal.glsl"

layout(location = 0) in vec3       fragWorldNormal;
layout(location = 1) in vec3       fragColor;
layout(location = 2) in vec2       fragUV;
layout(location = 3) in flat float fragAlpha;
layout(location = 6) in vec3       fragWorldPosition;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outSurfaceNormal;

layout(set = 0, binding = 0) uniform sampler2D albedo;
layout(set = 0, binding = 4) uniform sampler2D emissiveMap;

const int LOOK = LOOK_LAMBERT;

const float KEYHOLE_RIM_DARKNESS = 0.15;

Surface read_surface(vec3 geometric_normal)
{
    Surface surface;
    surface.geometric_normal = geometric_normal;
    surface.uv        = fragUV;
    surface.normal    = geometric_normal;
    surface.occlusion = 1.0;
    surface.roughness = 0.0;
    surface.metallic  = 0.0;
    surface.albedo    = texture(albedo, surface.uv).rgb * fragColor;
    surface.emissive  = texture(emissiveMap, surface.uv).rgb;
    return surface;
}

void main() {
    float hole = keyhole_at(fragWorldPosition);
    if (hole < 0.0)
        discard;
    discard_outside_reveal(fragWorldPosition);

    vec3 geometric_normal = normalize(fragWorldNormal) * (gl_FrontFacing ? 1.0 : -1.0);
    vec3 V                = normalize(scene.camera_position.xyz - fragWorldPosition);

    Surface surface  = read_surface(geometric_normal);
    outSurfaceNormal = store_surface_normal(geometric_normal);

    if (showing_debug_channel())
    {
        outColor = debug_channel_color(surface, geometric_normal, fragWorldPosition, V, albedo);
        return;
    }

    float solid_ink;
    vec3  lit        = light_surface(LOOK, surface, fragWorldPosition, V, solid_ink);
    outSurfaceNormal = store_solid_ink(outSurfaceNormal, solid_ink);

    float rim = 1.0 - smoothstep(0.0, KEYHOLE_RIM, hole);
    lit       = mix(lit, lit * KEYHOLE_RIM_DARKNESS, rim);

    outColor     = vec4(lit, fragAlpha);
    outColor.rgb = reveal_rim(outColor.rgb, fragWorldPosition, geometric_normal);
}

#ifndef DIRECT_LIGHT_GLSL
#define DIRECT_LIGHT_GLSL

// The RUNTIME half of a light's occlusion (lighting_def.md gate 9, decision K):
// the one sampler2DArrayShadow every shadow map is a layer of, the receiver's
// visibility test against it, and the analytic tail loop the non-PBR fragment
// shaders share -- lit's Lambert arm, grid and blend -- so the three cannot
// disagree about which lights a surface sees or how they are shadowed.
//
// Deliberately NOT reached through light_arrival.glsl: the shader tool's
// preview vertex shader includes that, and a sampler at set 3 is a binding it
// does not have.

#ifndef PI
#define PI 3.14159265359
#endif

#include "scene.glsl"

layout(set = 3, binding = 9) uniform sampler2DArrayShadow shadowMaps;

// Which layer of the pool this light's map is, or -1. It rides radiance.w
// because that was the one component the Light struct had left; both copies of
// a Mixed light carry the same number.
#define LIGHT_SHADOW_LAYER(light) (int((light).radiance.w))

// V for one light at one point: 1.0 where the light has no map, or the point is
// outside it. The receiver is pushed along its normal by a distance measured in
// the map's own texels at this range, scaled by the SINE of the incidence
// angle so a grazing face gets the whole offset and a face-on one almost none;
// the slope-scaled half of the bias is the shadow pipeline's rasterizer state.
// The kernel is a square of hardware-compared taps, radius
// scene.shadow_settings.y in texels -- the fixed minimum kernel the PCSS search
// will widen (gate 9), which anti-aliases the map and is not softness.
float shadow_visibility(Light light, Light_Arrival arrival, vec3 world_position, vec3 N)
{
    int layer = LIGHT_SHADOW_LAYER(light);
    if (layer < 0)
        return 1.0;

    float texel_size = scene.shadow_layers[layer].x * arrival.distance;
    float cos_theta  = clamp(dot(N, arrival.direction), 0.0, 1.0);
    float sin_theta  = sqrt(1.0 - cos_theta * cos_theta);
    vec3  receiver   = world_position + N * (scene.shadow_settings.x * texel_size * sin_theta);

    vec4 clip = scene.shadow_view_projection[layer] * vec4(receiver, 1.0);
    if (clip.w <= 0.0)
        return 1.0;
    vec3 ndc = clip.xyz / clip.w;
    if (ndc.z >= 1.0)
        return 1.0;

    vec2 uv       = ndc.xy * 0.5 + 0.5;
    vec2 texel_uv = 1.0 / vec2(textureSize(shadowMaps, 0).xy);
    int  radius   = int(scene.shadow_settings.y);

    float lit = 0.0;
    for (int y = -radius; y <= radius; ++y)
        for (int x = -radius; x <= radius; ++x)
            lit += texture(shadowMaps, vec4(uv + vec2(x, y) * texel_uv, float(layer), ndc.z));

    float taps = float((2 * radius + 1) * (2 * radius + 1));
    return lit / taps;
}

// Lambert over the tail -- the lights no bake saw, plus a second copy of every
// Mixed one -- each through its shadow map. A lightmapped surface SKIPS the
// Mixed copy, having shaded that light through its chart (lightmap.glsl).
vec3 analytic_tail_diffuse(vec3 N, vec3 world_position)
{
    vec3 diffuse = vec3(0.0);
    for (int index = scene.baked_light_count; index < scene.light_count; ++index)
    {
        Light light = scene.lights[index];
#ifdef LIGHTMAP
        if (LIGHT_BAKED_SLOT(light) >= 0)
            continue;
#endif
        Light_Arrival arrival    = light_arrival(light, world_position);
        float         visibility = shadow_visibility(light, arrival, world_position, N);
        diffuse += light.radiance.rgb *
                   (arrival.attenuation * visibility * max(dot(N, arrival.direction), 0.0)) / PI;
    }
    return diffuse;
}

// r_debug_channel = shadow_visibility: V alone, white and black, for the
// shadowed light nearest the camera. Magenta where no light in the pass has a
// map, so "nothing to show" cannot be mistaken for "everything lit".
vec4 shadow_visibility_debug_color(vec3 world_position, vec3 N)
{
    int index = scene.debug_shadow_light;
    if (index < 0 || index >= scene.light_count)
        return vec4(1.0, 0.0, 1.0, 1.0);

    Light         light      = scene.lights[index];
    Light_Arrival arrival    = light_arrival(light, world_position);
    float         visibility = shadow_visibility(light, arrival, world_position, N);
    return vec4(vec3(visibility), 1.0);
}

#endif // DIRECT_LIGHT_GLSL

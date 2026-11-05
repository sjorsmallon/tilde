#ifndef SHADOW_MARCH_GLSL
#define SHADOW_MARCH_GLSL

// A player's drawn shadow body (projector_plan.md ss1b): the volume the collision reads is the hull box's, so
// its planes only say where to look, and the body itself is read from the light's shadow map, which the
// skinned player is drawn into. shadow_body.frag includes this after shadow_chord.glsl; it reaches the pools
// through direct_light.glsl, which the body pass's pass set carries.

#include "direct_light.glsl"
#include "shadow_chord.glsl"

// projector_mask.hpp's PROJECTOR_MASK_DARK_THRESHOLD: a picture cell this dark is solid, so it is dark here too.
const float PROJECTOR_MASK_DARK_THRESHOLD = 0.25;

// Whether `light`'s shadow map says `point` is lit (1.0) or dark (0.0) for a point in the AIR: the compare at the
// point pushed toward the light by the layer's bias, as a surface is, with no normal offset to push by. 1.0
// where the light has no map, the point is behind it or past the last cascade. A projector's picture darkens
// where its cell is solid.
float shadow_map_visibility_at(Light light, vec3 point)
{
    int layer = LIGHT_SHADOW_LAYER(light);
    if (layer < 0)
        return 1.0;
    int   type         = int(light.spot_params.w);
    bool  directional  = type == LIGHT_TYPE_DIRECTIONAL;
    vec3  toward_light = directional ? -normalize(light.direction.xyz) : light.position.xyz - point;
    float distance     = directional ? 1.0 : max(length(toward_light), 1e-4);
    if (!directional)
        toward_light /= distance;
    if (type == LIGHT_TYPE_POINT)
        layer += point_shadow_face(light, point);
    else if (directional)
    {
        Cascade_Pick pick = pick_shadow_cascade(point);
        if (pick.index < 0)
            return 1.0;
        layer += pick.index;
    }
    vec3 receiver = point + toward_light * (scene.shadow_layers[layer].y * distance);
    vec4 clip     = scene.shadow_view_projection[layer] * vec4(receiver, 1.0);
    if (clip.w <= 0.0)
        return 1.0;
    vec3 ndc = clip.xyz / clip.w;
    if (ndc.z >= 1.0)
        return 1.0;
    float visibility = shadow_compare(layer, ndc.xy * 0.5 + 0.5, ndc.z);
    vec3  picture    = projector_mask(light, point);
    if (dot(picture, vec3(1.0 / 3.0)) < PROJECTOR_MASK_DARK_THRESHOLD)
        visibility = 0.0;
    return visibility;
}

// Jorge Jimenez's interleaved gradient noise: one value in [0, 1) per pixel with little low-frequency structure.
float interleaved_gradient_noise(vec2 pixel)
{
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

// The four numbers drawn_shadow_lengths_from_planes answers, read from `light`'s shadow map: the chord through
// volume `volume` is walked in scene.shadow_volume_fill.y steps, the start jittered by a pixel's noise, and a
// step whose point the map calls dark counts. x the dark length in front of the surface at `surface_t`, y the
// same (the walk stops at the surface, nothing is drawn through it), z and w the first and last dark point for
// a ring to sit on. A part thinner than one step is dithered rather than dropped; buy it down with steps.
vec4 drawn_shadow_lengths_from_shadow_map(int volume, Light light, vec3 origin, vec3 ray, float surface_t)
{
    float enter = 0.0;
    float leave = EMPTY_ENTER;
    clip_to_shadow_volume(enter, leave, volume, origin, ray);
    leave = min(leave, surface_t);
    if (leave <= enter)
        return vec4(0.0, 0.0, EMPTY_ENTER, 0.0);

    int   steps = max(int(scene.shadow_volume_fill.y), 1);
    float step  = (leave - enter) / float(steps);
    float t     = enter + step * interleaved_gradient_noise(gl_FragCoord.xy);
    vec4  drawn = vec4(0.0, 0.0, EMPTY_ENTER, 0.0);
    for (int index = 0; index < steps; ++index, t += step)
    {
        if (shadow_map_visibility_at(light, origin + ray * t) >= 0.5)
            continue;
        drawn.x += step;
        drawn.z  = min(drawn.z, t);
        drawn.w  = max(drawn.w, t);
    }
    drawn.y = drawn.x;
    return drawn;
}

#endif // SHADOW_MARCH_GLSL

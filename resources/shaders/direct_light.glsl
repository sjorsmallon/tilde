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
#include "probes.glsl"

layout(set = 3, binding = 9) uniform sampler2DArrayShadow shadowMaps;
// The same pool through a non-compare nearest sampler: the PCSS blocker search reads depths.
layout(set = 3, binding = 11) uniform sampler2DArray shadowDepths;

#define PCSS_TAP_COUNT 16
const vec2 PCSS_POISSON_DISC[PCSS_TAP_COUNT] = vec2[](
    vec2(-0.94201624, -0.39906216), vec2( 0.94558609, -0.76890725),
    vec2(-0.09418410, -0.92938870), vec2( 0.34495938,  0.29387760),
    vec2(-0.91588581,  0.45771432), vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543,  0.27676845), vec2( 0.97484398,  0.75648379),
    vec2( 0.44323325, -0.97511554), vec2( 0.53742981, -0.47373420),
    vec2(-0.26496911, -0.41893023), vec2( 0.79197514,  0.19090188),
    vec2(-0.24188840,  0.99706507), vec2(-0.81409955,  0.91437590),
    vec2( 0.19984126,  0.78641367), vec2( 0.14383161, -0.14100790));

// Interleaved gradient noise (Jimenez 2014) as a per-pixel rotation of the disc.
mat2 pcss_disc_rotation()
{
    float noise = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float angle = noise * 2.0 * PI;
    float s = sin(angle);
    float c = cos(angle);
    return mat2(c, s, -s, c);
}

// The radius the last sample_shadow_layer filtered at, for r_debug_channel = shadow_penumbra.
float g_shadow_penumbra_texels = 0.0;

// Which layer of the pool this light's map is, or -1. It rides radiance.w
// because that was the one component the Light struct had left; both copies of
// a Mixed light carry the same number.
#define LIGHT_SHADOW_LAYER(light) (int((light).radiance.w))

// V through ONE layer of the pool at one point: 1.0 where the point is outside
// the map. The receiver is pushed along its normal by a distance measured in
// the map's own texels at this range, scaled by the SINE of the incidence
// angle so a grazing face gets the whole offset and a face-on one almost none,
// and then TOWARD the light by a fixed fraction of a texel -- the face-on case
// gets no slope bias either, and a float depth buffer makes the rasterizer's
// constant bias a few ulps, so a floor under the sun would otherwise compare
// against its own depth and dither. The slope-scaled half of the bias is the
// shadow pipeline's rasterizer state.
// The kernel is a square of hardware-compared taps, radius
// scene.shadow_settings.y in texels -- the fixed minimum kernel, which
// anti-aliases the map and is not softness. A light with a source_radius
// widens it by PCSS: a Poisson blocker search over the region the emitter could
// shadow the receiver from, then a Poisson PCF filter over the penumbra the
// average blocker distance implies, both capped by scene.shadow_pcss.y and
// never under the minimum. A radius of zero is the square kernel, bit for bit.
float sample_shadow_layer(int layer, Light_Arrival arrival, vec3 world_position, vec3 N,
                          float source_radius)
{
    float texel_size = scene.shadow_layers[layer].x * arrival.distance;
    float cos_theta  = clamp(dot(N, arrival.direction), 0.0, 1.0);
    float sin_theta  = sqrt(1.0 - cos_theta * cos_theta);
    vec3  receiver   = world_position + N * (scene.shadow_settings.x * texel_size * sin_theta) +
                       arrival.direction * (scene.shadow_layers[layer].y * arrival.distance);

    vec4 clip = scene.shadow_view_projection[layer] * vec4(receiver, 1.0);
    if (clip.w <= 0.0)
        return 1.0;
    vec3 ndc = clip.xyz / clip.w;
    if (ndc.z >= 1.0)
        return 1.0;

    vec2 uv       = ndc.xy * 0.5 + 0.5;
    vec2 texel_uv = 1.0 / vec2(textureSize(shadowMaps, 0).xy);
    int  radius   = int(scene.shadow_settings.y);

    g_shadow_penumbra_texels = float(radius);
    if (scene.shadow_pcss.x > 0.0 && source_radius > 0.0)
    {
        float near_plane   = scene.shadow_layers[layer].z;
        float far_plane    = scene.shadow_layers[layer].w;
        bool  orthographic = near_plane <= 0.0;
        float texel_size_at_unit_distance = scene.shadow_layers[layer].x;
        float receiver_depth = shadow_linear_depth(ndc.z, near_plane, far_plane, orthographic);
        float max_radius     = max(scene.shadow_pcss.y, float(radius));

        float search_radius = clamp(shadow_penumbra_texels(source_radius, receiver_depth, near_plane,
                                                           texel_size_at_unit_distance, orthographic),
                                    float(radius), max_radius);
        mat2 rotation = pcss_disc_rotation();

        float blocker_depth_sum = 0.0;
        int   blocker_count     = 0;
        for (int tap = 0; tap < PCSS_TAP_COUNT; ++tap)
        {
            vec2  offset = rotation * PCSS_POISSON_DISC[tap] * search_radius * texel_uv;
            float depth  = texture(shadowDepths, vec3(uv + offset, float(layer))).r;
            if (depth < ndc.z)
            {
                blocker_depth_sum += depth;
                ++blocker_count;
            }
        }
        if (blocker_count == 0)
            return 1.0;

        float blocker_depth = shadow_linear_depth(blocker_depth_sum / float(blocker_count),
                                                  near_plane, far_plane, orthographic);
        float penumbra = clamp(shadow_penumbra_texels(source_radius, receiver_depth, blocker_depth,
                                                      texel_size_at_unit_distance, orthographic),
                               float(radius), max_radius);
        g_shadow_penumbra_texels = penumbra;

        float lit = 0.0;
        for (int tap = 0; tap < PCSS_TAP_COUNT; ++tap)
        {
            vec2 offset = rotation * PCSS_POISSON_DISC[tap] * penumbra * texel_uv;
            lit += texture(shadowMaps, vec4(uv + offset, float(layer), ndc.z));
        }
        return lit / float(PCSS_TAP_COUNT);
    }

    float lit = 0.0;
    for (int y = -radius; y <= radius; ++y)
        for (int x = -radius; x <= radius; ++x)
            lit += texture(shadowMaps, vec4(uv + vec2(x, y) * texel_uv, float(layer), ndc.z));

    float taps = float((2 * radius + 1) * (2 * radius + 1));
    return lit / taps;
}

// Which of the sun's cascades a point falls in, by its VIEW depth, and how far
// into the blend band toward the next it sits (0 outside the band). index is
// -1 past the last cascade, or when no sun claimed any this frame.
struct Cascade_Pick {
    int   index;
    float blend;
};

Cascade_Pick pick_shadow_cascade(vec3 world_position)
{
    Cascade_Pick pick;
    pick.index = -1;
    pick.blend = 0.0;

    int count = int(scene.shadow_settings.w);
    if (count <= 0)
        return pick;

    float depth = dot(world_position - scene.camera_position.xyz, scene.camera_forward.xyz);
    int   index = 0;
    while (index < count - 1 && depth > scene.shadow_cascade_splits[index])
        ++index;
    if (depth > scene.shadow_cascade_splits[count - 1])
        return pick;

    pick.index = index;
    if (index + 1 < count)
    {
        float split = scene.shadow_cascade_splits[index];
        float band  = split * scene.shadow_settings.z;
        if (band > 0.0)
            pick.blend = clamp((depth - (split - band)) / band, 0.0, 1.0);
    }
    return pick;
}

// Which of a point light's six faces a point falls in -- +X, -X, +Y, -Y, +Z,
// -Z by the major axis of the light-to-point vector, the order
// shared::point_shadow_faces lays the layers out in. Each face's map reaches a
// few texels past the 45-degree edge, so the kernel at a seam stays inside it.
int point_shadow_face(Light light, vec3 world_position)
{
    vec3 d = world_position - light.position.xyz;
    vec3 m = abs(d);
    if (m.x >= m.y && m.x >= m.z)
        return d.x >= 0.0 ? 0 : 1;
    if (m.y >= m.z)
        return d.y >= 0.0 ? 2 : 3;
    return d.z >= 0.0 ? 4 : 5;
}

// V for one light at one point: 1.0 where the light has no map. A spot light
// is one layer; a point light is six, the face picked by major axis; the sun
// is a run of cascade layers starting at the one its Light names, picked by
// view depth and cross-faded across the band at each seam so the split is not
// a visible line.
float shadow_visibility(Light light, Light_Arrival arrival, vec3 world_position, vec3 N)
{
    int layer = LIGHT_SHADOW_LAYER(light);
    if (layer < 0)
        return 1.0;
    int type = int(light.spot_params.w);
    float source_radius = light.direction.w;
    if (type == LIGHT_TYPE_POINT)
        return sample_shadow_layer(layer + point_shadow_face(light, world_position), arrival,
                                   world_position, N, source_radius);
    if (type != LIGHT_TYPE_DIRECTIONAL)
        return sample_shadow_layer(layer, arrival, world_position, N, source_radius);

    Cascade_Pick pick = pick_shadow_cascade(world_position);
    if (pick.index < 0)
        return 1.0;
    float visibility = sample_shadow_layer(layer + pick.index, arrival, world_position, N, source_radius);
    if (pick.blend > 0.0)
        visibility = mix(visibility,
                         sample_shadow_layer(layer + pick.index + 1, arrival, world_position, N,
                                             source_radius),
                         pick.blend);
    return visibility;
}

// r_debug_channel = shadow_cascades: the shaded result washed with its
// cascade's colour -- red, green, blue, yellow in order, blended across each
// seam exactly as the visibility is -- and left alone past the last cascade or
// where no sun is shadowed. Every fragment shader routes its final colour
// through this, so a wrong split is visible on every surface.
vec4 shadow_cascade_debug(vec4 shaded, vec3 world_position)
{
    if ((scene.debug_flags & DEBUG_FLAG_RENDER_SHADOW_CASCADES) == 0)
        return shaded;

    const vec3 tints[MAX_SHADOW_CASCADES] =
        vec3[](vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, 1.0), vec3(1.0, 1.0, 0.0));

    Cascade_Pick pick = pick_shadow_cascade(world_position);
    if (pick.index < 0)
        return shaded;
    vec3 tint = tints[pick.index];
    if (pick.blend > 0.0)
        tint = mix(tint, tints[pick.index + 1], pick.blend);
    return vec4(mix(shaded.rgb, tint, 0.5), shaded.a);
}

// Lambert over the tail -- the lights no bake saw, plus a second copy of every
// Mixed one -- each through its shadow map. A lightmapped surface SKIPS the
// Mixed copy, having shaded that light through its chart (lightmap.glsl); a
// surface with no chart multiplies in the probes' visibility for it instead,
// which is the atlas texel's job done at a point in space (decision K).
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
#ifndef LIGHTMAP
        visibility *= probe_light_visibility(light, world_position);
#endif
        diffuse += light.radiance.rgb *
                   (arrival.attenuation * visibility * max(dot(N, arrival.direction), 0.0)) / PI;
    }
    return diffuse;
}

// r_debug_channel = shadow_visibility: V alone, white and black, for the
// shadowed light nearest the camera. Magenta where no light in the pass has a
// map, so "nothing to show" cannot be mistaken for "everything lit".
// r_debug_channel = probe_visibility: the PROBES' V for the Mixed light the
// renderer picked, on every surface -- walls included, so the volume can be
// read against the geometry that shadowed it.
// r_debug_channel = shadow_penumbra: the radius the same light was filtered at
// over its cap, grey -- black is the minimum kernel, white is the cap -- so a
// wrong blocker search is told from a wrong filter.
#define DEBUG_FLAGS_SHOWING_VISIBILITY                                            \
    (DEBUG_FLAG_RENDER_SHADOW_VISIBILITY | DEBUG_FLAG_RENDER_PROBE_VISIBILITY | \
     DEBUG_FLAG_RENDER_SHADOW_PENUMBRA)

vec4 shadow_visibility_debug_color(vec3 world_position, vec3 N)
{
    int index = scene.debug_shadow_light;
    if (index < 0 || index >= scene.light_count)
        return vec4(1.0, 0.0, 1.0, 1.0);

    Light light = scene.lights[index];
    if ((scene.debug_flags & DEBUG_FLAG_RENDER_PROBE_VISIBILITY) != 0)
        return vec4(vec3(probe_light_visibility(light, world_position)), 1.0);

    Light_Arrival arrival    = light_arrival(light, world_position);
    float         visibility = shadow_visibility(light, arrival, world_position, N);
    if ((scene.debug_flags & DEBUG_FLAG_RENDER_SHADOW_PENUMBRA) != 0)
    {
        float cap = max(scene.shadow_pcss.y, scene.shadow_settings.y);
        return vec4(vec3(g_shadow_penumbra_texels / max(cap, 1.0)), 1.0);
    }
    return vec4(vec3(visibility), 1.0);
}

#endif // DIRECT_LIGHT_GLSL

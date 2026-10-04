#ifndef SCENE_GLSL
#define SCENE_GLSL

// The array is INDEXED BY BAKED SLOT for its first `baked_light_count` entries,
// which is what makes a light count this large affordable: a lightmapped surface
// never loops it. Its chart kept four lights at bake time and named them on the
// vertex, so it reads exactly those four and the level's other sixty cost it
// nothing (lighting_def.md ss14 step 6). The tail past baked_light_count is the
// lights the bake never saw, plus a second copy of every Mixed one, and it is
// what a surface with no chart evaluates -- small by construction, which is the
// premise ss4 rests the whole forward renderer on.
#define MAX_LIGHTS 64

// The shadow map pool's layer count -- renderer.hpp's MAX_SHADOW_LAYERS, and the
// scene block's size assert is what keeps the two one number. Sixteen because
// a point light is SIX layers (gate 9 step 3) beside the sun's cascades.
#define MAX_SHADOW_LAYERS 16
// How many layers the sun's shadow may take -- shared/lighting.hpp's
// MAX_SHADOW_CASCADES; the receiver picks one by view depth (direct_light.glsl).
#define MAX_SHADOW_CASCADES 4
// Team wall ripples the ghost shader draws (ripple.glsl) -- renderer.cpp's
// MAX_SCENE_RIPPLES, kept one number by the scene block's size assert.
#define MAX_RIPPLES 16
// renderer.cpp's MAX_SCENE_REVEAL_CONES, kept one number by the same assert.
#define MAX_REVEAL_CONES 8
// renderer.hpp's MAX_SCENE_FOG_VOLUMES, kept one number by the same assert.
#define MAX_FOG_VOLUMES 8

// scene.cel_fill_pattern.x, from r_cel_fill -- renderer.cpp's cel_fill_pattern_of.
#define CEL_FILL_NONE     0
#define CEL_FILL_HATCH    1
#define CEL_FILL_DITHER3D 2
#define CEL_FILL_DITHER3D_ORIGINAL 3

// scene.debug_flags, from r_debug_channel. One text for every fragment shader
// that reads them, so a channel added here is a channel every shader can show.
#define DEBUG_FLAG_RENDER_NORMALS           (1 << 0)
#define DEBUG_FLAG_RENDER_UV                (1 << 1)
#define DEBUG_FLAG_RENDER_PARALLAX_UV       (1 << 2)
#define DEBUG_FLAG_RENDER_SHADOW_VISIBILITY (1 << 3)
#define DEBUG_FLAG_RENDER_SHADOW_CASCADES   (1 << 4)
// The lighting split in two, each shown ALONE and before albedo: what the
// analytic lights deliver through their shadows (Lambert, over pi), and what
// the bake holds (atlas residual and bounce, or the probes). A scene that is
// too bright in one and not the other says which half to look at.
#define DEBUG_FLAG_RENDER_DIRECT_LIGHT      (1 << 5)
#define DEBUG_FLAG_RENDER_BAKED_LIGHT       (1 << 6)
// The probe volume's visibility for one Mixed light (gate 9 step 4), white and
// black -- the static half of what a dynamic object multiplies into that
// light, shown apart from the shadow map's half.
#define DEBUG_FLAG_RENDER_PROBE_VISIBILITY  (1 << 7)
#define DEBUG_FLAG_RENDER_SHADOW_PENUMBRA   (1 << 8)
// Gate 6 step 5: the parallax-corrected capture fetch alone at the surface's
// roughness, and the shaded result tinted by which capture won the pick.
#define DEBUG_FLAG_RENDER_REFLECTION         (1 << 9)
#define DEBUG_FLAG_RENDER_REFLECTION_CAPTURE (1 << 10)

// `Light` and LIGHT_BAKED_SLOT are light_arrival.glsl's -- the struct sits with
// the maths that reads it, so the shader tool's preview binds the same LAYOUT
// out of its own UBO instead of a second declaration of it. Nothing here needs
// the arrival functions; a vertex shader including this file gets them anyway,
// which is free and is why they carry no derivatives.
#include "light_arrival.glsl"

// One impact on a team wall: where the hull centre came through (xyz) and how
// long ago (w); the face's outward normal (xyz) and dot(normal, point) (w).
struct Ripple {
    vec4 center_age;
    vec4 plane;
};

// One reveal cone (reveal.glsl): the apex (xyz) and range (w); the unit axis (xyz) and the cosine of the half-angle (w).
struct RevealCone {
    vec4 apex_range;
    vec4 axis_cosine;
};

// One box of fog (fog_cells.comp): its lowest corner (xyz) and its density per world unit (w); its highest corner (xyz) and
// how far in from each face the fog takes to reach that density (w); the colour the air inside scatters (rgb).
struct FogVolume {
    vec4 minimum_density;
    vec4 maximum;
    vec4 color;
};

layout(set = 3, binding = 1) uniform SceneUniform {
    mat4  view_projection;
    vec4  camera_position;  // xyz, w unused
    vec4  ambient;          // rgb = r_ambient_floor, a unused
    // The probe volume's world-to-texture mapping (lighting_def.md gate 5):
    // uv = (P - probe_origin.xyz) * probe_inverse_extent.xyz. probe_origin.w is
    // 1 when this pass's bake carries probes and 0 when the bound volume is the
    // black stand-in, so a fragment can skip the four fetches.
    vec4  probe_origin;
    vec4  probe_inverse_extent;
    int   light_count;
    int   debug_flags;
    // Where the slot-indexed region ends and the tail begins. Entries below it
    // are addressed by a chart's stored slots and by nothing else.
    int   baked_light_count;
    // The light r_debug_channel = shadow_visibility shows, or -1 for none.
    int   debug_shadow_light;
    Light lights[MAX_LIGHTS];
    // Gate 9. Per layer of the shadow pool: the light's view-projection, and in
    // shadow_layers.x the world size of one of its texels ONE UNIT from the
    // light, in .y how far a receiver moves toward the light before the compare
    // at that same unit distance, .z the map's near plane (0 for an
    // orthographic map) and .w its far plane. A light names its layer in
    // Light.radiance.w (direct_light.glsl).
    mat4  shadow_view_projection[MAX_SHADOW_LAYERS];
    vec4  shadow_layers[MAX_SHADOW_LAYERS];
    // x = receiver normal offset in texels, y = PCF kernel radius in texels,
    // z = the cascade blend band as a fraction of each split depth, w = how
    // many cascades the sun claimed this frame (0 when no sun is shadowed).
    vec4  shadow_settings;
    // Gate 9 step 2. The camera's forward, which the view depth a cascade is
    // picked by is measured along, and each cascade's far depth. The sun's
    // Light names its FIRST cascade's layer; the others follow it in order.
    vec4  camera_forward;
    vec4  shadow_cascade_splits;
    // Gate 9 step 4: which baked slot each channel of the probe visibility
    // volume is OF, -1 for a channel no Mixed light claimed. A tail light whose
    // slot matches one reads that channel (probes.glsl).
    ivec4 probe_visibility_slots;
    // x = PCSS on (1) or off (0), y = the cap on the search and filter radius in texels
    vec4  shadow_pcss;
    // x = how many of `ripples` are live, the newest last; y = the age a ripple is dropped at.
    vec4   ripple_settings;
    Ripple ripples[MAX_RIPPLES];
    // x = seconds the pass has been drawing for, for what animates by itself.
    vec4   clock;
    // x = 1 when r_cel shades the whole frame through shading_cel.glsl, y = r_cel_terminator,
    // z = r_cel_shadow_edge, w = r_cel_softness.
    vec4   look;
    // rgb = what the unlit side is multiplied by, the three r_cel_shadow_* cvars, a = r_cel_bands.
    vec4   cel_shadow_tint;
    // x = r_cel_fill_strength, y = r_cel_fill_spacing, z = r_cel_fill_edge, w = r_cel_hatch_width.
    vec4   cel_fill;
    // x = r_cel_fill as one of CEL_FILL_*, y = r_cel_fill_shadow_tone_dark, z = r_cel_fill_material, w = r_cel_flat_albedo.
    vec4   cel_fill_pattern;
    // x = r_cel_fill_shadow_tone_light, y = r_cel_fill_ambient_dark, z = r_cel_fill_ambient_light, w = r_cel_fill_tone_lit.
    vec4   cel_fill_tone_range;
    // x = r_cel_speckle, y = r_cel_speckle_spacing, z = r_cel_speckle_density, w = r_cel_speckle_radius.
    vec4   cel_speckle;
    // x = r_cel_dither3d_size_variability, y = r_cel_dither3d_contrast, z = r_cel_dither3d_stretch_smoothness,
    // w = r_cel_halftone.
    vec4   cel_dither3d;
    // x = r_cel_pebble, y = r_cel_pebble_spacing, z = r_cel_pebble_density, w = r_cel_pebble_size.
    vec4   cel_pebble;
    // x = r_cel_pebble_irregularity, y = r_cel_pebble_width, z = r_cel_halftone_paper.
    vec4   cel_pebble_shape;
    // x = how many of `reveal_cones` reveal, from the first; y = how many erase, after those.
    vec4       reveal_settings;
    RevealCone reveal_cones[MAX_REVEAL_CONES];
    // x = how many of `fog_volumes` are live, y = the view depth the fog grid starts at, z = the view depth it ends at,
    // w = r_fog_anisotropy.
    vec4       fog_settings;
    // The camera's right and up, each as long as half the view is wide or tall one unit of view depth away (fog_grid.glsl).
    vec4       fog_view_right;
    vec4       fog_view_up;
    FogVolume  fog_volumes[MAX_FOG_VOLUMES];
} scene;

// r_cel_flat_albedo: a material's colour drawn towards its smallest mip, the mean of the whole texture.
vec3 cel_flat_albedo(sampler2D map, vec2 uv, vec3 sampled)
{
    float flatness = scene.cel_fill_pattern.w;
    return flatness > 0.0 ? mix(sampled, textureLod(map, uv, 1000.0).rgb, flatness) : sampled;
}

#endif // SCENE_GLSL

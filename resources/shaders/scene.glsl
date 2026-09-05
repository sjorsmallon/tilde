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

// `Light` and LIGHT_BAKED_SLOT are light_arrival.glsl's -- the struct sits with
// the maths that reads it, so the shader tool's preview binds the same LAYOUT
// out of its own UBO instead of a second declaration of it. Nothing here needs
// the arrival functions; a vertex shader including this file gets them anyway,
// which is free and is why they carry no derivatives.
#include "light_arrival.glsl"

layout(set = 3, binding = 1) uniform SceneUniform {
    mat4  view_projection;
    vec4  camera_position;  // xyz, w unused
    vec4  ambient;          // rgb = the constant floor, a unused
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
} scene;

#endif // SCENE_GLSL

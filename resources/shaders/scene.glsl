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
// scene block's size assert is what keeps the two one number.
#define MAX_SHADOW_LAYERS 8

// scene.debug_flags, from r_debug_channel. One text for every fragment shader
// that reads them, so a channel added here is a channel every shader can show.
#define DEBUG_FLAG_RENDER_NORMALS           (1 << 0)
#define DEBUG_FLAG_RENDER_UV                (1 << 1)
#define DEBUG_FLAG_RENDER_PARALLAX_UV       (1 << 2)
#define DEBUG_FLAG_RENDER_SHADOW_VISIBILITY (1 << 3)

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
    // light. A light names its layer in Light.radiance.w (direct_light.glsl).
    mat4  shadow_view_projection[MAX_SHADOW_LAYERS];
    vec4  shadow_layers[MAX_SHADOW_LAYERS];
    // x = receiver normal offset in texels, y = PCF kernel radius in texels.
    vec4  shadow_settings;
} scene;

#endif // SCENE_GLSL

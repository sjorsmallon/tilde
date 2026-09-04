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
    int   _pad1;
    Light lights[MAX_LIGHTS];
} scene;

#endif // SCENE_GLSL

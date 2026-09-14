// The SCENE the bake's kernels trace against, as types: lightmap_gpu.hpp's
// gpu_triangle_t and gpu_material_t, std430, plus the constants a hit is read
// with. Declared here rather than in each kernel because both of them resolve a
// hit now -- the indirect one for a bounce's albedo, the direct one for the tint
// of the glass a shadow ray crossed.
//
// Included BEFORE the kernel's own buffer declarations, since those are arrays
// of these. The functions that read them are in lightmap_shadow.glsl, which
// comes after.

struct gpu_triangle_t
{
  uint material;
  uint pad;
  vec2 uv0;
  vec2 uv1;
  vec2 uv2;
};

struct gpu_material_t
{
  uint albedo_texture;
  uint emissive_texture;
  // assets::alpha_mode_t: what the albedo's fourth channel MEANS. Only a shadow
  // ray reads it -- a bounce lands on the surface whatever its alpha says.
  uint alpha_mode;
  float alpha_cutoff;
};

const uint ALPHA_MODE_OPAQUE = 0u;
const uint ALPHA_MODE_CUTOUT = 1u;
const uint ALPHA_MODE_BLEND = 2u;

const float UNTEXTURED_BOUNCE_ALBEDO = 0.5;
const uint NO_TEXTURE = 0xffffffffu;
const float NO_MAX_DISTANCE = 1e30;

// The three parts of the scene, told apart by the instance's cull mask rather
// than by anything in the triangle -- light_occlusion_of's three answers. A
// bounce traces the opaque set ALONE, which is what the CPU tracer does: its
// scene is the occluder BVH and neither the glass nor the fences are in it.
const uint OPAQUE_INSTANCE_MASK = 0x01u;
const uint TRANSMISSIVE_INSTANCE_MASK = 0x02u;
// The FENCES: traced WITH the opaque set, because what they answer is the same
// question -- did the ray get through -- and a candidate of theirs is committed
// or dropped by one texel's alpha rather than tinting anything.
const uint ALPHA_TESTED_INSTANCE_MASK = 0x04u;

#pragma once

// The path tracer: what INDIRECT light arrives at a point. lighting_def.md gate 2.
//
// A chain, never a tree -- one direction per bounce, next-event estimation at
// every hit, and it never reads the atlas. Reading a stored value at the hit is
// the OTHER method (progressive gather), and mixing the two counts the hit
// surface's light twice.
//
// What it hands back is the incoming radiance field projected onto SH L1 --
// FOUR coefficients a colour channel, not an irradiance. The cosine has left the
// bake: a texel stores what ARRIVES and the shader applies the cosine against
// the shaded normal, which is what lets a normal map move the indirect term. The
// chain returns a value and writes nowhere, so the projection is the only thing
// that changed between the flat accumulator and this one.

#include "asset.hpp"
#include "lightmap_lights.hpp"
#include "map.hpp"
#include "span.hpp"

#include <utility>
#include <vector>

namespace shared
{

// What a face with no material reflects. A blockout level is every face, so this
// is what most of a bounce reads today; 0.5 is the mid-grey the untextured grid
// shader draws and is a preference to retune by looking at a bake.
inline constexpr float UNTEXTURED_BOUNCE_ALBEDO = 0.5f;

struct indirect_trace_settings_t
{
  // Chains fired per texel SAMPLE -- so a 2x2 supersampled texel costs four
  // times this. Zero is the whole off switch, and is what every bake before
  // gate 2 did.
  int rays_per_sample = 0;

  // How deep a chain runs before Russian roulette may end it. A fixed cap alone
  // biases every long path dark; `max_bounces` is a guard against a chain that
  // roulette keeps re-weighting, not the termination rule.
  int bounces_before_roulette = 2;
  int max_bounces = 16;

  // Offsets a bounce ray off the surface it leaves, the job shadow_ray_bias does
  // for a shadow ray. Same number by default because it is the same problem --
  // a ray starting exactly on a face re-enters the solid it left.
  float ray_bias = 0.25f;

  float shadow_ray_bias = 0.25f;
  int soft_shadow_samples = 8;
  float directional_shadow_distance = 100000.f;
};

// Everything a bounce needs that is not the ray: what the surface it landed on
// IS. Built ONCE, before the workers -- resolving a material loads a texture,
// and a load mutates the asset pools.
//
// `bvh_intersect_ray` reports a Collision_Id naming an OBJECT, which for the
// occluder BVH is a map uid; the rest of this is what turns that into an albedo.
struct traced_scene_t
{
  const Bounding_Volume_Hierarchy *bvh = nullptr;

  // Sorted by uid, so a hit resolves by binary search rather than by walking the
  // map's geometry list once per bounce.
  std::vector<std::pair<entity_uid_t, const brush_geometry_t *>> brushes;

  // By map material index. A null albedo is a material that resolved to none,
  // which reads as UNTEXTURED_BOUNCE_ALBEDO rather than as black; a null
  // EMISSIVE is a material with no emissive.png, which emits nothing. The two
  // polarities differ because the two absences mean opposite things.
  //
  // One struct rather than two parallel vectors: a bounce asks both questions
  // about the same material at the same hit, and two vectors are two lengths
  // that can disagree.
  struct material_t
  {
    const assets::texture_asset_t *albedo = nullptr;
    const assets::texture_asset_t *emissive = nullptr;
  };
  std::vector<material_t> materials;
};

[[nodiscard]] traced_scene_t build_traced_scene(const map_t &map,
                                                const Bounding_Volume_Hierarchy &bvh);

// sRGB-encoded bytes to a LINEAR reflectance. Albedo textures upload SRGB, so
// their bytes are encoded and reflectance arithmetic is linear -- sampling them
// raw makes every bounce systematically too bright, a 0.5 grey wall reflecting as
// 0.73. Decision F arriving on the CPU side, where no attachment format catches
// it for you.
[[nodiscard]] float srgb_byte_to_linear(uint8_t encoded);

// What the surface under a hit REFLECTS and what it EMITS, both in linear RGB:
// resolve the object, find the face by plane, read its material index, sample
// albedo at the hit's UV.
//
// Emission is the material's emissive.png, sampled at the same UV, and its
// PRESENCE is the whole of gate 4 -- a folder without one emits nothing. It is a
// radiance, in the units radiance_of(Light) hands back, which is what makes it
// the same lighting model as everything else.
//
// The face's LAYER 0 only. A blended face bounces its base material, which is
// the same simplification the runtime's own indirect term will make.
struct traced_surface_t
{
  linalg::vec3 albedo{0.f, 0.f, 0.f};
  linalg::vec3 emission{0.f, 0.f, 0.f};
};

[[nodiscard]] traced_surface_t surface_at(const traced_scene_t &scene,
                                          const ray_hit_result_t &hit,
                                          const linalg::vec3 &hit_position);

// The indirect light arriving at a surface point, projected onto SH L1: the
// average over `rays_per_sample` chains, each of which collects the DIRECT light
// at every vertex it visits and nothing at the first one. Direct light at
// `position` itself is the solve's own business and is not in here.
//
// Coefficients ADD, so accumulation IS the projection -- the basis is
// orthonormal, so a coefficient is an inner product, which is the integral the
// chains already estimate. There is no fitting pass and no second walk.
//
// The direction a coefficient is weighted by is the chain's FIRST LEG and
// nothing later: a chain box -> wall -> floor -> lamp delivers light to the box
// along box -> wall, and nothing past the wall is a direction the box can see.
[[nodiscard]] indirect_sh_l1_t trace_indirect_light(
    const traced_scene_t &scene, Span<const baked_light_t> lights,
    const linalg::vec3 &position, const linalg::vec3 &normal,
    const indirect_trace_settings_t &settings, uint32_t hash);

// The light arriving at a point in SPACE, projected onto SH L1 -- gate 5's probe.
// The same chains as trace_indirect_light fired over the FULL sphere (a crate
// lit from above is dark underneath), plus what a texel deliberately leaves out:
// the DIRECT light. A wall shades its direct light through its four slots; a
// dynamic object has none, and a pure Baked light sits in the array's head
// where the tail's loop never reads it. So each Baked light is added by
// next-event estimation from the probe itself. A Mixed light is NOT -- the
// runtime tail delivers it analytically to everything -- but its bounce is,
// since a bounce is a bounce whatever mode the light that made it has.
//
// The projection of a single direction's irradiance is E * Y(d): what the SH
// fit of a clamped cosine reads back as 0.25 E + 0.5 E cos, the standard L1
// smear, and the same approximation the atlas bounce already makes.
[[nodiscard]] indirect_sh_l1_t trace_probe_light(const traced_scene_t &scene,
                                                Span<const baked_light_t> lights,
                                                const linalg::vec3 &position,
                                                const indirect_trace_settings_t &settings,
                                                uint32_t hash);

} // namespace shared

#pragma once

// The lights a bake solves, and what one DELIVERS at a point: the arrival, the
// shadow rays and the derived jitter they are spread with.
//
// Extracted out of the direct solve because the path tracer's next-event
// estimation asks the same question at every bounce, and a bounce that
// disagreed with the direct term about a falloff, a cone or a bias would be a
// second lighting model (lighting_def.md ss11).

#include "collision_detection.hpp"
#include "entity_uid.hpp"
#include "lighting.hpp"
#include "lightmap.hpp"
#include "map.hpp"

#include <vector>

namespace shared
{

// A light the bake solves, and WHICH entity it is. The uid is how a mask slot is
// named and what the runtime resolve table matches against.
struct baked_light_t
{
  entity_uid_t uid = 0;
  scene_light_t light;
};

// Every light in the map a bake may see: Baked and Mixed, never Dynamic. Without
// that filter a level lit by both the atlas and the runtime array is lit twice
// (lighting_def.md ss2).
[[nodiscard]] std::vector<baked_light_t> collect_lights(const map_t &map);

// Every piece of every geometry object, as one BVH. The occluder set for the
// shadow rays AND the intersectable scene the tracer walks -- one structure,
// because a surface that stops a shadow ray and a surface a bounce lands on are
// the same surface.
[[nodiscard]] Bounding_Volume_Hierarchy build_occluder_bvh(const map_t &map);

// What one light does at one point, before any caller has its say: the direction
// to it, how far, and how much of it arrives.
//
// TWO gates, and the split is the whole of lighting_def.md ss14 step 6. `arrives`
// is range, the cone and a positive attenuation -- what light_arrival.glsl
// recomputes at runtime anyway, so nothing downstream can be wrong about it.
// `reaches` adds N.L against the surface normal it was asked about, which the
// runtime does NOT reproduce for a lightmapped face: it shades with the
// normal-mapped normal. An irradiance sum needs `reaches`; a visibility mask must
// be gated on `arrives` alone.
struct light_arrival_t
{
  bool arrives = false;
  bool reaches = false;
  linalg::vec3 direction{0.f, 0.f, 0.f};
  float distance = 0.f;
  float attenuation = 0.f;
  float normal_dot_light = 0.f;

  // The radius of the disc the emitter subtends AT THIS SURFACE POINT, which is
  // what the shadow rays are spread over. A point or spot's is its own radius; a
  // directional light's source_radius is a tangent, so its disc grows with the
  // distance the ray is cast over. One number either way, so the sampler needs no
  // light-type branch. Zero is a punctual light and costs one ray.
  float shadow_disc_radius = 0.f;
};

[[nodiscard]] light_arrival_t arrival_at(const scene_light_t &light,
                                         const linalg::vec3 &surface_position,
                                         const linalg::vec3 &surface_normal,
                                         float directional_shadow_distance);

// ONE ray, from the surface toward a named point on the emitter. The primitive
// under both the punctual test and the area one, so a soft shadow cannot use a
// different bias or a different miss rule from a hard one.
[[nodiscard]] bool shadow_ray_reaches(const Bounding_Volume_Hierarchy &bvh,
                                      const linalg::vec3 &surface_position,
                                      const linalg::vec3 &surface_normal,
                                      const linalg::vec3 &direction, float distance,
                                      float shadow_ray_bias);

// What FRACTION of the emitter this surface point can see: 1 for an unshadowed
// punctual light, 0 for a fully occluded one, and anything between for a point
// inside a penumbra. One number rather than a bool because that is the only
// difference an area light makes to everything downstream -- the stored coverage,
// the slot ranking and every irradiance sum just multiply by it.
//
// A light with no size takes exactly ONE ray whatever `soft_shadow_samples` says,
// so every map authored before area lights existed bakes bit for bit what it did.
[[nodiscard]] float light_visibility(const Bounding_Volume_Hierarchy &bvh,
                                     const linalg::vec3 &surface_position,
                                     const linalg::vec3 &surface_normal,
                                     const light_arrival_t &arrival,
                                     float shadow_ray_bias, int soft_shadow_samples,
                                     uint32_t hash);

// The jitter is DERIVED, never drawn from shared/rng.hpp's global state: a rebake
// at unchanged settings has to reproduce the same pixels, and the chart loop runs
// on several threads, so a sequence anyone can advance is a bake that differs
// from itself. Keyed by atlas position, which is unique across the whole solve.
[[nodiscard]] uint32_t hash_mix(uint32_t hash, uint32_t value);
[[nodiscard]] uint32_t sample_hash(int atlas_x, int atlas_y, int page, int sample_index);

// A uniform float in [0, 1) out of a hash, which is what every sampling decision
// below the solve is spelled in.
[[nodiscard]] inline float unit_float_from(uint32_t bits)
{
  return (float)(bits >> 8) * (1.f / 16777216.f);
}

// Rec. 709, which is what "how much light is this" means when the answer has to
// be one number. Only a RANKING or a survival probability reads it -- nothing
// stored is ever collapsed to a luminance.
[[nodiscard]] float luminance_of(const linalg::vec3 &linear_rgb);

// WHY a light does or does not reach a face, per face, through the same gates
// the solve runs. A bake that keeps a light nowhere has one symptom -- nothing --
// and the four reasons (out of range, outside the cone, facing away, occluded)
// look identical from the viewport. This samples every face of the map at a
// coarse stride and counts how many samples clear each gate, so the answer is a
// number per face rather than a guess.
struct light_reach_on_face_t
{
  entity_uid_t object_uid = 0;
  linalg::vec3 normal{0.f, 0.f, 0.f};
  int sampled = 0;
  int arrives = 0;  // range, cone and attenuation -- arrival_at's first gate
  int reaches = 0;  // ...and N.L against the face plane, its second
  int visible = 0;  // ...and an unoccluded shadow ray, out of those that arrive
  float nearest_distance = 0.f;
  // dot(-L, cone axis) at the sample closest to the axis, for a spot; the
  // outer cosine is what it is measured against. Left at -2 for other kinds.
  float best_cone_cos = -2.f;
};

[[nodiscard]] std::vector<light_reach_on_face_t> probe_light_reach(
    const map_t &map, const baked_light_t &light, const lightmap_bake_settings_t &settings,
    float shadow_ray_bias, float directional_shadow_distance, int max_samples_per_axis);


} // namespace shared

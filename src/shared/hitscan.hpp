#pragma once

#include "entity_uid.hpp"
#include "animation/hitbox_rig.hpp"
#include "linalg.hpp"
#include "hit_region.hpp"
#include "span.hpp"

#include <limits>

namespace shared
{

using linalg::vec3f;

// bounding sphere for broad phase rejection.
struct bounding_sphere_t
{
  vec3f center{0.f, 0.f, 0.f};
  float radius = std::numeric_limits<float>::infinity();
};

bounding_sphere_t compute_bounding_sphere(Span<const assets::posed_hitbox_t> hitboxes);

struct hitscan_target_t
{
  entity_uid_t                       uid{null_entity_uid};
  Span<const assets::posed_hitbox_t> hitboxes{};
  bounding_sphere_t                  bounds{};
};


inline hitscan_target_t make_hitscan_target(entity_uid_t uid,
                                            Span<const assets::posed_hitbox_t> hitboxes)
{
  return {uid, hitboxes, compute_bounding_sphere(hitboxes)};
}

struct hitscan_result_t
{
  entity_uid_t hit_uid{null_entity_uid};                    // 0 = nothing hit
  hit_region_t region = hit_region_t::Torso;  // only meaningful when hit_uid != 0
  vec3f impact_point{};
  vec3f impact_normal{};
  float distance = 0.f;
};


// direction is expected to be normalized.
// `ignore_uid` is how a shooter skips itself. max_range is built by the caller
// so  we don't feed the bvh into this thing.
hitscan_result_t resolve_hitscan(vec3f origin, vec3f direction,
                                 float max_range,
                                 Span<const hitscan_target_t> targets,
                                 entity_uid_t ignore_uid = null_entity_uid);

} // namespace shared

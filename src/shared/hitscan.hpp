#pragma once

#include "entity_uid.hpp"
#include "hitbox_rig.hpp"
#include "linalg.hpp"
#include "hit_region.hpp"
#include "span.hpp"

namespace shared
{

using linalg::vec3f;

// A target is its POSED VOLUMES, not a position: the caller places them (see
// compute_player_hitboxes in player_rig.hpp) and hands them over in world
// space.
//
// Passing the volumes rather than the pose inputs is what keeps lag
// compensation from needing a second entry point. Rewinding means feeding the
// endpoints a past tick recorded -- animation_def.md §4 stores OUTPUTS in
// Snapshot_History precisely so nothing has to be re-derived -- and those
// arrive here as exactly this span.
struct hitscan_target_t
{
  entity_uid_t                       uid;
  Span<const assets::posed_hitbox_t> hitboxes;
};

struct hitscan_result_t
{
  entity_uid_t hit_uid = 0;                    // 0 = nothing hit
  hit_region_t region  = hit_region_t::Torso;  // only meaningful when hit_uid != 0
  vec3f        impact_point{};
  vec3f        impact_normal{};
  float        distance = 0.f;
};

// Closest player hitbox along the ray, or a zeroed result if none.
//
// Deliberately knows nothing about the world: the CALLER casts against the
// world first (cast_ray) and passes the resulting distance as `max_range`, so
// "a wall blocks the shot" is a clamp rather than a second comparison in here.
// That keeps this a pure function -- no physics_state_t, no Jolt headers, no
// skeleton, and the test needs none of them.
//
// PRECONDITION: `direction` is normalized. Distances are measured in units of
// its length and compared against `max_range` directly.
hitscan_result_t resolve_hitscan(vec3f origin, vec3f direction,
                                 float max_range,
                                 Span<const hitscan_target_t> targets);

} // namespace shared

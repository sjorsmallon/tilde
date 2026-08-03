#pragma once

#include "entity_uid.hpp"
#include "linalg.hpp"
#include "player_hitboxes.hpp"
#include "span.hpp"

namespace shared
{

using linalg::vec3f;

struct hitscan_target_t     // caller builds this list -- lag comp later
{                           // means feeding rewound positions here
  entity_uid_t uid;
  vec3f        position;    // the player's FEET, the same origin the hitbox
                            // offsets are measured from
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
// That keeps this a pure function -- no physics_state_t, no Jolt headers, and
// the test needs neither.
//
// PRECONDITION: `direction` is normalized. Distances are measured in units of
// its length and compared against `max_range` directly.
hitscan_result_t resolve_hitscan(vec3f origin, vec3f direction,
                                 float max_range,
                                 Span<const hitscan_target_t> targets);

} // namespace shared

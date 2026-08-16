#pragma once

#include "entity_uid.hpp"
#include "hitbox_rig.hpp"
#include "linalg.hpp"
#include "hit_region.hpp"
#include "span.hpp"

#include <limits>

namespace shared
{

using linalg::vec3f;

// A whole target in one sphere, for the broad-phase reject in resolve_hitscan.
//
// The default radius is INFINITE, and that direction is deliberate: an unbounded
// sphere can never reject, so a target built without one is merely slower, never
// wrong. Getting this backwards -- a zero default -- would silently drop every
// hit on any target whose bound someone forgot to fill.
struct bounding_sphere_t
{
  vec3f center{0.f, 0.f, 0.f};
  float radius = std::numeric_limits<float>::infinity();
};

// A sphere containing every one of `hitboxes`. Conservative rather than minimal:
// this exists to skip work, so a loose bound that is cheap to compute beats a
// tight one that is not.
bounding_sphere_t compute_bounding_sphere(Span<const assets::posed_hitbox_t> hitboxes);

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
  // Prefer make_hitscan_target, which fills this. Left unbounded by an aggregate
  // that omits it, which costs the reject and nothing else.
  bounding_sphere_t                  bounds{};
};

// The sanctioned way to build one: pairs the volumes with their own bound so the
// two cannot describe different things.
inline hitscan_target_t make_hitscan_target(entity_uid_t uid,
                                            Span<const assets::posed_hitbox_t> hitboxes)
{
  return {uid, hitboxes, compute_bounding_sphere(hitboxes)};
}

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
//
// `ignore_uid` is how a shooter skips itself. It is a parameter rather than the
// caller's job because the target list is now built ONCE PER TICK for every
// shooter to share (server_context_t::posed_players) -- filtering it per shot
// would mean rebuilding it per shot, which is the cost that sharing removes.
// null_entity_uid ignores nobody.
hitscan_result_t resolve_hitscan(vec3f origin, vec3f direction,
                                 float max_range,
                                 Span<const hitscan_target_t> targets,
                                 entity_uid_t ignore_uid = null_entity_uid);

} // namespace shared

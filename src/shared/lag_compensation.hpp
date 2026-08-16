#pragma once

// Rewinding the hit test to what the shooter saw. lag_compensation_def.md is
// the design; the short version is the policy: SHOOTER-FAVORED. "I hit what my
// crosshair was on." The bill is that a victim already behind cover on their own
// screen and on the server's can still take damage, and the cap on how far back
// a client may reach (sv_max_rewind_ticks) is what bounds it.
//
// Values in, values out -- no server_context_t, no cvar_state_t. That is what
// lets lag_compensation_test link game_shared alone, and it is why the two
// halves below are separate: classify_bracket decides WHETHER a request is
// usable (pure arithmetic over what the server knows about the client), and
// try_pose_players_across_bracket builds the volumes for one that is.

#include "hitscan.hpp"
#include "network/entity_snapshot.hpp"
#include "network/snapshot_history.hpp"
#include "player_animator.hpp"
#include "player_rig.hpp"

#include <cstdint>
#include <vector>

namespace shared
{

struct posed_players_t
{
  // rig.volume_count() volumes per target, in `targets` order.
  std::vector<assets::posed_hitbox_t> volumes;
  // Each one's span points into `volumes`, so nothing here may outlive a resize
  // of it; both builders size `volumes` fully before filling `targets`.
  std::vector<hitscan_target_t> targets;
  uint32_t built_for_tick = 0;
};

// this is what the client was interpolating between.
struct interpolation_bracket_t
{
  uint32_t from_tick    = 0;
  uint32_t towards_tick = 0;
  float    fraction     = 0.f; // 0..1, from -> towards
};

// What the server may do with a requested bracket. The status IS the answer --
// this is a classification, not a fallible call -- and every value other than Ok
// and Clamped means the caller falls back to its present-tick pose set.
enum class bracket_status_t
{
  Ok,
  // Usable, but pinned to the rewind limit: the shot will be judged through a
  // blend OLDER than the shooter aimed through, and they feel that as no-reg.
  // Never silent -- the caller logs it, rate-limited.
  Clamped,
  // No blend requested (either endpoint 0): spectating, or fewer than two
  // snapshots seen. Routine, and not worth a word.
  Absent,
  // from > towards, or a fraction outside [0,1]. rejected outright.
  Malformed,

  // A malformed or hostile client, proposing a tick we never sent or one we didn't send yet.
  Unheld,
};

struct bracket_verdict_t
{
  bracket_status_t        status = bracket_status_t::Absent;
  // Meaningful for Ok and Clamped only.
  interpolation_bracket_t bracket{};
};

// Decides whether `requested` is a blend an honest client on this connection
// could actually have been drawing, and pins it to the rewind limit if it
// reaches too far back.
//
// `held_snapshot_tick` is the server's own note about what the client acked (it
// only ever grows, so UDP reordering cannot make this reject a late move), and
// `max_rewind_ticks` is the policy cap already min'd against the snapshot ring's
// capacity by the caller. 
bracket_verdict_t classify_bracket(interpolation_bracket_t requested,
                                   uint32_t held_snapshot_tick,
                                   uint32_t current_tick,
                                   uint32_t max_rewind_ticks);

// Poses every living player as the client drawing `bracket` saw them, into
// caller-owned storage.
//
// Fallible: either endpoint may have aged out of the ring, which is the caller's
// business -- it falls back to the present-tick pose set. A near miss is NOT
// substituted for a hit; a bracket the server cannot reproduce exactly is one it
// should decline, and the fallback is honest about that where a near-miss would
// not be.
//
// `out` is rebuilt whole, so a caller may reuse one buffer across shots.
[[nodiscard]] bool try_pose_players_across_bracket(
    const network::Snapshot_History<network::snapshot_frame_t>& history,
    const player_rig_t&                                         rig,
    const aim_settings_t&                                       settings,
    interpolation_bracket_t                                     bracket,
    posed_players_t&                                            out);

} // namespace shared

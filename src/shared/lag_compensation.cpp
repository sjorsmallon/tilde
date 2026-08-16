#include "lag_compensation.hpp"

#include "linalg.hpp"
#include "math.hpp"

#include <cmath>

namespace shared
{
namespace
{

// The angular members take the SHORT way round through linalg's shared
// lerp_degrees_clamped -- the same function the client draws through, which is
// what keeps the rewound silhouette the drawn one.
//
// Everything here CLAMPS. classify_bracket already rejects a fraction outside
// [0,1] as Malformed, but that is a separate call and this one is public: a
// caller that skipped it should get a pinned endpoint, not players posed outside
// the bracket entirely.
linalg::vec3f lerp_position(const linalg::vec3f& from, const linalg::vec3f& towards,
                            float fraction)
{
  return {lerp_clamped(from.x, towards.x, fraction),
          lerp_clamped(from.y, towards.y, fraction),
          lerp_clamped(from.z, towards.z, fraction)};
}

player_pose_t blend_pose(const entities::Player_Entity& from,
                         const entities::Player_Entity* towards, float fraction)
{
  // A uid absent from the `towards` frame despawned mid-blend. It is posed from
  // `from` alone rather than skipped: the client was drawing it at the start of
  // the blend, and that is the world the crosshair was on.
  if (towards == nullptr)
    return {.feet_position = from.position,
            .body_yaw      = from.body_yaw,
            .view_yaw      = from.view_angle_yaw,
            .view_pitch    = from.view_angle_pitch};

  return {.feet_position = lerp_position(from.position, towards->position, fraction),
          .body_yaw =
              linalg::lerp_degrees_clamped(from.body_yaw, towards->body_yaw, fraction),
          .view_yaw = linalg::lerp_degrees_clamped(from.view_angle_yaw,
                                                   towards->view_angle_yaw, fraction),
          // Pitch is bounded to +/-89 and never wraps, so it takes the plain one.
          .view_pitch = lerp_clamped(from.view_angle_pitch, towards->view_angle_pitch,
                                     fraction)};
}

} // namespace

bracket_verdict_t classify_bracket(interpolation_bracket_t requested,
                                   uint32_t held_snapshot_tick, uint32_t current_tick,
                                   uint32_t max_rewind_ticks)
{
  if (requested.from_tick == 0 || requested.towards_tick == 0)
    return {bracket_status_t::Absent, {}};

  // A client cannot have drawn a snapshot it never told us it holds, nor one we
  // have not taken yet. This is the tie between the two wire fields, and the
  // only check here that a fabricated bracket cannot walk past.
  if (requested.towards_tick > held_snapshot_tick || requested.towards_tick > current_tick)
    return {bracket_status_t::Unheld, {}};

  if (requested.from_tick > requested.towards_tick || !(requested.fraction >= 0.f) ||
      requested.fraction > 1.f)
    return {bracket_status_t::Malformed, {}};

  // The policy clamp. Ticks start at 1, so 1 is the oldest tick that can exist
  // at all; subtracting past it would wrap.
  const uint32_t oldest_allowed =
      current_tick > max_rewind_ticks ? current_tick - max_rewind_ticks : 1;

  bracket_verdict_t verdict{bracket_status_t::Ok, requested};

  // Each endpoint is PINNED to the boundary rather than the request being
  // rejected: a client at 300ms should still get a rewind, just a bounded one.
  // Pinning `towards` collapses the blend onto that tick, which is exactly the
  // oldest state the server is willing to be judged against.
  if (verdict.bracket.from_tick < oldest_allowed)
  {
    verdict.bracket.from_tick = oldest_allowed;
    verdict.status            = bracket_status_t::Clamped;
  }
  if (verdict.bracket.towards_tick < oldest_allowed)
  {
    verdict.bracket.towards_tick = oldest_allowed;
    verdict.status               = bracket_status_t::Clamped;
  }

  return verdict;
}

bool try_pose_players_across_bracket(
    const network::Snapshot_History<network::snapshot_frame_t>& history,
    const player_rig_t& rig, const aim_settings_t& settings,
    interpolation_bracket_t bracket, posed_players_t& out)
{
  const network::snapshot_frame_t* from    = history.find(bracket.from_tick);
  const network::snapshot_frame_t* towards = history.find(bracket.towards_tick);
  if (from == nullptr || towards == nullptr)
    return false;

  // `volumes` is resized rather than cleared: every element it ends up holding
  // is overwritten below, so clearing first would only zero them all twice.
  out.targets.clear();
  // A rewound set is posed for a BLEND, not for a tick; this names the newer
  // endpoint so a debug read says something true. Nothing gates on it here --
  // the live path's staleness check is what that field exists for.
  out.built_for_tick = bracket.towards_tick;

  const uint32_t volume_count = rig.volume_count();

  // Liveness comes from the `from` frame: that is who the client was drawing at
  // the start of its blend, and a player who died mid-blend was still a body
  // under the crosshair for part of it.
  uint32_t living_count = 0;
  for (const auto& [uid, player] : from->players)
    living_count += player.health > 0 ? 1 : 0;

  // Sized in full before a single target is pushed: each target holds a SPAN
  // into this vector, so filling the two in lockstep would leave every span
  // taken before a reallocation pointing at freed storage.
  out.volumes.resize((size_t)living_count * volume_count);
  out.targets.reserve(living_count);

  for (const auto& [uid, from_player] : from->players)
  {
    if (from_player.health <= 0)
      continue;

    const auto towards_it = towards->players.find(uid);
    const entities::Player_Entity* towards_player =
        towards_it == towards->players.end() ? nullptr : &towards_it->second;

    const Span<assets::posed_hitbox_t> slice{
        out.volumes.data() + (size_t)out.targets.size() * volume_count, volume_count};

    // The SAME function the live path and the client's debug_show_hitboxes
    // overlay call, so the rewound silhouette is the drawn silhouette.
    compute_player_hitboxes(rig, blend_pose(from_player, towards_player, bracket.fraction),
                            settings, slice);

    out.targets.push_back(make_hitscan_target(from_player.entity_id,
                                              Span<const assets::posed_hitbox_t>{slice}));
  }

  return true;
}

} // namespace shared

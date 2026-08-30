#pragma once

#include "../shared/hitbox_rig.hpp"
#include "../shared/lag_compensation.hpp" // interpolation_bracket_t
#include "../shared/player_animator.hpp"  // aim_settings_t
#include "../shared/player_rig.hpp"       // player_pose_t
#include "renderer.hpp"

#include "game.pb.h"

#include <cstdint>
#include <deque>
#include <vector>

namespace client
{

struct shot_debug_pose_t
{
  shared::entity_uid_t  uid = shared::null_entity_uid;
  shared::player_pose_t pose{};
};

// What THIS CLIENT believed at the moment it pulled the trigger.
//
// The poses are read straight off the values the draw used
// (Remote_Player_State::render_*), not reconstructed from the snapshot ring.
// That is the difference between "what was on my screen" and "what I can argue
// should have been on my screen", and only the first one settles anything.
struct shot_debug_local_t
{
  uint32_t                        input_number = 0;
  linalg::vec3f                   eye{};
  linalg::vec3f                   direction{};
  shared::interpolation_bracket_t reported_bracket{};
  std::vector<shot_debug_pose_t>  drawn;
};

// A short ring. The server's reply is a round trip behind, and an entry older
// than that is unpairable -- so this is sized by latency, not by history.
struct shot_debug_history_t
{
  static constexpr size_t CAPACITY = 32;
  std::deque<shot_debug_local_t> shots;

  void record(shot_debug_local_t &&shot);
  // Null when the pair has aged out, which is the routine case for a client
  // that turned the cvar on mid-flight.
  const shot_debug_local_t *find(uint32_t input_number) const;
};

// Appends both halves into `out`, held for `seconds`.
//
// CLIENT BLUE, SERVER RED, and the pairing rule that makes the picture readable:
// blue is what you shot at, red is what the server let you shoot at. Blue and
// red on top of each other means lag compensation worked and the miss is
// elsewhere; blue and red apart means the server judged a different world, and
// the caption says which of the five reasons it was.
//
// `settings` comes from the client's own cvar_state_t: the three sv_aim_* extents
// are @Mirrored, so the client already holds the server's values and posing with
// them cannot introduce a disagreement of its own.
void draw_shot_debug_pair(renderer::debug_draw_list_t &out,
                          const shot_debug_local_t *local,
                          const game::S2C_ShotDebug &server,
                          const aim_settings_t &settings, float seconds);

} // namespace client

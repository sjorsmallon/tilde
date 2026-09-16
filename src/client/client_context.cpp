#include "client_context.hpp"

#include "../shared/network/cvar_mirror.hpp"

// The one place that answers "what resets when, and why". Each group's presence
// or absence below carries its reason on the line that does it; if a group ever
// needs to be half-cleared, that is the signal its boundary is drawn wrong, not
// a reason to open-code a field list at the call site again.

namespace client
{

void reset_for_new_connection(client_context_t& context)
{
  shared::finish_replay_recording(context.replay_recorder);
  context.connection  = {};
  context.prediction  = {};
  context.replication = {};
  context.visuals     = {};

  // The transport's connection-scoped half, a stratum below the four groups
  // above. Not the whole layer: the socket outlives a connection.
  network::reset_connection_scoped_state(context.transport_layer);

  // The one piece of connection-scoped state we do not own. Gated because the
  // integrated launcher hands ONE cvar_state_t to both client::Init and
  // server::Init, so an in-process server is still the authority on these
  // values and reverting them here would clobber the running game.
  // server_session is non-null exactly when such a server exists.
  if (context.server_session == nullptr && context.cvars != nullptr)
    shared::revert_mirrored_cvars_to_defaults(*context.cvars);
}

void reset_state_in_preparation_for_new_map_load(client_context_t& context)
{
  shared::finish_replay_recording(context.replay_recorder);
  context.replication = {};
  context.visuals     = {};
}

const entities::Player_Entity* try_find_player_in_slot(const client_context_t& context, int32_t slot)
{
  for (const entities::Player_Entity& player :
       context.world.session.entity_system.entities_of<entities::Player_Entity>())
    if (player.client_slot_index == slot)
      return &player;
  return nullptr;
}

const entities::Player_Entity* try_find_my_player(const client_context_t& context)
{
  return try_find_player_in_slot(context, context.connection.my_slot);
}

const entities::Match* try_find_match(const client_context_t& context)
{
  Span<const entities::Game_Rules_Entity> rules =
      context.world.session.entity_system.entities_of<entities::Game_Rules_Entity>();
  return rules.empty() ? nullptr : &rules[0].match;
}

void snap_local_aim_to(prediction_t& prediction, const linalg::quatf& orientation)
{
  const linalg::view_angles_t facing =
      linalg::view_angles_from_direction(linalg::forward(orientation));
  const shared::subtick_view_t view = {facing.yaw_degrees,
                                       linalg::clamp(facing.pitch_degrees, -89.0f, 89.0f)};

  prediction.player_yaw         = view.yaw;
  prediction.player_pitch       = view.pitch;
  prediction.view_at_tick_start = view;
  for (prediction_t::pending_input_edge_t& edge : prediction.pending_input_edges)
    edge.view_after = view;
}

} // namespace client

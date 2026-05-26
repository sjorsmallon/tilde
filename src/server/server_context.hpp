#pragma once

#include "../shared/cosmetic_events.hpp"
#include "../shared/game_events.hpp"
#include "../shared/game_session.hpp"
#include "../shared/network/server_connection_state.hpp"
#include "../shared/physics.hpp"
#include <cstdint>
#include <memory>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

namespace server
{

// 'Context' refers to the bundle of data required for the active game session
// (entities, network state). This is distinct from 'State' (e.g. Initializing,
// Running, Shutdown) which refers to the FSM state.
struct server_context_t
{
  network::Server_Connection_State net;
  shared::game_session_t session;
  // Heap-allocated so construction is deferred past static init: physics_state_t
  // contains a JPH::TempAllocatorImpl that calls AlignedAlloc (Jolt's allocator)
  // in its constructor, which crashes if jolt_init() hasn't been called yet.
  // g_state is a file-scope global, so all value members would construct before main().
  std::unique_ptr<physics_state_t> physics;

  // Last-tick overlap state for trigger volumes, keyed by
  // (trigger.entity_id, player.entity_id). Used to detect the rising edge for
  // triggers configured with fire_mode == "on_enter". Lives on the context (not
  // file-scope) so that resetting the context (e.g. on map reload) clears it.
  std::set<std::pair<std::uint64_t, std::uint64_t>>
      previous_tick_overlapping_trigger_player_pairs;

  // Cosmetic effects produced this tick by gameplay code (rocket explosions,
  // bullet impacts, footsteps). Drained into every outgoing snapshot at end of
  // tick and cleared. FIFO ordering preserved so that deterministic server
  // runs produce identical effect streams (see plan §"Dispatch (server side)").
  std::vector<shared::dispatched_effect_t> effect_queue_this_tick;

  // Reliable gameplay events produced this tick (rocket_detonated, future:
  // player_died, round_started, …). Separate queue from cosmetic effects —
  // these ship in a dedicated S2C_GameEventBatch protobuf, not in the
  // unreliable snapshot. Drained and cleared at end of tick.
  std::vector<shared::game_event_t> game_event_queue_this_tick;

  // Server-side side table: player entity_uid_t → server tick the player
  // died on. Populated by the death-detection site (rocket_system today;
  // future: trigger volumes, melee, void-out) right next to its PLAYER_DIED
  // fire. Drained each tick by update_respawns() in
  // src/server/systems/respawn_system.cpp: any entry where
  // `current_tick >= death_tick + respawn_delay_ticks` has its player reset
  // (position/health/velocity/orientation from a spawn marker), the entry
  // removed, and PLAYER_SPAWNED fired. Clients consume the event to
  // dismiss death-screen UI.
  //
  // Lives server-side only: clients render the dead state from
  // replicated `health <= 0` and don't need the precise respawn tick. If
  // a "respawning in 2.3..." countdown UI lands, promote to a Networked
  // schema field on Player_Entity (so it rides the same channel as the
  // player's other state) rather than to a protobuf field.
  std::unordered_map<shared::entity_uid_t, uint32_t> death_tick_by_player_uid;
};

} // namespace server

#pragma once

#include "../shared/cosmetic_events.hpp"
#include "../shared/cvars/generated/cvars_generated.hpp"
#include "../shared/game_events.hpp"
#include "../shared/game_session.hpp"
#include "game_rules.hpp"
#include "../shared/map.hpp"
#include "../shared/network/server_transport_layer.hpp"
#include "../shared/physics.hpp"
#include <array>
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
// Running, shutdown) which refers to the FSM state.
struct server_context_t
{
  // Borrowed, never owned: the LAUNCHER owns the one cvar_state_t and the one
  // command_table_t for the process and hands them to server::init(). In the
  // integrated build these are the same two objects the client holds, so a
  // console toggle finally reaches the simulating side. Non-null from init()
  // until shutdown().
  cvars::cvar_state_t*    cvars    = nullptr;
  cvars::command_table_t* commands = nullptr;

  // The @Mirrored values as they were last broadcast. Change detection is a
  // member compare of *cvars against this (see shared/network/cvar_mirror.hpp),
  // which is why a direct field write in server code replicates and there is no
  // Set() to forget to call. Seeded from *cvars in init(), so the first tick
  // broadcasts nothing — a joining client gets the full set at connect instead.
  cvars::cvar_state_t last_broadcast_cvars;

  network::Server_Transport_Layer transport_layer;
  shared::game_session_t session;

  // Match-level rules: which round we're in, which phase, when the phase ends.
  // Server-authoritative and server-only — see the placement argument at the
  // top of game_rules.hpp for why this is not on game_session_t. Driven by
  // update_game_rules() (systems/game_rules_system.hpp) once per tick and
  // reset by load_map_into_state().
  game_rules_state_t rules;

  // The map the server is currently running, as loaded by load_map_into_state().
  // current_map is the retained map_t (kept, not discarded after session init) so
  // we can serialize it for streaming/hashing without re-reading disk.
  // current_map_path is what we loaded from disk (sent to clients so they can
  // load the same file); map_content_hash is the FNV-1a of the canonical
  // serialization (compute_map_content_hash), computed once at load and echoed on
  // connect so clients can verify a match.
  shared::map_t current_map;
  std::string   current_map_path;
  uint32_t      map_content_hash = 0;

  // Per-slot "has this client finished loading the current map?" gate. Set true
  // when a client connects (it loads before connecting) and again when it acks
  // C2S_MapLoaded; set false for every connected client the moment we broadcast
  // a CmdChangeMap. Snapshots are withheld from a not-ready client so it never
  // receives entity deltas for a map it isn't running yet.
  std::array<bool, network::sv_max_player_count> client_map_ready{};
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

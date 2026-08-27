#pragma once

#include "../shared/array.hpp"
#include "../shared/cvars/generated/cvars_generated.hpp"
// Both channels: the context owns their per-tick streams, and every fire site
// on this side reaches them through it.
#include "../shared/effects/generated/effects_generated.hpp"
#include "../shared/events/generated/events_generated.hpp"
#include "../shared/game_session.hpp"
#include "../shared/hitscan.hpp"
#include "../shared/lag_compensation.hpp"
#include "../shared/map.hpp"
#include "../shared/network/entity_snapshot.hpp"
#include "../shared/network/server_transport_layer.hpp"
#include "../shared/network/snapshot_history.hpp"
#include "../shared/network/udp_socket.hpp"
#include "../shared/physics.hpp"
#include "bot_state.hpp"
#include "damage_types.hpp"
#include "game_rules.hpp"

#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace server
{

static constexpr int32_t invalid_slot_idx = -1;

// True for a slot that can be used to index `server_context_t::clients` and the
// transport layer's parallel arrays. Says nothing about whether anyone is
// connected there — `transport_layer.slot_occupied[slot]` is that question.
inline bool is_valid_client_slot(int32_t slot)
{
  return slot >= 0 && slot < network::sv_max_client_count;
}

// Input numbers start at 0, so "none consumed yet" has to sit below that. Its
// own constant rather than invalid_slot_idx: that one names SLOT indices, and a
// client's input stream is a different number space that merely happens to
// spell its empty value the same way.
static constexpr int32_t no_input_processed_yet = -1;

struct client_slot_t
{
  shared::entity_uid_t player_uid = shared::null_entity_uid;

  // Sanitized at the connect site, held here because a client is accepted as a
  // spectator and its Player_Entity -- where the name actually replicates from
  // -- may not exist until much later. Same capacity as that field, so the
  // copy across cannot truncate a second time.
  network::pascal_string_t<32> player_name;

  // How far this client's input stream has been CONSUMED -- a high-water mark
  // over C2S_ClientInput.input_number, not "the last input that moved you". A spectator's input and one whose
  // sub-tick grammar was refused both advance it, because both were consumed;
  // only an over-budget drop does not, since that one never ran and its button
  // edges must not be skipped. Echoed back in every snapshot, where it both
  // trims the client's resend tail and starts its reconciliation replay. No
  // "server" in the name: this struct is the server's note about a client, so
  // both sides of that are already said. The client's copy has to spell it out.
  int32_t  latest_processed_input_number = no_input_processed_yet;

  // What the client's buttons were when its last processed input ENDED, which
  // is what the next one's rising edges are measured against. Not "what it sent
  // last": an input is a start state plus the edges inside the tick, so a press
  // and its release between two boundaries is a press this sees and a naive
  // compare of the two boundaries does not. See shared/subtick.hpp.
  uint64_t latest_buttons_bitmap    = 0;
  uint32_t held_snapshot_tick = 0;

  // This client holds the map we are running. DERIVED every tick from
  // C2S_ClientInput::map_content_hash, never announced -- see the pass at the
  // top of Tick(). False on a fresh slot and false after a map load, and it
  // comes back on its own one input later; there is no ack to wait for and
  // nothing that retransmits.
  bool map_ready = false;

  // This client wants a body, whether or not it has one yet. The two are not
  // the same question in a mode with join_in_progress = false: a player who
  // asks to join mid-round waits as a spectator until the round boundary, and
  // player_uid alone cannot tell that player from one who chose to spectate.
  //
  // Set by join_game, cleared by spectate, and read at the round boundary by
  // admit_waiting_players. False on a fresh slot: a connecting client is
  // accepted as a spectator and joins on request.
  bool wants_to_play = false;

  // Tick we last complained that this client's rewind request had to be clamped
  // to sv_max_rewind_ticks. A client sitting at 300ms over-clamps on EVERY shot,
  // and a warning per shot buries everything else in the log — so the warning is
  // rate-limited to one per second per slot off this. 0 means "never".
  uint32_t last_rewind_warning_tick = 0;

  // Move commands this client may still execute. One is granted per tick and
  // one is spent per executed move, which bounds movement RATE rather than
  // moves-per-tick — see server/move_budget.hpp for why that distinction is the
  // whole fix.
  //
  // Starts at ONE, not zero, and that is load-bearing: the two clocks are not
  // synchronised, so a client's command for a tick can arrive before the server
  // grants that tick's credit. A client is entitled to be one tick out of phase.
  // Starting empty cost every client its first burst — visible as one dropped
  // command on join and after any drain, for no reason anyone could defend.
  int32_t move_credits = 1;

  // Same rate-limit shape as last_rewind_warning_tick: a client over budget is
  // over it on every move, so the complaint is one per second per slot.
  uint32_t last_move_throttle_warning_tick = 0;
};

// The map currently running, and everything keyed to it. Cleared whole by
// reset_state_in_preparation_for_new_map_load.
struct world_t
{
  shared::game_session_t session;
  std::unique_ptr<physics_state_t> physics;
  shared::map_t current_map;
  std::string   current_map_path;
  uint32_t      map_content_hash = 0;

  std::vector<Bot_State> bots;
  int32_t next_bot_slot = BOT_SLOT_BASE; // increments with each spawned bot

  std::set<std::pair<std::uint64_t, std::uint64_t>>
      previous_tick_overlapping_trigger_player_pairs;

  std::unordered_map<shared::entity_uid_t, uint32_t> death_tick_by_player_uid;
  game_rules_state_t rules;

  // Which cvars this map's attached_cvars list actually set. Ids, not values:
  // unloading the map puts them back to the cvars.def defaults, and a third
  // copy of the values would be a third thing that can disagree. This is why
  // the cvars at the top of server_context_t are "nothing resets them" EXCEPT
  // the named few a map claimed -- see
  // reset_state_in_preparation_for_new_map_load.
  std::vector<cvars::cvar_id> cvars_applied_by_map;
};


struct replication_t
{
  network::Snapshot_History<network::snapshot_frame_t> snapshot_history;
};

using tick_input_t = network::ServerInbox;

// A hit that has been RESOLVED but not yet applied.
struct pending_hit_t
{
  damage_info_t        info;
  linalg::vec3f        impact_point{};
  linalg::vec3f        impact_normal{};
  shared::hit_region_t region = shared::hit_region_t::Torso;
};


struct tick_output_t
{
  shared::event_stream_t effects;

  shared::event_stream_t events;

  std::vector<pending_hit_t> pending_hits;
};

struct server_context_t
{

  cvars::cvar_state_t*    cvars    = nullptr;
  cvars::command_table_t* commands = nullptr;
  cvars::cvar_state_t last_broadcast_cvars;


  network::Udp_Socket             socket;
  network::Server_Transport_Layer transport_layer;

  uint32_t tick_number = 1;

  // Rebuilt whole at the top of every tick, so nothing resets it: only the
  // vectors' CAPACITY lives across ticks, and `built_for_tick` is what makes a
  // stale read a crash rather than an assumption.
  shared::posed_players_t posed_players;

 // allocated once but "reused" per shot, expected to be sized once and then just repopulated for every shot fired by clients.
  shared::posed_players_t rewind_scratch;

  // --- Reset-scoped state ---
  world_t       world;
  Array<client_slot_t, network::sv_max_client_count> clients;
  replication_t replication;
  tick_input_t  incoming;
  tick_output_t outgoing;
};

void reset_state_in_preparation_for_new_map_load(server_context_t& context);
void reset_client_slot(server_context_t& context, int32_t slot);
void clear_incoming(server_context_t& context);
void clear_outgoing(server_context_t& context);

} // namespace server

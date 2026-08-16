#pragma once

#include "../shared/array.hpp"
#include "../shared/cvars/generated/cvars_generated.hpp"
// Both channels: the context owns their per-tick streams, and every fire site
// on this side reaches them through it.
#include "../shared/effects/generated/effects_generated.hpp"
#include "../shared/events/generated/events_generated.hpp"
#include "../shared/game_session.hpp"
#include "../shared/map.hpp"
#include "../shared/network/entity_snapshot.hpp"
#include "../shared/network/server_transport_layer.hpp"
#include "../shared/network/snapshot_history.hpp"
#include "../shared/network/udp_socket.hpp"
#include "../shared/physics.hpp"
#include "bot_state.hpp"
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
// connected there — `transport_layer.player_slots[slot]` is that question.
inline bool is_valid_client_slot(int32_t slot)
{
  return slot >= 0 && slot < network::sv_max_player_count;
}

struct client_slot_t
{
  shared::entity_uid_t player_uid = shared::null_entity_uid;
  int32_t  latest_processed_command = invalid_slot_idx;
  uint64_t latest_buttons_bitmap    = 0;
  uint32_t acked_snapshot_tick = 0;
  bool map_ready = false;
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
};


struct replication_t
{
  network::Snapshot_History<network::snapshot_frame_t> snapshot_history;
};

using tick_input_t = network::ServerInbox;

// The two S2C channels' per-tick output. ONE shape: both encode at fire time,
// and nothing here is ever read back -- the only consumer is the send at end of
// tick.
//
// `effects` used to be a value queue, because its batch is appended inside the
// per-client snapshot loop and therefore starts at a different BIT offset per
// client. The snapshot writer byte-ALIGNS before splicing the finished stream
// in, which costs at most 7 bits per packet and is what lets one encoding serve
// every client.
struct tick_output_t
{
  // Spliced into each client's snapshot packet, byte-aligned.
  shared::event_stream_t effects;

  // Its own message, so its bitstream starts at bit 0.
  shared::event_stream_t events;
};

struct server_context_t
{

  cvars::cvar_state_t*    cvars    = nullptr;
  cvars::command_table_t* commands = nullptr;
  cvars::cvar_state_t last_broadcast_cvars;


  network::Udp_Socket             socket;
  network::Server_Transport_Layer transport_layer;

  uint32_t tick_number = 1;

  // --- Reset-scoped state ---
  world_t       world;
  Array<client_slot_t, network::sv_max_player_count> clients;
  replication_t replication;
  tick_input_t  incoming;
  tick_output_t outgoing;
};

void reset_state_in_preparation_for_new_map_load(server_context_t& context);
void reset_client_slot(server_context_t& context, int32_t slot);
void clear_incoming(server_context_t& context);
void clear_outgoing(server_context_t& context);

} // namespace server

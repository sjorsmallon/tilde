#pragma once

#include "../shared/cvars/generated/cvars_generated.hpp"
#include "../shared/entities/entity_reflection.hpp"
#include "../shared/game_session.hpp"
#include "../shared/network/client_connection_state.hpp"
#include "../shared/network/entity_snapshot.hpp"
#include "../shared/network/snapshot_history.hpp"
#include "../shared/physics.hpp"
#include "../shared/player_move.hpp"

#include <array>
#include <unordered_map>
#include <vector>

namespace client
{

struct audio_system_t; // owned by client_impl.cpp; see Init()/Shutdown()

struct client_context_t
{
   // launcher hands these off to the client module on Init().
  // They are owned by the launcher and outlive the client module.
  cvars::cvar_state_t*    cvars    = nullptr;
  cvars::command_table_t* commands = nullptr;

  // client only since the server has cannot visualize.
  debug_collision::Face_Sink debug_collision_faces;

  // --- Shared game world (entities, BVH, navmesh) ---
  shared::game_session_t session;

  // FNV-1a hash of the map the client loaded
  // computed via shared::compute_map_content_hash().
  // Verified against the server's CmdAccept.content_hash to detect a
  // client/server map mismatch. 0 = not computed (verification skipped).
  uint32_t loaded_map_content_hash = 0;

  // --- Network socket and server address ---
  ::network::Client_Connection_State connection_state;

  // --- Requested server endpoint ---
  // Written by whoever initiates a join (the main menu's Join Game field, the
  // `connect` console command) and read exactly once, by Play_State::on_enter,
  // which is the only place that sets connection_state.server_address above.
  // state_manager::switch_to() carries no payload and the states are long-lived
  // singletons, so the context is the seam between "who picked the server" and
  // "who connects to it". Defaults to loopback, which is what the integrated
  // launcher and the menu's plain Start Game want.
  ::network::Address requested_server_address =
      ::network::Address(127, 0, 0, 1, ::network::server_port_number);

  // --- Connection phase (client network/handshake state machine) ---
  //   Disconnected ──on_enter: send CmdConnect──▶ Connecting
  //
  //   Connecting ──CmdAccept, have matching map──▶ Connected
  //              ──CmdAccept, no/mismatched map──▶ Loading   (send C2S_RequestMapData)
  //              ──CmdReject──────────────────────▶ Disconnected
  //
  //   Connected ──CmdChangeMap (new map)─────────▶ Loading
  //             ──on_exit: send CmdDisconnect─────▶ Disconnected
  //
  //   Loading ──local copy present & hash matches─▶ Connected (send C2S_MapLoaded)
  //           ──S2C_MapData arrives & verifies────▶ Connected (send C2S_MapLoaded)
  //           ──load/verify fails─────────────────▶ (stay Loading; re-request)
  //
  // Invariants: we only send move commands / run prediction+reconciliation while
  // Connected; the server withholds snapshots (client_map_ready) until it sees
  // our C2S_MapLoaded, so a Loading client receives no entity deltas. See
  // play_state.cpp update() and awaiting_stream_content_hash below.
  enum class Connection_Phase { Disconnected, Connecting, Loading, Connected };
  Connection_Phase connection_phase = Connection_Phase::Disconnected;

  // Non-zero while we're in Loading because we lacked (cache miss) or
  // mismatched the server's map and asked it to stream the compiled package:
  // holds the content_hash we're waiting to receive. Guards the CmdChangeMap
  // handler so a resent switch message re-requests the stream (cheap retransmit
  // stand-in) instead of tearing down and reloading the world every tick.
  // Cleared once S2C_MapData applies. See map_transfer / play_state.
  uint32_t awaiting_stream_content_hash = 0;



  // the slot we occupy on the server (I guess to filter inputs etc?)
  int my_slot = -1;

  // Local player's entity uid, learned from the first self snapshot. Used to
  // suppress server-dispatched cosmetic effects attached to our own player
  // (jump/land), which we already played locally via prediction. 0 until known.
  shared::entity_uid_t my_entity_uid = 0;
  int command_number = 0;
  uint32_t server_tickrate = 60;

  // --- Local player simulation ---
  vec3f player_position = {0, 36, 0};
  vec3f player_velocity = {0, 0, 0};
  float player_yaw = 0.0f;
  float player_pitch = 0.0f;
  float physics_accumulator = 0.0f;

  // --- Client-side prediction ring buffer ---
  struct Saved_Command
  {
    int command_number = -1;
    Move_Input input = {};
    float yaw = 0.f;
    float pitch = 0.f;
    vec3f predicted_position = {0, 0, 0};
    vec3f predicted_velocity = {0, 0, 0};
  };
  static constexpr int MAX_PENDING_COMMANDS = 128;
  std::array<Saved_Command, MAX_PENDING_COMMANDS> pending_commands = {};

  // --- Server reconciliation ---
  vec3f latest_server_position = {0, 0, 0};
  vec3f latest_server_velocity = {0, 0, 0};
  int latest_server_ack_command = -1;
  bool received_server_update = false;
  vec3f visual_error_offset = {0, 0, 0};
  vec3f reconciliation_error = {0, 0, 0};
  float reconciliation_error_magnitude = 0.0f;

  // --- Remote player interpolation ---
  struct Remote_Player_Snapshot
  {
    vec3f position = {0, 0, 0};
    float yaw = 0.f;
    float pitch = 0.f;
    uint32_t server_tick = 0;
  };
  struct Remote_Player_State
  {
    int slot_index = -1;
    // Which entity currently occupies the slot. A slot can change occupant, and
    // interpolating across that would lerp the new player in from the old
    // player's last position.
    shared::entity_uid_t entity_uid = shared::null_entity_uid;
    bool active = false;
    Remote_Player_Snapshot snapshots[2] = {};
    int snapshot_count = 0;
    vec3f render_position = {0, 0, 0};
    float render_yaw = 0.f;
    float render_pitch = 0.f;
  };


  std::unordered_map<int32_t, Remote_Player_State> remote_players;
  float interpolation_time = 0.f;

  // --- Delta decompression: the latest reconstructed snapshot ---
  // These are what the game reads. They are a copy of the newest frame in the
  // history below, kept separate because the rest of the client wants "the
  // current world", not "frame N".
  std::unordered_map<int32_t, entities::Player_Entity> latest_player_entities;
  std::unordered_map<shared::entity_uid_t, entities::Rocket_Entity> remote_rockets;
  // Physics bodies received from server. State is replaced wholesale each
  // snapshot — no interpolation yet (see todo.md). Renders correctly in
  // integrated mode via server_session; in networked mode this will visibly
  // stutter at server tick boundaries until interpolation is added.
  std::unordered_map<shared::entity_uid_t, entities::Physics_Body_Entity> remote_physics_bodies;
  uint32_t latest_processed_tick = 0;

  // --- Delta decompression: the snapshot history ---
  // The server deltas against a tick we ACKED, so we must still be holding the
  // exact state we reconstructed for that tick — not merely "the current
  // world", which has moved on. Mirrors the server's ring one for one; see
  // shared/network/snapshot_history.hpp. `acked_tick` on it is the value echoed
  // back in every C2S_PlayerMoveCommand.
  // The frame type is ::network::snapshot_frame_t, the same one the server
  // stores — the server deltas against what it believes we reconstructed, so
  // the two structures being one type is not a convenience, it is the
  // guarantee. Keyed by entity uid on both ends; `latest_player_entities` above
  // is the by-slot view the rest of the client wants, rebuilt on publish.
  ::network::Snapshot_History<::network::snapshot_frame_t> snapshot_history;

  void clear_snapshot_history()
  {
    snapshot_history.clear();
    latest_processed_tick = 0;
  }

  // --- Integrated-mode session pointer ---
  // In integrated builds, set to the server's authoritative session so the
  // renderer can read entity pools directly instead of going through snapshot
  // interpolation. Null in dedicated/networked-only builds.
  const shared::game_session_t* server_session = nullptr;

  // --- Client-owned physics world (static geometry only) ---
  // Borrowed from Play_State for the duration of the play session — set in
  // Play_State::on_enter, cleared in on_exit. Effect handlers (e.g. the
  // rocket-explosion handler) cast against this to resolve surface contact
  // locally. Null in editor / menu states and during reconnects.
  physics_state_t* physics_state = nullptr;

  // --- Client audio ---
  // Borrowed pointer to the client-global audio system (owned in
  // client_impl.cpp, lives for the whole client session). Cosmetic-effect
  // handlers play sounds through this. Always non-null after client Init();
  // handlers guard it anyway in case audio init failed.
  audio_system_t* audio = nullptr;

  //@Note(SMIA): move this.
  struct explosion_effect_t
  {
    vec3f position;
    float time_remaining;
    uint32_t explosion_index; // renderer key: high bit set to avoid entity_id collision
  };
  std::vector<explosion_effect_t> explosion_effects;
  uint32_t next_explosion_index = 0;
};

} // namespace client

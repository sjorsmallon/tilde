#pragma once

#include "../shared/array.hpp"
#include "../shared/cvars/generated/cvars_generated.hpp"
#include "../shared/entities/entity_reflection.hpp"
#include "../shared/game_session.hpp"
#include "../shared/network/client_transport_layer.hpp"
#include "../shared/network/entity_snapshot.hpp"
#include "../shared/network/snapshot_history.hpp"
#include "../shared/physics.hpp"
#include "../shared/player_move.hpp"

#include <memory>
#include <unordered_map>
#include <vector>

namespace client
{

struct audio_system_t; // owned by client_impl.cpp; see init()/shutdown()

static constexpr int32_t invalid_slot_idx = -1;

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
// Connected; the server withholds snapshots (its client_slot_t::map_ready) until it sees
// our C2S_MapLoaded, so a Loading client receives no entity deltas. See
// play_state.cpp update() and connection_t::awaiting_stream_content_hash.
enum class Connection_Phase { Disconnected, Connecting, Loading, Connected };

// --- Client-side prediction ring buffer entry ---
struct Saved_Command
{
  int command_number = -1;
  Move_Input input = {};
  float yaw = 0.f;
  float pitch = 0.f;
  vec3f predicted_position = {0, 0, 0};
  vec3f predicted_velocity = {0, 0, 0};
};
static constexpr uint32_t MAX_PENDING_COMMANDS = 128;

// --- Remote player interpolation ---
struct Remote_Player_Snapshot
{
  vec3f position = {0, 0, 0};
  float yaw = 0.f;
  float pitch = 0.f;
  // Server-owned; see Remote_Player_State::body_yaw below.
  float body_yaw = 0.f;
  uint32_t server_tick = 0;
};

struct Remote_Player_State
{
  int32_t slot_index = invalid_slot_idx;
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
  // Where the FEET point, which lags the view yaw. The difference between the
  // two is the torso twist, and it is what drives the left/right aim poses --
  // drawing the body at the view yaw would make that difference zero forever
  // and leave two of the five poses unreachable.
  //
  // READ FROM THE SNAPSHOT, never integrated here. The server owns this: it
  // advances the accumulator on the fixed tick and poses the hit volumes with
  // it, so a client integrating its own copy would draw a silhouette the
  // server is not testing. Interpolated between the two snapshots like the
  // view yaw beside it, and that interpolated value never feeds the next
  // frame -- the moment it did, the local integrator would be back. See
  // animation_def.md, "RESOLVED: body_yaw is a tier-1 accumulator".
  float body_yaw = 0.f;

  // Player_Entity::death_tick as of the last snapshot: 0 = alive, non-zero =
  // this player is a corpse and the death clip is what gets drawn.
  uint32_t death_tick = 0;
  // How far into the death clip the corpse is. Advanced on the RENDER clock
  // rather than recomputed from the tick stamp every frame, because the
  // stamp only moves at the server tickrate and the clip would visibly step
  // at 60Hz on a 144Hz display. SEEDED from the stamp when death_tick
  // changes, which is what makes a client connecting mid-corpse -- or one
  // that dropped the packet the death landed in -- pick the animation up
  // where it is instead of restarting it.
  float death_animation_seconds = 0.f;
};

//@Note(SMIA): move this.
struct explosion_effect_t
{
  vec3f position;
  float time_remaining;
  uint32_t explosion_index; // renderer key: high bit set to avoid entity_id collision
};

struct local_world_t
{
  shared::game_session_t session;

  // FNV-1a hash of the map the client loaded, via compute_map_content_hash().
  // Verified against the server's CmdAccept.content_hash to detect a
  // client/server map mismatch. 0 = not computed (verification skipped).
  uint32_t map_content_hash = 0;

  // Client-owned static physics world. Effect handlers (e.g. the
  // rocket-explosion handler) cast against this to resolve surface contact
  // locally. Null in editor / menu states and before the first map load.
  //
  // OWNED here, not borrowed. The unique_ptr used to live on Play_State with a
  // raw copy in this slot, which made teardown ORDER load-bearing: on_exit had
  // to null the borrow before dropping the owner, or a late effect dispatch
  // would cast against a freed world.
  std::unique_ptr<physics_state_t> physics_state;

  // Session and physics are built; the world can be simulated and drawn. Was
  // Play_State::session_ready_for_simulation_and_rendering -- a fact ABOUT this
  // struct, stored where nobody holding the struct could see it.
  //
  // Distinct from connection_t::phase: while streaming a map we are
  // Connecting/Loading with this still false, and an offline client with a map
  // has it true with no connection at all.
  bool ready = false;
};

// Who we are to the server, and how far through the handshake we got. Plain
// data only -- the socket is client_context_t::transport_layer, a stratum below
// this one, and is not reset with it.
struct connection_t
{
  Connection_Phase phase = Connection_Phase::Disconnected;

  // Non-zero while we're in Loading because we lacked (cache miss) or
  // mismatched the server's map and asked it to stream the compiled package:
  // holds the content_hash we're waiting to receive. Guards the CmdChangeMap
  // handler so a resent switch message re-requests the stream (a cheap
  // retransmit stand-in) instead of tearing down and reloading the world every
  // tick. Cleared once S2C_MapData applies.
  uint32_t awaiting_stream_content_hash = 0;

  // The slot we occupy on the server.
  int32_t my_slot = invalid_slot_idx;

  // Local player's entity uid, learned from the first self snapshot. Used to
  // suppress server-dispatched cosmetic effects attached to our own player
  // (jump/land), which we already played locally via prediction. 0 until known.
  shared::entity_uid_t my_entity_uid = 0;

  uint32_t server_tickrate = 60;
};

// The local player as WE compute them, plus the server's last word on us that
// reconciliation corrects against. All of it is meaningless across a connect,
// which is what makes it a group.
struct prediction_t
{
  vec3f player_position = {0, 36, 0};
  vec3f player_velocity = {0, 0, 0};
  float player_yaw = 0.0f;
  float player_pitch = 0.0f;
  float physics_accumulator = 0.0f;

  // Our own Player_Entity::health, off the last snapshot. Prediction reads it:
  // the server stops steering a dead player, so a client that kept feeding its
  // own input into player_move would predict a walk the server never runs and
  // spend the whole respawn delay being reconciled backwards. 100 until the
  // first snapshot, which is also the not-connected case (no server, no death).
  int32_t local_player_health = 100;

  // Zoom STATE, not the click: right-click toggles it, and it rides the button
  // bitfield to the server (Button::Zoom) so the server can charge movement
  // speed or accuracy for it. That is what makes it a predicted INPUT and puts
  // it here rather than beside the eased zoom_fraction on Play_State -- as a
  // state member it survived a disconnect, so you rejoined still zoomed.
  bool zoom_active = false;

  // Real seconds since our own last predicted gunshot, for re-running the
  // server's fire-rate gate locally (weapons.hpp is shared, so it is the same
  // number). Audio only -- the authoritative limit is the server's
  // last_fire_tick -- but it is per-connection like the health above it.
  float seconds_since_local_fire = 0.f;

  int command_number = 0;
  Array<Saved_Command, MAX_PENDING_COMMANDS> pending_commands = {};

  // --- Server reconciliation ---
  vec3f latest_server_position = {0, 0, 0};
  vec3f latest_server_velocity = {0, 0, 0};
  int latest_server_ack_command = -1;
  bool received_server_update = false;
  vec3f visual_error_offset = {0, 0, 0};
  vec3f reconciliation_error = {0, 0, 0};      // HUD readout only
  float reconciliation_error_magnitude = 0.0f; // HUD readout only
};

// One bot's pathfinding state as of the last S2C_BotDebug packet. Filled from
// the wire in play_state.cpp; drawn by the bot HUD and the path overlay.
//
// This is NOT a memory bridge from game_server, though it claimed to be one as
// a `bot_debug::g_entries` global for a long time. It cannot be: game_shared is
// a static lib, so each module linked its own copy and the server's writes were
// never visible here. The server's real path is `g_bots` -> S2C_BotDebug ->
// this vector, which works in both the integrated (loopback) and networked
// builds. It sits in replication_t because that is what it is -- and because as
// a global neither reset reached it, so a previous connection's bot paths kept
// drawing over the new one.
struct bot_debug_entry_t
{
  int32_t            slot       = -1;
  int                goal       = 0; // BotGoal: 0=Idle 1=Chase 2=Attack 3=Retreat
  int                type       = 0; // BotType: 0=Idle 1=Chase 2=Regular
  std::vector<vec3f> path;
  int                path_index = 0;
};

// Everything the server tells us about the world other than ourselves. Keyed by
// entity uid / slot, both of which mean nothing in a different map or a
// different connection -- which is why this is the one group BOTH resets clear.
struct replication_t
{
  std::unordered_map<int32_t, Remote_Player_State> remote_players;
  float interpolation_time = 0.f;

  // --- Delta decompression: the latest reconstructed snapshot ---
  // What the game reads. A copy of the newest frame in the history below, kept
  // separate because the rest of the client wants "the current world", not
  // "frame N".
  std::unordered_map<int32_t, entities::Player_Entity> latest_player_entities;
  std::unordered_map<shared::entity_uid_t, entities::Rocket_Entity> remote_rockets;
  // Physics bodies received from server. State is replaced wholesale each
  // snapshot — no interpolation yet (see todo.md). Renders correctly in
  // integrated mode via server_session; in networked mode this will visibly
  // stutter at server tick boundaries until interpolation is added.
  std::unordered_map<shared::entity_uid_t, entities::Physics_Body_Entity> remote_physics_bodies;

  // --- Delta decompression: the snapshot history ---
  // The server deltas against a tick we ACKED, so we must still be holding the
  // exact state we reconstructed for that tick — not merely "the current
  // world", which has moved on. Mirrors the server's ring one for one; see
  // shared/network/snapshot_history.hpp. `acked_tick` on it is the value echoed
  // back in every C2S_PlayerMoveCommand. The frame type is the same one the
  // server stores — the server deltas against what it believes we
  // reconstructed, so the two structures being one type is not a convenience,
  // it is the guarantee. Keyed by entity uid on both ends;
  // `latest_player_entities` above is the by-slot view, rebuilt on publish.
  ::network::Snapshot_History<::network::snapshot_frame_t> snapshot_history;
  uint32_t latest_processed_tick = 0;

  // Per-player Player_Entity::last_fire_tick as of the last snapshot we looked
  // at, keyed by entity uid. An advance means that player fired; see
  // weapon_fire_audio.hpp. Keyed by uid rather than slot so a slot changing
  // occupant cannot inherit the previous player's stamp.
  std::unordered_map<shared::entity_uid_t, uint32_t> last_seen_fire_tick_per_player;

  // The same trick for OUR OWN Player_Entity::last_hit_tick — an advance means
  // a shot of ours landed. A scalar and not a map because a hitmarker is ours
  // alone: nobody else's hits are our business. `seeded` distinguishes "never
  // looked" from "looked, and it was 0", so joining a server where we already
  // have a stamp does not ding on the first snapshot.
  uint32_t last_seen_hit_tick = 0;
  bool hit_tick_seeded = false;

  // Replaced wholesale by each S2C_BotDebug packet.
  std::vector<bot_debug_entry_t> bot_debug_entries;
};

// Drawn, and read by nothing else. Dropping any of it costs at most a frame's
// appearance, which is what makes it safe to clear on any transition.
struct visual_effects_t
{
  std::vector<explosion_effect_t> explosion_effects;
  uint32_t next_explosion_index = 0;

  // Client only, since the server cannot visualize. Filled by player_move
  // during prediction, drawn and cleared in build_frame.
  debug_collision::Face_Sink debug_collision_faces;
};

struct client_context_t
{
  cvars::cvar_state_t*    cvars    = nullptr;
  cvars::command_table_t* commands = nullptr;
  audio_system_t* audio = nullptr;

  const shared::game_session_t* server_session = nullptr;
  ::network::Client_Transport_Layer transport_layer;

  ::network::Address requested_server_address =
      ::network::Address(127, 0, 0, 1, ::network::server_port_number);

  // --- Reset-scoped state ---
  local_world_t    world;
  connection_t     connection;
  prediction_t     prediction;
  replication_t    replication;
  visual_effects_t visuals;
};

void reset_for_new_connection(client_context_t& context);
void reset_for_new_map(client_context_t& context);

} // namespace client

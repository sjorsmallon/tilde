#pragma once

#include "remote_interpolation.hpp"

#include "../shared/array.hpp"
#include "../shared/cvars/generated/cvars_generated.hpp"
#include "../shared/entities/entity_reflection.hpp"
#include "../shared/game_session.hpp"
#include "../shared/network/client_transport_layer.hpp"
#include "../shared/network/entity_snapshot.hpp"
#include "../shared/network/snapshot_history.hpp"
#include "../shared/physics.hpp"
#include "../shared/player_move.hpp"
#include "../shared/subtick.hpp"

#include <memory>
#include <unordered_map>
#include <vector>

namespace client
{

struct audio_system_t; // owned by client_impl.cpp; see init()/shutdown()

namespace ui
{
struct ui_font_t; // client/ui/font.hpp
}

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
// Invariants: we only send client input / run prediction+reconciliation while
// Connected; the server withholds snapshots (its client_slot_t::map_ready) until it sees
// our C2S_MapLoaded, so a Loading client receives no entity deltas. See
// play_state.cpp update() and connection_t::awaiting_stream_content_hash.
enum class Connection_Phase { Disconnected, Connecting, Loading, Connected };

// --- Client-side prediction ring buffer entry ---
struct Saved_Input
{
  int input_number = -1;
  // The whole tick's input, edges and all, not the Move_Input it decodes to.
  // Reconciliation replays this through the SAME split_input_per_tick_into_subtick_steps the live prediction
  // ran it through, so a replay reproduces the sub-steps rather than averaging
  // them into one -- and it has to, because the split is where the clamps fire.
  // A replay that split differently would be rubber-banding, not rounding.
  // The aim rides INSIDE `input`, per step, which is why there is no yaw/pitch
  // pair beside it any more. There used to be one, and reconciliation replayed
  // every step of a tick through it while the live prediction had already moved
  // on -- two spellings of the same thing, free to disagree. A replay now runs
  // the identical basis the live step did because it reads the identical field.
  shared::subtick_input_t input = {};
  vec3f predicted_position = {0, 0, 0};
  vec3f predicted_velocity = {0, 0, 0};
};
static constexpr uint32_t MAX_PENDING_INPUTS = 128;

// --- Remote player interpolation ---
// The snapshot ring and the interpolation cursor both live in client/remote_interpolation.hpp,
// which is where the reasoning is too. What is left here is the per-player
// state around them: who the slot holds, and what the last sample rendered to.
struct Remote_Player_State
{
  int32_t slot_index = invalid_slot_idx;
  // Which entity currently occupies the slot. A slot can change occupant, and
  // interpolating across that would lerp the new player in from the old
  // player's last position.
  shared::entity_uid_t entity_uid = shared::null_entity_uid;
  bool active = false;
  // Per player rather than one shared pair: "time since arrival" means something
  // different for every entity, so the old global phase could only ever describe
  // them all by accident. The ring is what the RENDER CLOCK indexes into.
  client::interpolation_ring_t interpolation;
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
  // How far into the death clip the corpse is. Advanced by frame dt
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

  // The server holds no player entity for our slot: we are connected but not
  // playing. DERIVED each snapshot from whether our slot appears among the
  // reconstructed players, never sent — the server encodes spectating as the
  // absence of a body, so there is no flag on the wire to disagree with.
  //
  // True until a snapshot says otherwise, because that is how a connection
  // starts: you join as a spectator and `join_game` gives you a body. Only
  // meaningful while phase == Connected — an offline client has no server to
  // hold a body for it, and every reader gates on the phase for that reason.
  bool spectating = true;

  uint32_t server_tickrate = 60;

  // One log line per connection, the first time a snapshot names our own body.
  // Connection-scoped, so it resets with the rest of this group rather than
  // with the UI struct it used to sit in -- which also lets advance_newest_held_snapshot stay
  // a free function with no Play_State in its signature.
  bool logged_first_server_update = false;
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

  // Real seconds since our own last predicted gunshot, PER WEAPON, for
  // re-running the server's fire-rate gate locally (weapons.hpp is shared, so
  // it is the same number). Audio only -- the authoritative limit is the
  // server's -- but it is per-connection like the health above it.
  //
  // Keyed by weapon because the SERVER's clock is: Weapon_Entity::next_fire_time
  // is per weapon and runs while holstered. One float here was the client half
  // of exactly the bug the inventory work fixed on the server, and it would now
  // be a second answer that disagrees -- swinging a Knife would silence a Scout
  // the server is perfectly willing to fire.
  //
  // Starts at zero, which reads as "fired just now" for one interval after a
  // connect. That is the safe direction: this gate can only suppress a sound,
  // never invent one, and the first shot of a session is the server's answer
  // anyway.
  Enum_Array<entities::Weapon, float> seconds_since_local_fire = {};

  // Real seconds left on our own predicted weapon switch, or 0 when none is
  // running -- the client half of Inventory::deploy_complete_time.
  //
  // PREDICTED rather than replicated for the same reason the reload clock below
  // is: the deadline is a sub-tick value on a wire with no grid finer than a
  // tick, and the reader wants "how long left", which a client that pressed the
  // key already knows a round trip sooner than the server can say it.
  //
  // Unlike the two clocks around it this one is also DRAWN -- see
  // cl_show_deploy_timer and hud/deploy_timer.hpp -- so a disagreement with the
  // server is visible rather than merely audible. It is still not simulation:
  // being wrong costs a wrong number on screen and a wrong bang, never a desync.
  float seconds_until_local_deploy_complete = 0.f;

  // Real seconds left on our own predicted reload, or 0 when none is running.
  //
  // PREDICTED, not replicated, for exactly the reason the fire clock above is:
  // the gunshot is gated at the press and the server's answer is a round trip
  // behind. Player_Entity::reload_complete_time is deliberately server-only and
  // stays that way -- it is a sub-tick value on a wire with no grid finer than a
  // tick, and replicating it would buy a reader that only needs "am I reloading"
  // eight bytes it cannot use.
  //
  // Started, cancelled and cleared on the same conditions the server uses, off
  // this client's own key presses. Audio only: nothing simulates on it, so a
  // disagreement costs a sound and never a desync.
  float seconds_until_local_reload_complete = 0.f;

  int input_number = 0;
  Array<Saved_Input, MAX_PENDING_INPUTS> pending_inputs = {};

  // --- Sub-tick input edges ---
  //
  // The tracked buttons (Button::Subtick_Tracked) as of the last tick boundary,
  // and every transition since it that no tick has consumed yet. Together they
  // are what the next command's subtick_input_t is built from.
  //
  // `accumulator_seconds` is measured in the SAME timeline the ticks are cut
  // from -- seconds past the last tick boundary, in physics_accumulator space --
  // and not in wall-clock. That is deliberate: the accumulator is what decides
  // where a tick boundary lands, so placing an edge anywhere else means the two
  // can disagree about which side of the boundary a press fell on, which is the
  // one thing sub-tick exists to get right. Every tick consumed rebases the
  // leftovers by tick_dt, exactly as the accumulator itself is rebased.
  //
  // A frame can produce more transitions than one tick can carry
  // (MAX_SUBTICK_EDGES); the overflow is logged where it is dropped, because
  // that is input being abandoned.
  struct pending_input_edge_t
  {
    float                  accumulator_seconds = 0.f;
    uint64_t               buttons_after       = 0; // tracked bits only; the rest are polled
    shared::subtick_view_t view_after          = {};

    // Wall clock, unlike accumulator_seconds beside it, and both are needed
    // because they answer to different things. The accumulator decides which
    // TICK the transition falls in; this decides which drawn FRAME the player
    // was looking at when they made it, and the drawn frames are stamped on the
    // input clock (remote_interpolation.hpp, drawn_history_t). Deriving either
    // from the other means guessing at the offset between two clocks that are
    // read at different points in the frame.
    uint64_t arrival_qpc_ticks = 0;
  };
  std::vector<pending_input_edge_t> pending_input_edges;
  uint64_t tracked_buttons_at_tick_start = 0;

  // Where the aim was at that same boundary, and it cannot be derived from the
  // edges: a tick whose mouse moved and whose buttons did not carries no edge
  // at all, and that is the common tick. Carried across from the last sample
  // the previous tick consumed, which is by construction the last travel before
  // the boundary.
  shared::subtick_view_t view_at_tick_start = {};

  // Whether the edges above are a current account of what is held. False while
  // anything stops them being read -- the console owning the keyboard, or not
  // being Connected, so no tick loop is draining them -- and resuming from that
  // resamples the keyboard SILENTLY, because the transitions that happened while
  // parked were genuinely never seen. It is the difference between "we missed
  // these on purpose" and a lost event, and only the second one is worth a log
  // line.
  bool input_edges_are_live = false;

  // Every move built but not yet acked by the server, oldest first, resent whole
  // in each move datagram. Held as the BUILT messages rather than rebuilt from
  // pending_inputs above: a resend has to carry the held_snapshot_tick and the
  // interpolation bracket the command was issued with, and those describe the
  // moment it was made, not the moment it is re-sent. Saved_Input has neither
  // -- it exists for reconciliation, which replays movement and nothing else.
  //
  // Trimmed by latest_input_number_processed_by_server, which is a high-water
  // mark, so this needs no per-input acking. Capped by cl_max_unacked_inputs.
  std::vector<game::C2S_ClientInput> unacked_inputs;

  // --- Server reconciliation ---
  vec3f latest_server_position = {0, 0, 0};
  vec3f latest_server_velocity = {0, 0, 0};
  // Our copy of the server's high-water mark over our own input stream. Spelled
  // out to the last word because both halves are load-bearing: it is a NUMBER,
  // not an input, and the processing is the SERVER'S -- we process our own
  // inputs too, every tick, as prediction. Everything at or below it is done, so
  // it both trims unacked_inputs and is where reconciliation starts replaying.
  int latest_input_number_processed_by_server = -1;
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

  // WHERE ON THE SERVER'S TICK AXIS THE CLIENT IS DRAWING. One per connection:
  // every remote entity's ring is indexed by this same clock, which is what a
  // shared ABSOLUTE coordinate buys and a shared arrival phase could not.
  //
  // It replaced a float reset to zero on every snapshot arrival. That reset was
  // `cursor_tick = newest_received_tick - 1` written as an assignment, and it
  // was only ever correct when the clock had already reached exactly that value
  // -- the discarded remainder was the pop. See remote_interpolation.hpp.
  client::interpolation_cursor_t interpolation_cursor;

  // One entry per presented frame: what the remote players were drawn at, and
  // when. A trigger press is judged against the entry that was in front of the
  // player at the moment it arrived, which is the only way to answer that
  // question without deriving it from a clock that never measured it.
  client::drawn_history_t drawn_history;

  // --- Delta decompression: the latest reconstructed snapshot ---
  // What the game reads. A copy of the newest frame in the history below, kept
  // separate because the rest of the client wants "the current world", not
  // "frame N".
  std::unordered_map<int32_t, entities::Player_Entity> latest_player_entities;
  // Every weapon in the world, keyed by uid -- ours and everyone else's, since
  // the server has no relevance filtering. Resolved through our own
  // Player_Entity::inventory.weapons, the same forward list the server uses,
  // rather than by scanning for one whose owner_uid is us: deriving the
  // inventory from the back-reference is a second answer to "what am I
  // carrying" and the two are free to disagree.
  std::unordered_map<shared::entity_uid_t, entities::Weapon_Entity> latest_weapon_entities;
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
  // back in every C2S_ClientInput. The frame type is the same one the
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
  debug_collision::Face_Bucket debug_collision_faces;
};

struct client_context_t
{
  cvars::cvar_state_t*    cvars    = nullptr;
  cvars::command_table_t* commands = nullptr;
  audio_system_t* audio = nullptr;
  // The HUD font. Borrowed, like `audio` above: client_impl.cpp owns it and
  // neither reset touches it, because a font means the same thing in every map
  // and on every connection.
  const ui::ui_font_t* font = nullptr;

  const shared::game_session_t* server_session = nullptr;
  ::network::Client_Transport_Layer transport_layer;

  // One frame's drained messages. Not reset-scoped: it is refilled from scratch
  // and fully consumed every frame, and lives here only so its vectors keep
  // their capacity instead of reallocating at the frame rate. Reassembly state
  // is NOT here -- it is transport_layer.partial_packets, a stratum down, which
  // is what lets a fragmented message span as many frames as it needs.
  ::network::Client_Inbox incoming;

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
void reset_state_in_preparation_for_new_map_load(client_context_t& context);

} // namespace client

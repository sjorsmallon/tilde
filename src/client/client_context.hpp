#pragma once

#include "audio/audio_system.hpp"
#include "camera.hpp"
#include "entities/generated/entities/player_entity_generated.hpp"
#include "remote_interpolation.hpp"
#include "replay_playback.hpp"

#include "../shared/array.hpp"
#include "../shared/cvars/generated/cvars_generated.hpp"
#include "../shared/entities/entity_reflection.hpp"
#include "../shared/map/game_session.hpp"
#include "../shared/replay/ghost.hpp"
#include "../shared/network/client_transport_layer.hpp"
#include "../shared/network/entity_snapshot.hpp"
#include "../shared/network/snapshot_history.hpp"
#include "../shared/map/map.hpp"
#include "../shared/replay/replay_recorder.hpp"
#include "../shared/movement/player_move.hpp"
#include "../shared/round_phase_rules.hpp"
#include "../shared/input_step.hpp"
#include "../shared/subtick.hpp"
#include "../shared/beam_ripples.hpp"
#include "../shared/team_wall_ripples.hpp"
#include "../shared/lighting/lens.hpp"
#include "../shared/lighting/shadow_volume.hpp"

#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace client
{

struct Audio_System; // owned by client_impl.cpp; see init()/shutdown()

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
//   Loading ──local copy present & hash matches─▶ Connected
//           ──S2C_MapData arrives & verifies────▶ Connected
//           ──load/verify fails─────────────────▶ (stay Loading; re-request)
//
// Nothing is SENT on the two Loading -> Connected edges, and that is the point:
// the map we hold rides every C2S_ClientInput as `map_content_hash`, so the
// server derives client_slot_t::map_ready by comparing it rather than waiting
// for an ack it could lose. Entering Connected starts the input flow, and
// snapshots resume one tick later.
//
// Invariants: we only send client input / run prediction+reconciliation while
// Connected; the server withholds snapshots (its client_slot_t::map_ready)
// until an input reports the hash it is running, so a Loading client -- which
// sends no input at all -- receives no entity deltas. See play_state.cpp
// update() and connection_t::downloading_map_content_hash.
// Replaying has no peer at all: a file fills the inbox (replay_playback.hpp), so
// every gate that assumes a server -- input, the reliable stream, the disconnect
// message -- is off by being a different phase rather than a flag beside Connected.
enum class Connection_Phase { Disconnected, Connecting, Loading, Connected, Replaying };

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
};
static constexpr uint32_t MAX_PENDING_INPUTS = 128;

// --- Remote player interpolation ---
// The snapshot rings and the interpolation cursor both live in client/remote_interpolation.hpp,
// which is where the reasoning is too. What is left here is the per-player
// state around them: who the slot holds, and what the last sample rendered to.
// The player's pose is read from replication_t::interpolated_entities by its
// uid, the same ring every other replicated entity has.
struct Remote_Player_State
{
  int32_t slot_index = invalid_slot_idx;
  // Which entity currently occupies the slot. A slot can change occupant, and
  // the death timer below must not carry across that.
  shared::entity_uid_t entity_uid = shared::null_entity_uid;
  bool active = false;
  vec3f drawn_position = {0, 0, 0};
  float drawn_yaw = 0.f;
  float drawn_pitch = 0.f;
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

  // The map the session was built from, kept for the replay recorder to embed.
  shared::map_t map;

  // FNV-1a hash of the map the client loaded, via compute_map_content_hash().
  // Verified against the server's CmdAccept.content_hash to detect a
  // client/server map mismatch. 0 = not computed (verification skipped).
  uint32_t map_content_hash = 0;

  // The ghost the server last announced, out of our cache or off the wire; empty when there is none or it
  // is still on its way. The hash is what an arriving S2C_GhostData is checked against.
  std::optional<shared::ghost_t> ghost;
  uint32_t                       ghost_hash_the_server_announced = 0;

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
  // handler so a second switch message naming the map we are ALREADY
  // downloading is ignored rather than restarting the transfer under us --
  // begin_paced_transfer replaces whatever a slot was sending. Cleared once
  // S2C_MapData applies.
  uint32_t downloading_map_content_hash = 0;

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
  // The local player as this client predicts them: the hull, the hand with its clocks and
  // magazines, and the three toggle flags, advanced by the same apply_input_step_to_player
  // the server runs (input_step_plan.md). Reconciliation snaps the whole of it back to
  // latest_server_state and replays the unacked inputs through the same function; between
  // corrections this is what the local player's abilities are actually spending.
  shared::player_input_state_t state = {.move = {.feet = {0.f, 36.f, 0.f}}};

  float player_yaw = 0.0f;
  float player_pitch = 0.0f;
  float physics_accumulator = 0.0f;

  // The aim a Pilot flight was launched with, handed back when the ride ends; empty while not riding.
  std::optional<shared::subtick_view_t> aim_at_pilot_launch;

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
  // -- it exists for reconciliation, which replays the input and nothing else.
  //
  // Trimmed by latest_input_number_processed_by_server, which is a high-water
  // mark, so this needs no per-input acking. Capped by cl_max_unacked_inputs.
  std::vector<game::C2S_ClientInput> unacked_inputs;

  // --- Server reconciliation ---
  // The server's word on us, read off the newest snapshot by the builder the server reads a
  // step's state with; what a replay restarts from.
  shared::player_input_state_t latest_server_state = {};
  // Our copy of the server's high-water mark over our own input stream. Spelled
  // out to the last word because both halves are load-bearing: it is a NUMBER,
  // not an input, and the processing is the SERVER'S -- we process our own
  // inputs too, every tick, as prediction. Everything at or below it is done, so
  // it both trims unacked_inputs and is where reconciliation starts replaying.
  int latest_input_number_processed_by_server = -1;
  // The tick of the snapshot that carried latest_server_state. Input N is
  // predicted to run at latest_server_tick + (N - the processed number above),
  // which is where a mover is for it (mover_def.md ss12).
  uint32_t latest_server_tick = 0;
  // latest_server_tick less the processed number, eased: what DRAWING adds to an input number. The step keeps the exact one.
  double drawn_tick_gap = 0.0;
  bool received_server_update = false;

  // The canopy state.move.feet stands on and where the step that put it there guessed that canopy to be.
  struct ridden_canopy_t
  {
    shared::entity_uid_t uid              = shared::null_entity_uid;
    vec3f                guessed_position = {0, 0, 0};
  };
  std::optional<ridden_canopy_t> ridden_canopy;

  // Drawn, never simulated: carries the eye from the guessed canopy to the drawn one. Written once per frame.
  vec3f                drawn_carry_shift = {0, 0, 0};
  shared::entity_uid_t drawn_carry_uid   = shared::null_entity_uid;

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
  int                goal       = 0; // bot_goal_t: 0=Idle 1=Chase 2=Attack 3=Retreat
  int                type       = 0; // bot_behavior_t: 0=Idle 1=Chase 2=Regular
  std::vector<vec3f> path;
  int                path_index = 0;
};

// Everything the server tells us about the world other than ourselves. Keyed by
// entity uid / slot, both of which mean nothing in a different map or a
// different connection -- which is why this is the one group BOTH resets clear.
struct replication_t
{
  std::unordered_map<int32_t, Remote_Player_State> remote_players;

  // One ring per replicated entity with a Render, players included and the
  // local player excluded. Fed by the snapshot apply and read at the cursor by
  // the draw, so a rocket, a crate and a remote body all move between ticks
  // rather than in steps of one. Per entity rather than one shared pair: "time
  // since arrival" means something different for every entity, so a global
  // phase could only ever describe them all by accident.
  client::interpolated_entities_t interpolated_entities;

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

  // --- Delta decompression: the snapshot history ---
  // The server deltas against a tick we ACKED, so we must still be holding the
  // exact state we reconstructed for that tick — not merely "the current
  // world", which has moved on. Mirrors the server's ring one for one; see
  // shared/network/snapshot_history.hpp. `acked_tick` on it is the value echoed
  // back in every C2S_ClientInput. The frame type is the same one the
  // server stores — the server deltas against what it believes we
  // reconstructed, so the two structures being one type is not a convenience,
  // it is the guarantee.
  //
  // There is deliberately no "latest frame" copy beside it. The newest frame is
  // applied INTO world.session.entity_system, which is the client's ONE world:
  // every replicated entity, map-placed or spawned by the server, is read from
  // there by uid. A frame in this ring is the delta baseline and the previous
  // state the edge watchers compare against, and nothing else reads one.
  ::network::Snapshot_History<::network::snapshot_frame_t> snapshot_history;
  uint32_t latest_processed_tick = 0;

  // Looping emitters are not built; said once per connection rather than per tick.
  bool loop_emitters_unbuilt_reported = false;

  // The voices each emitter started, so Stop_Playing has something to name.
  std::unordered_map<shared::entity_uid_t, std::vector<client::voice_handle_t>> emitter_voices;

  // Replaced wholesale by each S2C_BotDebug packet.
  std::vector<bot_debug_entry_t> bot_debug_entries;
};

// What a player's body was last drawn as. Kept per uid so that when its teleport_count changes the frame after,
// the afterimage stands exactly where the body was drawn and not where the snapshot says it was.
struct drawn_body_t
{
  vec3f   feet           = {0, 0, 0};
  float   yaw            = 0.f;
  float   pitch          = 0.f;
  float   body_yaw       = 0.f;
  uint8_t teleport_count = 0;
};

// The body a player was cut away from: drawn where it stood, in the aim pose it left in, dissolving over
// cl_afterimage_seconds under a puff of dust.
struct afterimage_t
{
  vec3f            feet          = {0, 0, 0};
  float            body_yaw      = 0.f;
  float            pitch         = 0.f;
  float            yaw_deviation = 0.f;
  entities::Render render;
  float            age_seconds   = 0.f;
  uint32_t         index         = 0;
};

// Drawn, and read by nothing else. Dropping any of it costs at most a frame's
// appearance, which is what makes it safe to clear on any transition.
struct visual_effects_t
{
  std::vector<explosion_effect_t> explosion_effects;
  uint32_t next_explosion_index = 0;

  // Running step counts, so the variants cycle per foot owner: ours from the
  // prediction, everyone else's from the Footstep effect, keyed by uid.
  uint32_t                                          own_footstep_count = 0;
  std::unordered_map<shared::entity_uid_t, uint32_t> footstep_counts;

  std::unordered_map<shared::entity_uid_t, drawn_body_t> drawn_bodies;
  std::vector<afterimage_t>                              afterimages;
  uint32_t                                               next_afterimage_index = 0;
  // Seconds since the eye's own cut, while r_cut_wipe_seconds has the old view still peeling away; below 0 otherwise.
  float cut_wipe_seconds = -1.f;
  // The tonemap's page for the Red Lens view: how far it has faded in over r_lens_view_seconds, and the filter
  // it last faded in with, so a lens put away fades out in its own colour. The view itself is the prediction's
  // held_lens_on, never this.
  float                 lens_view_fraction = 0.f;
  entities::Lens_Filter lens_view_filter   = entities::Lens_Filter::Red;

  // Client only, since the server cannot visualize. Filled by player_move
  // during prediction, drawn and cleared in draw.
  debug_collision::Face_Bucket debug_collision_faces;

  // Where players came through a team wall lately, and who is inside one now.
  // Derived per frame from positions every client already holds, so nothing is
  // networked; aged in retire_per_frame_visuals, drawn by the ghost shader.
  shared::wall_ripple_state_t wall_ripples;
  // Where players landed on a solid beam lately, and who stands on one now; derived per frame as the above.
  shared::beam_ripple_state_t beam_ripples;

  // The shadow volumes the newest predicted tick was cut with, copied each update so the draw
  // cuts the same volumes the floor did (shadow_volume_plan.md ss5).
  std::vector<shared::shadow_volume_t> drawn_shadow_volumes;
  // The lens watchers that tick was held by, own eye from the prediction, copied for the same reason:
  // a lens_watched draw keeps what those eyes hold solid.
  std::vector<shared::lens_watcher_t>  drawn_lens_watchers;
  // What those volumes are drawn up to, for the first renderer::MAX_SCENE_SHADOW_VOLUMES of them, and how
  // many pieces outran the plane slots (shadow_volume_plan.md ss5).
  std::vector<shared::shadow_occluder_t> drawn_shadow_occluders;
  uint32_t                               drawn_shadow_occluders_skipped = 0;
  // The same cut's counts, and the line cl_shadow_volume_debug last printed for them.
  shared::shadow_volume_report_t       drawn_shadow_volume_report;
  std::string                          shadow_volume_debug_line;
  // The solid beams the same cut carved, copied only while cl_solid_beam_debug draws them.
  std::vector<shared::mover_t>         drawn_solid_beams;

  // Which local input number the run's pose 0 is, latched once per Live phase so the ghost
  // advances with our own input counter rather than with snapshot arrival.
  bool     ghost_clock_latched          = false;
  uint32_t ghost_clock_phase_start_tick = 0;
  int      ghost_clock_first_input      = 0;
};

struct client_context_t
{
  cvars::cvar_state_t*    cvars    = nullptr;
  cvars::command_table_t* commands = nullptr;
  // Inert until client_impl.cpp calls init(), and inert again after shutdown():
  // every play_* on a system with no device is a no-op, so nobody checks.
  Audio_System audio;
  // The HUD font. Borrowed: client_impl.cpp owns it and neither reset touches
  // it, because a font means the same thing in every map and on every connection.
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

  // Asked for by whoever sent us to Play_State: the editor's play button means
  // "play this map", not "spectate the map I just saved". Play_State reads it
  // once in on_enter and clears it, so the request belongs to that one entry
  // into the play state rather than to whatever connects next -- unlike the address
  // above, which is deliberately remembered.
  //
  // Not in the connection group: it is set BEFORE on_enter, which resets that.
  bool requested_join_on_connect = false;

  // The editor camera F1 left from: the first living body this connection gets is
  // moved there and aimed along it. Read once and cleared, like the join above.
  std::optional<camera_t> requested_spawn_view;

  // The other direction: the view play was showing when it went back to the
  // editor, which the editor camera starts from instead of its default.
  std::optional<camera_t> requested_editor_view;

  // What THIS client receives, one file per map per connection. Outside the
  // groups: both resets FINISH it rather than wipe it, so the index is written.
  shared::replay_recorder_t replay_recorder;

  // replay_def.md §5. `requested_replay` is the file `replay_play` opened, taken
  // by Play_State::on_enter the way requested_server_address is; `replay` is the
  // playback it became, ended by on_exit.
  std::optional<shared::replay_t> requested_replay;
  replay_playback_t               replay;

  // --- Reset-scoped state ---
  local_world_t    world;
  connection_t     connection;
  prediction_t     prediction;
  replication_t    replication;
  visual_effects_t visuals;
};

void reset_for_new_connection(client_context_t& context);
void reset_state_in_preparation_for_new_map_load(client_context_t& context);
// A replay jump: everything keyed to where playback was, and the @Mirrored cvars
// back to their pre-playback values so the file's records up to the target
// rebuild them.
void reset_state_for_replay_seek(client_context_t& context);
void snap_local_aim_to(prediction_t& prediction, const linalg::quatf& orientation);

// The Player_Entity a connection slot has a body for, or nullptr: a spectator,
// an empty slot, or a connect the server has not answered yet. A walk of the
// player pool rather than a by-slot index -- players are few, and an index
// would be a second copy of client_slot_index. Same lifetime rule as get<T>.
[[nodiscard]] const entities::Player_Entity* try_find_player_in_slot(const client_context_t& context,
                                                                    int32_t slot);
[[nodiscard]] const entities::Player_Entity* try_find_my_player(const client_context_t& context);
// Whose HUD is drawn: the cl_spectate_slot player in a replay, otherwise our own body.
[[nodiscard]] const entities::Player_Entity* try_find_viewed_player(const client_context_t& context);

// The session's Game_Rules_Entity's match, or nullptr before the world has one.
[[nodiscard]] const entities::Match* try_find_match(const client_context_t& context);

// "maps", or MAPS_DIR when set (the cold-client streaming test).
[[nodiscard]] std::string client_maps_directory();

} // namespace client

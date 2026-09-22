#pragma once

#include "../../shared/hitbox_rig.hpp" // posed_hitbox_t, for the overlay scratch below
#include "../../shared/array.hpp"
#include "../shot_debug.hpp"
#include "../../shared/player_constants.hpp"
#include "../../shared/skinning.hpp"
#include "../camera.hpp"
#include "../game_state.hpp"
#include "../hud/scoreboard.hpp"
#include "pause_menu.hpp"
#include "../shared/game_session.hpp"
#include "../shared/network/client_transport_layer.hpp"
#include "../shared/network/network_types.hpp"
#include "../frame_builder.hpp"
#include "../skybox_selection.hpp"
#include "../state_manager.hpp"
#include "imgui.h"
#include "physics.hpp"
#ifdef JPH_DEBUG_RENDERER
#include "../jolt_debug_renderer.hpp"
#endif
#include <deque>
#include <memory>
#include <vector>

namespace shared
{
struct map_package_t; // defined in shared/network/map_transfer.hpp
}

namespace client
{

// One frame's resolved values, handed from step to step down Play_State::update.
// Defined in play_state.cpp: nothing outside that file builds one or reads one,
// and it is a local of `update` rather than a member because nothing in it
// survives a frame.
struct play_frame_t;

class Play_State : public Game_State
{
public:
  void on_enter() override;
  void on_exit() override;
  void update(float dt) override;
  void draw_imgui_panels() override;
  void build_frame(float delta_seconds, std::vector<renderer::view_pass_t> &passes,
                   renderer::ui_draw_list_t &ui) override;

private:

  bool load_client_map(const std::string &map_path);
  bool apply_map_package(const shared::map_package_t &package);
  void switch_to_map(const shared::map_t &map);
  void set_provisional_player_pose_for_new_map(client_context_t &ctx);

  // The steps of `update`, in the order it runs them: receive, simulate,
  // render. tick_def.md is the design of record, and its "The client" section
  // is the rule each of these is checked against -- nothing writes a session
  // entity outside snapshot apply and own-player prediction.
  //
  // update_shell returns TRUE when it switched state, in which case this object
  // no longer exists and the caller must return immediately.
  [[nodiscard]] bool update_shell(client_context_t &ctx, play_frame_t &frame);
  void receive_from_server(client_context_t &ctx, play_frame_t &frame);
  void retire_per_frame_visuals(client_context_t &ctx, play_frame_t &frame);
  void reconcile_with_server(client_context_t &ctx, play_frame_t &frame);
  void resolve_aim_and_buttons(client_context_t &ctx, play_frame_t &frame);
  void place_input_edges_on_the_tick_timeline(client_context_t &ctx, play_frame_t &frame);
  void run_predicted_ticks(client_context_t &ctx, play_frame_t &frame);
  void play_local_movement_sounds(client_context_t &ctx, play_frame_t &frame);
  void advance_render_state(client_context_t &ctx, play_frame_t &frame);
  void ripple_team_walls(client_context_t &ctx, play_frame_t &frame);
  void resolve_camera(client_context_t &ctx, play_frame_t &frame);
  void update_audio_listener(client_context_t &ctx, play_frame_t &frame);


  void enter_connected_phase();
  void enter_replay_playback(shared::replay_t&& replay);

  camera_t camera;

  // there's some awkwardness with noclip camera that we don't really move
  // but snap to a new position.
  camera_t noclip_camera;
  bool noclip_was_active = false;

  pass_builder_t scene;

  // Resolves sv_skybox's text once per change. The value arrives over the cvar
  // mirror, so it can land AFTER the map did -- which is why this is asked
  // every frame rather than once at load.
  skybox_selection_t skybox;


  std::deque<assets::posed_skeleton_t> pose_storage;
  size_t pose_count = 0;


  // Player dimensions — canonical values live in shared::player_half_width/height
  static constexpr float player_half_width = shared::player_half_width;
  static constexpr float player_half_height = shared::player_half_height;

  struct per_connection_ui_t
  {
    float zoom_fraction = 0.0f;

    bool mouse_captured        = true;
    bool console_was_open      = false;
    bool show_pause_menu     = false;
    bool menu_overlay_was_open = false;
  };

  per_connection_ui_t connection_ui;
  ui::list_menu_t pause_menu;

  // This trip's pending `join_game`, taken off client_context_t in on_enter and
  // sent by enter_connected_phase. It cannot be sent any earlier: the line is
  // @Server, so it needs the forwarder that entering Connected installs.
  bool pending_match_join = false;

  // Storage, not state: refilled from the latest snapshot every frame the board
  // is up. Sized by the connection slot count because that IS the row bound --
  // one row per player, and a player needs a slot -- so collect_scoreboard_rows
  // is handed the whole thing and hands back the prefix it filled.
  Array<hud::scoreboard_row_t, network::sv_max_client_count> scoreboard_rows;

  // debug / tracking information.
  static constexpr int FPS_HISTORY_SIZE = 64;
  float dt_history[FPS_HISTORY_SIZE] = {};
  int dt_history_index = 0;
  int dt_history_count = 0;

#ifdef JPH_DEBUG_RENDERER
  std::unique_ptr<client::jolt_debug_renderer_t> jolt_debug_renderer;
#endif

  // Scratch for the debug_show_hitboxes overlay, reused across players and
  // frames. Unlike pose_storage this needs no stable address -- the volumes are
  // drawn on the spot and the debug list copies them -- it is here purely so the
  // overlay stops allocating per player per frame.
  std::vector<assets::posed_hitbox_t> hitbox_scratch;

  // What this client believed at each recent trigger pull, waiting to be paired
  // with the server's S2C_ShotDebug reply a round trip later. Lives here rather
  // than on client_context_t because nothing outside this state records into it
  // or draws out of it.
  client::shot_debug_history_t shot_debug_history;

};

} // namespace client

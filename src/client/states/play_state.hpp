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


  void enter_connected_phase();

  camera_t camera;
  pass_builder_t scene;


  std::deque<assets::posed_skeleton_t> pose_storage;
  size_t                               pose_count = 0;


  // Player dimensions — canonical values live in shared::player_half_width/height
  static constexpr float player_half_width  = shared::player_half_width;
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

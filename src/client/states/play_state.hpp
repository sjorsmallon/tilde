#pragma once

#include "../../shared/hitbox_rig.hpp" // posed_hitbox_t, for the overlay scratch below
#include "../../shared/player_constants.hpp"
#include "../../shared/skinning.hpp"
#include "../camera.hpp"
#include "../game_state.hpp"
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
  void render_ui() override;
  void build_frame(float delta_seconds, std::vector<renderer::view_pass_t> &passes) override;

private:



  bool load_client_map(const std::string &map_path);
  bool apply_map_package(const shared::map_package_t &package);
  void finalize_client_map(const shared::map_t &map);

  // Transitions the connection to Connected: flags it, sets the phase, and
  // registers the console network-forwarder. Shared by the direct-connect
  // (hash match) path and the post-download (streamed map) path.
  void enter_connected_phase();

  // Camera (first person, follows player). DERIVED, every frame, from
  // context.prediction (position, yaw, pitch) or from a spectated player's
  // interpolated render pose. Nothing here is ever read back into prediction --
  // that is what lets the spectate override at the end of update() write
  // camera.yaw alone without desyncing what we send to the server.
  camera_t camera;

  // The one view pass this state draws, and the storage its spans point into.
  // A member rather than a local because the debug list carries entries with
  // lifetimes across frames, and because the vectors keep their capacity.
  pass_builder_t scene;

  // One posed skeleton per posed player, reused every frame. mesh_draw_t holds
  // a Span into these and nothing is recorded until render_frame, so they
  // cannot be locals. A deque rather than a vector because growing must not
  // move the slots already handed out.
  std::deque<assets::posed_skeleton_t> pose_storage;
  size_t                               pose_count = 0;

  // Scratch for the debug_show_hitboxes overlay, reused across players and
  // frames. Unlike pose_storage this needs no stable address -- the volumes are
  // drawn on the spot and the debug list copies them -- it is here purely so the
  // overlay stops allocating per player per frame.
  std::vector<assets::posed_hitbox_t> hitbox_scratch;

  // Player dimensions — canonical values live in shared::player_half_width/height
  static constexpr float player_half_width  = shared::player_half_width;
  static constexpr float player_half_height = shared::player_half_height;

  // Per-connection UI state, cleared wholesale by on_enter -- the same rule the
  // context's lifetime groups follow, and for the same reason: states are
  // long-lived singletons, so anything NOT in here survives a disconnect and
  // rejoin. Membership test: does this mean anything to a different connection?
  //
  // It used to mean the menu overlay was still up when you re-entered play
  // after leaving with F1 (which is handled ahead of the overlay bail), and
  // that "first server update" logged once per process instead of once per
  // connect.
  struct per_connection_ui_t
  {
    // The eased follower for context.prediction.zoom_active: 0 = r_fov,
    // 1 = r_zoom_fov, over r_zoom_easing_time_between_fovs. Purely local
    // presentation -- the toggle itself is a predicted input and lives in
    // prediction_t, because the server is told we are zoomed.
    float zoom_fraction = 0.0f;

    bool mouse_captured        = true;
    bool console_was_open      = false;
    bool show_menu_overlay     = false;
    bool menu_overlay_was_open = false;

    bool logged_first_server_update = false;
  };
  per_connection_ui_t ui;

  // FPS averaging ring buffer. Outside `ui` on purpose: process-lifetime
  // scratch, and carrying it across a reconnect costs nothing.
  static constexpr int FPS_HISTORY_SIZE = 64;
  float dt_history[FPS_HISTORY_SIZE] = {};
  int dt_history_index = 0;
  int dt_history_count = 0;

#ifdef JPH_DEBUG_RENDERER
  std::unique_ptr<client::jolt_debug_renderer_t> jolt_debug_renderer;
#endif
};

} // namespace client

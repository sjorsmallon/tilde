#pragma once

#include "../../../shared/player_animator.hpp"
#include "../../../shared/animation.hpp"
#include "../../../shared/asset.hpp"
#include "../../../shared/hitbox_rig.hpp"
#include "../../../shared/skinning.hpp"
#include "../editor_tool.hpp"

#include <string>
#include <vector>

namespace client
{

class Animation_Tool : public Editor_Tool
{
public:
  void on_enable(editor_context_t &ctx) override;
  void on_disable(editor_context_t &ctx) override;
  void on_update(editor_context_t &ctx, const viewport_state_t &view, float dt) override;

  void on_mouse_down(editor_context_t &ctx, const input::mouse_event_t &e) override;
  void on_mouse_drag(editor_context_t &ctx, const input::mouse_event_t &e) override;
  void on_mouse_up(editor_context_t &ctx, const input::mouse_event_t &e) override;
  void on_key_down(editor_context_t &ctx, const key_event_t &e) override;

  void on_draw_overlay(editor_context_t &ctx, pass_builder_t &draws) override;
  void on_draw_ui(editor_context_t &ctx) override;

  // Centres the keypad axis views on the POSED model rather than on the world
  std::optional<view_focus_t> view_focus() const override;

private:
  
  enum class Pose_Source
  {
    Bind,        // T-Pose,
    Single_Pose, // one authored aim pose at full weight, no blending
    Aim_Blend,   // the live (pitch, yaw) blend, exactly as the game drives it
    Clip         // one `.animation` played on a clock, scrubbable
  };


  bool update_pose();


  linalg::vec3f bone_head(uint32_t bone_index) const;

  
  linalg::vec3f to_world(const linalg::vec3f &model_space_point) const;
  linalg::vec3f to_world_direction(const linalg::vec3f &model_space_vector) const;
  void load_rig();

  void refresh_derivation();
  void draw_hitbox_panel();
  void draw_clip_panel();
  void scan_clips();
  void load_selected_clip_idx();
  void advance_clip(float dt);

  Pose_Source        pose_source = Pose_Source::Aim_Blend;
  entities::Aim_Pose single_pose = entities::Aim_Pose::Forward;

  // used for blending. yaw_deviation instead of absolute yaw because left/right blending
  // should happen w.r.t the forward pose.

  float pitch_degrees         = 0.0f;
  float yaw_deviation_degrees = 0.0f;

  float model_yaw_degrees = 0.0f;

  static constexpr int NO_CLIP_SELECTED = -1;
  std::vector<std::string> clip_paths;
  int selected_clip_idx = NO_CLIP_SELECTED;

  assets::asset_handle_t<assets::animation_asset_t> clip_handle;
  float clip_phase          = 0.0f;
  bool  clip_playing        = false;
  bool  clip_looping        = true;
  float clip_playback_speed = 1.0f;

  // visual toggles
  bool show_mesh            = true;
  bool show_hitboxes        = true;
  bool wireframe            = false;
  bool show_skeleton        = true;
  bool show_bone_names      = false;
  bool show_movement_hull   = true;
  bool unlit                = false;

  // Highlighted in the overlay and named in the panel. -1 is none.
  static constexpr int NO_BONE_SELECTED = -1;
  static constexpr int NO_VOLUME_SELECTED = -1;
  int selected_bone_idx   = NO_BONE_SELECTED;
  int selected_volume_idx = NO_VOLUME_SELECTED;

  // Resized once, reused every frame.
  assets::pose_t           pose;
  assets::posed_skeleton_t posed_skeleton;

  assets::asset_handle_t<assets::mesh_asset_t> mesh_handle;
  const assets::skeleton_t* skeleton = nullptr;

  // toggle to silence repeat failures.
  bool model_failure_logged = false;



  std::string                        rig_path;
  assets::hitbox_rig_t               rig;
  std::vector<assets::hitbox_seed_t> seeds;

  // Refilled every frame from the live pose
  std::vector<assets::posed_hitbox_t> hitboxes;

  
  // diagnostics
  // Audit readouts. Coverage is a bind-pose property and is computed at load;
  // excursion is per-pose and is recomputed with the capsules.
  assets::hitbox_coverage_t coverage;
  assets::hull_excursion_t  excursion;
  bool rig_load_attempted = false;
};

} // namespace client

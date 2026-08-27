#pragma once

#include "../../../shared/animation.hpp"
#include "../../../shared/asset.hpp"
#include "../../../shared/assets/generated/assets_generated.hpp"
#include "../../../shared/hitbox_rig.hpp"
#include "../../../shared/player_animator.hpp"
#include "../../../shared/skinning.hpp"
#include "../editor_tool.hpp"

#include <optional>
#include <vector>

namespace client
{

// what is the source of the pose currently on the screen.
enum class pose_source_t
{
  Bind,        // T-Pose,
  Single_Pose, // one authored aim pose at full weight, no blending
  Aim_Blend,   // the live (pitch, yaw) blend, exactly as the game drives it
  Clip         // one `.animation` played on a clock, scrubbable
};

struct preview_model_t
{
  assets::asset_handle_t<assets::mesh_asset_t> mesh;
  const assets::skeleton_t* skeleton = nullptr;
  assets::pose_t sampled_pose;
  assets::posed_skeleton_t posed_skeleton;
  bool posed = false;

  // Set pessimistically each attempt and cleared on success, so a run of
  // failing frames logs once and the frame after a success logs again.
  bool failure_logged = false;
};

struct pose_controls_t
{
  pose_source_t source = pose_source_t::Aim_Blend;
  entities::Aim_Pose single_pose = entities::Aim_Pose::Forward;
  float pitch_degrees = 0.0f;
  float yaw_deviation_degrees = 0.0f;
};

struct clip_playback_t
{
  // missing is used as a placeholder, I guess.
  assets::animation_asset selected = assets::animation_asset::Missing;
  assets::asset_handle_t<assets::animation_asset_t> handle;
  float phase = 0.0f;
  float playback_speed = 1.0f;
  bool playing = false;
  bool looping = true;
};

struct hitbox_workspace_t
{
  static constexpr int NO_HITBOX_VOLUME_SELECTED = -1;

  // nullopt means the manifest has no `.hitboxes` for this skeleton -- which is
  // the tool's seeding case, not a failure.
  std::optional<assets::hitbox_rig> source;
  assets::hitbox_rig_t rig;
  // this is used as a base to actually create the hitbox rig from.
  std::vector<assets::guesstimated_hitbox_from_bone_t> guesstimated_hitboxes_from_bones;
  std::vector<assets::posed_hitbox_t> posed_hitboxes; // refilled every frame from the live pose

  // coverage is a bind-pose property and is computed at load; excursion is
  // per-pose and is recomputed with the volumes.
  assets::hitbox_coverage_t coverage;
  assets::hull_excursion_t  excursion;

  int selected_volume_index = NO_HITBOX_VOLUME_SELECTED;
  bool load_attempted = false;
};

struct display_options_t
{
  static constexpr int NO_BONE_SELECTED = -1;

  bool mesh = true;
  bool wireframe = false;
  bool unlit = false;
  bool skeleton = true;
  bool bone_names = false;
  bool hitboxes = true;
  bool movement_hull = true;

  float model_yaw_degrees = 0.0f;
  int selected_bone_index = NO_BONE_SELECTED;
};

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
  preview_model_t model;
  pose_controls_t pose_controls;
  clip_playback_t clip;
  hitbox_workspace_t workspace;
  display_options_t display;

  // Read once per frame in on_update rather than at each use, so the sliders,
  // the blend readout and the pose the game would draw cannot be reading
  // different cvars.
  aim_settings_t aim_settings;
};

} // namespace client

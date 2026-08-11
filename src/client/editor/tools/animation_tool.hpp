#pragma once

#include "../../../shared/player_animator.hpp"
#include "../../../shared/animation.hpp"
#include "../../../shared/asset.hpp"
#include "../../../shared/hitbox_rig.hpp"
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

  void on_draw_overlay(editor_context_t &ctx, overlay_renderer_t &renderer) override;
  void on_draw_ui(editor_context_t &ctx) override;

  // Centres the keypad axis views on the POSED model rather than on the world
  std::optional<view_focus_t> view_focus() const override;

private:
  // Where the pose comes from. Bind is not "no pose" -- it is the pose the mesh
  // was skinned in, and it is the reference every other view is judged against.
  enum class Pose_Source
  {
    Bind,        // T-Pose,
    Single_Pose, // one authored aim pose at full weight, no blending
    Aim_Blend,   // the live (pitch, yaw) blend, exactly as the game drives it
    Clip         // one `.animation` played on a clock, scrubbable
  };

  // Resolves the mesh and its skeleton, samples the current pose, and fills
  // `skinning_matrices` / `model_space_matrices`. Returns false when the model
  // is missing or unskinned, which is a state the panel reports rather than a
  // condition worth dying over -- the tool is where you go to find out WHY the
  // model is wrong.
  bool update_pose();

  // Model-space bone positions: the head of bone i, i.e. the origin of its
  // model-space matrix. Bone `tail` is not stored in the skeleton, so a bone's
  // segment is drawn to each CHILD's head, and a leaf gets a short stub along
  // `assets::bone_direction`. 
  linalg::vec3f bone_head(uint32_t bone_index) const;

  // Model space -> world, which here is the model-yaw slider and nothing else.
  // Bones, volumes and the mesh must all turn together or the volumes stop
  // sitting on the limbs the moment you turn the model. The direction form is
  // for a box's axes, which rotate but do not translate.
  linalg::vec3f to_world(const linalg::vec3f &model_space_point) const;
  linalg::vec3f to_world_direction(const linalg::vec3f &model_space_vector) const;

  // Reads `<skeleton>.hitboxes` beside the skeleton and resolves it. Called on
  // enable and by Reload.
  void load_rig();

  // The derived seed column and the coverage report, from the CURRENT in-memory
  // rig. Both walk every vertex against every volume, so this runs on an edit
  // that changes the answer -- never per frame.
  void refresh_derivation();

  // The volume table and the audit readouts, i.e. all of phase B's UI.
  void draw_hitbox_panel();

  // The transport: clip picker, play/pause, loop, speed and the scrub bar.
  void draw_clip_panel();

  // Every `.animation` beside the skeleton, by path. Called on enable and by
  // Rescan, so a clip exported while the tool is open shows up in the list. It
  // does NOT re-read a clip already loaded -- see the note at the button.
  void scan_clips();

  // Loads `clip_paths[selected_clip]` and rewinds. A clip whose skeleton hash
  // disagrees is refused by the loader, so a failure here is already explained
  // in the console.
  void load_selected_clip();

  // Moves `clip_phase` by `dt`. Separate from update_pose because the pose is
  // sampled every frame and the clock only advances when playing -- folding them
  // together would make a paused scrub advance on its own.
  void advance_clip(float dt);

  Pose_Source        pose_source = Pose_Source::Aim_Blend;
  entities::Aim_Pose single_pose = entities::Aim_Pose::Forward;

  // The two blend inputs. Yaw is the DEVIATION between where the feet point and
  // where the player is looking -- not an absolute yaw -- because that is what
  // the left/right poses are authored against (animation_def.md §5).
  float pitch_degrees         = 0.0f;
  float yaw_deviation_degrees = 0.0f;

  // The model stands at the world origin, feet on the ground plane, so the
  // movement hull and the static hitbox table can be drawn around it in the
  // coordinates they are actually written in (offsets from the FEET).
  float model_yaw_degrees = 0.0f;

  // --- Clip playback ---
  //
  // `clip_phase` is 0..1 over the whole clip, the same parameter
  // sample_animation_clip_at takes -- NOT a frame index and not seconds. Keeping
  // the tool's state in the sampler's own parameter is what stops the scrub bar
  // and the clock disagreeing about where "the end" is when looping is toggled.
  static constexpr int NO_CLIP_SELECTED = -1;
  std::vector<std::string> clip_paths;
  int                      selected_clip = NO_CLIP_SELECTED;

  assets::asset_handle_t<assets::animation_clip_t> clip_handle;
  float clip_phase          = 0.0f;
  bool  clip_playing        = false;
  bool  clip_looping        = true;
  float clip_playback_speed = 1.0f;

  bool show_mesh            = true;
  bool show_hitboxes        = true;
  // Posed wireframe, through the skinned wireframe pipeline -- so the surface
  // follows the pose and you can see what a hitbox volume actually encloses
  // rather than guessing from a silhouette.
  bool wireframe            = false;
  bool show_skeleton        = true;
  bool show_bone_names      = false;
  bool show_movement_hull   = true;
  bool unlit                = false;

  // Highlighted in the overlay and named in the panel. -1 is none.
  static constexpr int NO_BONE_SELECTED = -1;
  static constexpr int NO_VOLUME_SELECTED = -1;
  int selected_bone   = NO_BONE_SELECTED;
  int selected_volume = NO_VOLUME_SELECTED;

  // Resized once, reused every frame.
  assets::pose_t             pose;
  std::vector<linalg::mat4f> local_matrices;
  std::vector<linalg::mat4f> model_space_matrices;
  std::vector<linalg::mat4f> skinning_matrices;

  // Resolved in update_pose, read by the overlay. A null skeleton means
  // "nothing to draw"; the console carries why.
  assets::asset_handle_t<assets::mesh_asset_t> mesh_handle;
  const assets::skeleton_t * skeleton = nullptr;

  // update_pose runs every frame and a missing asset stays missing, so the
  // reason is logged on the transition into failure and not again until it
  // resolves.
  bool model_failure_logged = false;

  // --- The hitbox rig ---
  //
  // `rig` is the authored file and is what Save writes back; `seeds` is the
  // derived column beside it.
  std::string                      rig_path;
  assets::hitbox_rig_t             rig;
  assets::resolved_hitbox_rig_t    resolved_rig;
  std::vector<assets::hitbox_seed_t> seeds;

  // Refilled every frame from the live pose
  std::vector<assets::posed_hitbox_t> hitboxes;

  // Audit readouts. Coverage is a bind-pose property and is computed at load;
  // excursion is per-pose and is recomputed with the capsules.
  assets::hitbox_coverage_t coverage;
  assets::hull_excursion_t  excursion;

  // Stops on_update re-reading a missing file every frame. Cleared by Reload,
  // which is the button that fixes the states this latches on.
  bool rig_load_attempted = false;
};

} // namespace client

#pragma once

#include "animation.hpp"
#include "asset.hpp"
#include "linalg.hpp"
#include "skeleton.hpp"

#include <vector>

// A set that exists is a set that loaded: load_aim_pose_set dies rather than
// returning a half-built one, so there is no `valid()` for callers to forget.
struct aim_pose_set_t
{
  Enum_Array<entities::Aim_Pose, assets::asset_handle_t<assets::animation_clip_t>> poses;
};

struct aim_settings_t
{
  // The extent of the authored pose space. Aiming past it clamps to the
  // extreme; it does not extrapolate.
  float max_pitch_degrees = 45.0f;
  float max_yaw_degrees   = 45.0f;
  // How fast the FEET chase the view yaw, in degrees per second. This is what
  // gives the yaw axis anything to do: with the body drawn at the view yaw the
  // torso can never be turned relative to it, and the left/right poses would be
  // unreachable by construction.
  float body_turn_rate_degrees_per_second = 540.0f;
};

// Loads `<directory>/{forward,upward,downward,left,right}_<suffix>.animation`.
// ANY of the five missing is fatal and names the file. The set is meaningless
// partial: sample_aim_pose would give the missing pose's weight back to Forward
// and draw a player who stares ahead while looking up, which reads as a rigging
// bug rather than as a missing file.
//@NOTE(SJM): this is super fragile and hacky since it depends on the directory structure and naming convention
// but it works for now.
aim_pose_set_t load_aim_pose_set(const char *directory, const char *suffix);

// The one `holding_gun` set, loaded on first use and held forever. Both the
// player draw path and the Animation tool want it, and two caches would be two
// chances to be looking at a different set than the game is drawing.
//
// The function-local static IS the whole cache: loading either succeeds or the
// process dies, so there is no "we already tried and it failed" state to carry.
const aim_pose_set_t &holding_gun_aim_poses();

// The blend on its own, stopping at the POSE. Split out of
// compute_aim_skinning_matrices because a caller that wants where the BONES are
// -- the Animation tool drawing a skeleton overlay or a hitbox capsule -- cannot
// get there from skinning matrices: those carry the inverse bind and are a
// delta, not a position. One sampler, two consumers.

// Advances `body_yaw` toward `view_yaw` and returns the DEVIATION between them
// in degrees -- the value the left/right poses are driven by.
// The feet lag the view, then catch up: within the pose set's yaw extent the
// torso simply twists, and past it the body turns to keep the deviation inside
// what the poses cover. Both angles are degrees and the difference is wrapped
// to (-180, 180], so crossing the 0/360 seam does not spin the model.
float advance_body_yaw(float &body_yaw, float view_yaw, float delta_time,
                       const aim_settings_t &settings);

void compute_aim_pose(const aim_pose_set_t &pose_set, const assets::skeleton_t &skeleton,
                      float pitch_degrees, float yaw_deviation_degrees,
                      const aim_settings_t &settings, assets::pose_t &out);

// pitch/yaw_deviation (degrees) -> one skinning matrix per bone, ready for
// `mesh_draw_parameters_t::skinning_matrices`.
//
// `out` is a vector rather than a Span because this is the function that KNOWS
// the bone count -- it resizes to the skeleton, where the two in skinning.hpp
// only fill what they are handed. Pass the same vector every frame and the
// resize is a no-op after the first.
//
// Every remaining failure here is a broken build, not a runtime condition: the
// pose set loaded or the process died, so a pose that cannot be sampled or a
// skeleton the poses were not authored against is fatal.
void compute_aim_skinning_matrices(const aim_pose_set_t &pose_set,
                                   const assets::skeleton_t &skeleton, float pitch_degrees,
                                   float yaw_deviation_degrees, const aim_settings_t &settings,
                                   std::vector<linalg::mat4f> &out);

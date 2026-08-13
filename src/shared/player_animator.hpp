#pragma once

// The player's aim pose blend, and the feet-chase-the-view integrator that
// feeds it.
//
// In `game_shared`, not in the client, because BOTH SIDES run it: the client
// draws the pose and the server poses the hit volumes out of the same blend
// (`player_rig.hpp`). One implementation is what makes the silhouette you shoot
// at the volume that gets tested -- animation_def.md §4, "the two guarantees".

#include "animation.hpp"
#include "asset.hpp"
#include "cvars/generated/cvars_generated.hpp"
#include "linalg.hpp"
#include "skeleton.hpp"
#include "skinning.hpp"

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

// The one translation from cvars to the values. Here rather than at each call
// site because there are now three of them -- the client's draw path, the
// server's hitbox pose and the Animation tool -- and three copies of a
// three-field struct literal is three chances to read a different cvar. The
// values are @Mirrored, so both sides read the same numbers.
inline aim_settings_t aim_settings_from(const cvars::cvar_state_t &cvars)
{
  return aim_settings_t{.max_pitch_degrees = cvars.sv_aim_max_pitch,
                        .max_yaw_degrees   = cvars.sv_aim_max_yaw,
                        .body_turn_rate_degrees_per_second = cvars.sv_aim_body_turn_rate};
}

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
// compute_aim_posed_skeleton for the callers that do not want the matrix walk
// at all -- the server's hit volumes sample here and walk the hierarchy
// themselves, since they pose the volumes rather than a mesh. One sampler,
// several consumers.

// Advances `body_yaw` toward `view_yaw` and returns the DEVIATION between them
// in degrees -- the value the left/right poses are driven by.
// The feet lag the view, then catch up: within the pose set's yaw extent the
// torso simply twists, and past it the body turns to keep the deviation inside
// what the poses cover. Both angles are degrees and the difference is wrapped
// to (-180, 180], so crossing the 0/360 seam does not spin the model.
//
// THE SERVER IS THE ONLY CALLER THAT ADVANCES. It runs this once per player per
// fixed tick off the replicated view yaw and writes the result to
// `Player_Entity::body_yaw`; clients read that field. A client calling this
// would be a second integrator on the render clock, which is exactly the
// three-way disagreement the field exists to end.
float advance_body_yaw(float &body_yaw, float view_yaw, float delta_time,
                       const aim_settings_t &settings);

void compute_aim_pose(const aim_pose_set_t &pose_set, const assets::skeleton_t &skeleton,
                      float pitch_degrees, float yaw_deviation_degrees,
                      const aim_settings_t &settings, assets::pose_t &out);

// pitch/yaw_deviation (degrees) -> the posed skeleton: `out.skinning` is ready
// for `mesh_draw_t::pose`, `out.model_space` for anything wanting bone
// positions.
//
// `out` is a posed_skeleton_t rather than a Span because this is the function
// that KNOWS the bone count -- compute_posed_skeleton resizes to the skeleton,
// where the two raw walks in skinning.hpp only fill what they are handed. Pass
// the same one every frame and the resize is a no-op after the first.
//
// Every remaining failure here is a broken build, not a runtime condition: the
// pose set loaded or the process died, so a pose that cannot be sampled or a
// skeleton the poses were not authored against is fatal.
void compute_aim_posed_skeleton(const aim_pose_set_t &pose_set, const assets::skeleton_t &skeleton,
                                float pitch_degrees, float yaw_deviation_degrees,
                                const aim_settings_t &settings, assets::posed_skeleton_t &out);

// The one death clip, loaded on first use and held forever -- the same shape and
// the same reasoning as holding_gun_aim_poses() above. A missing one is a broken
// build, not a runtime condition, so this dies naming the file rather than
// handing back a corpse frozen in the bind pose.
const assets::animation_clip_t &death_clip();

// A clip played on a CLOCK rather than sampled at a phase: `seconds` is time
// into the playback and the phase is derived from clip_duration_seconds, which
// is the one place that knows a loop spans one more interval than a one-shot.
// A one-shot past its end clamps on the last frame (sample_animation_clip_at
// does the clamping), which is what a corpse holding its final pose is.
//
// Not folded into compute_aim_posed_skeleton: that one blends five poses off
// two angles, this one plays one clip off a time, and the only thing they share
// is the tail below -- which is now a single compute_posed_skeleton call.
void compute_clip_posed_skeleton(const assets::animation_clip_t &clip,
                                 const assets::skeleton_t &skeleton, float seconds, bool looping,
                                 assets::posed_skeleton_t &out);

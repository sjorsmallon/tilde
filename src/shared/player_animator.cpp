#include "player_animator.hpp"

#include "assets/generated/asset_state_generated.hpp"
#include "log.hpp"
#include "skinning.hpp"

#include <cmath>
#include <optional>
#include <string>

namespace
{

// The exporter names its files lowercase (`forward_holding_gun.animation`) and
// the generated to_string gives the .def's spelling (`Forward`), so the prefix
// is DERIVED from the enum name rather than listed a second time. There used to
// be an AIM_POSE_PREFIXES table here carrying a comment asking it not to drift
// from the enum; a transformation cannot drift.
std::string filename_prefix_of(entities::Aim_Pose pose)
{
  std::string prefix = entities::to_string(pose);
  for (char &character : prefix)
    if (character >= 'A' && character <= 'Z')
      character += 'a' - 'A';
  return prefix;
}

} // namespace

using linalg::wrap_degrees;

aim_pose_set_t load_aim_pose_set(const char *suffix)
{
  aim_pose_set_t set;

  for (uint32_t index = 0; index < entities::Aim_Pose_COUNT; ++index)
  {
    const entities::Aim_Pose pose = (entities::Aim_Pose)index;
    const std::string        name = filename_prefix_of(pose) + "_" + suffix;

    // ANY missing pose is fatal, not just Forward. A set of five is meaningless
    // partial: falling back to Forward for a missing extreme ships a player who
    // stares straight ahead while looking up, which reads as a rig bug and gets
    // chased in the wrong place.
    const std::optional<assets::animation_asset> id =
        assets::try_from_string<assets::animation_asset>(name);
    if (!id)
      fatal_error("aim pose '{}' is not in the asset manifest, so the '{}' set is missing a "
                  "member; a partial set draws a player who stares ahead while looking up",
                  name, suffix);

    set.poses[pose] = assets::get_animation(*id);
  }

  return set;
}

float advance_body_yaw(float &body_yaw, float view_yaw, float delta_time,
                       const aim_settings_t &settings)
{
  float deviation = wrap_degrees(view_yaw - body_yaw);

  // Past the pose set's extent the FEET move, because there is no authored pose
  // for a torso turned further than this and clamping the blend would leave the
  // model aiming somewhere the player is not looking.
  const float limit = settings.max_yaw_degrees;
  if (deviation > limit)
  {
    body_yaw += deviation - limit;
    deviation = limit;
  }
  else if (deviation < -limit)
  {
    body_yaw += deviation + limit;
    deviation = -limit;
  }
  else if (settings.body_turn_rate_degrees_per_second > 0.0f)
  {
    // Inside the extent the feet still creep toward the view, so a player who
    // stops turning ends up facing where they are looking rather than staying
    // twisted forever.
    const float step = settings.body_turn_rate_degrees_per_second * delta_time;
    if (std::fabs(deviation) <= step)
    {
      body_yaw += deviation;
      deviation = 0.0f;
    }
    else
    {
      const float moved = deviation > 0.0f ? step : -step;
      body_yaw += moved;
      deviation -= moved;
    }
  }

  body_yaw = wrap_degrees(body_yaw);
  return deviation;
}

const aim_pose_set_t &holding_gun_aim_poses()
{
  static const aim_pose_set_t poses = load_aim_pose_set("holding_gun");
  return poses;
}

void compute_aim_pose(const aim_pose_set_t &pose_set, const assets::skeleton_t &skeleton,
                      float pitch_degrees, float yaw_deviation_degrees,
                      const aim_settings_t &settings, assets::pose_t &out)
{
  // Resolved HERE, per call, rather than cached: see the note on aim_pose_set_t.
  assets::aim_pose_clips_t clips;
  for (uint32_t index = 0; index < entities::Aim_Pose_COUNT; ++index)
  {
    const entities::Aim_Pose pose = (entities::Aim_Pose)index;
    clips[pose]                   = assets::get(pose_set.poses[pose]);
  }

  const assets::aim_poses_blend_weights_t blend = assets::compute_aim_blend(
      pitch_degrees, yaw_deviation_degrees, settings.max_pitch_degrees, settings.max_yaw_degrees);

  assets::sample_aim_pose(out, clips, blend);

  if (out.parent_space.size() != skeleton.bones.size())
    fatal_error("aim pose has {} bones but skeleton '{}' has {}; the pose set and the mesh are not "
                "on one skeleton",
                out.parent_space.size(), skeleton.name, skeleton.bones.size());
}

void compute_aim_posed_skeleton(const aim_pose_set_t &pose_set, const assets::skeleton_t &skeleton,
                                float pitch_degrees, float yaw_deviation_degrees,
                                const aim_settings_t &settings, assets::posed_skeleton_t &out)
{
  assets::pose_t pose;
  compute_aim_pose(pose_set, skeleton, pitch_degrees, yaw_deviation_degrees, settings, pose);
  assets::compute_posed_skeleton(skeleton, pose, out);
}

const assets::animation_asset_t &death_clip()
{
  // The handle is the cache -- Asset_Pool has no eviction, so a handle that
  // resolved once resolves forever -- but the POINTER is re-resolved per call
  // rather than stored, which is what keeps this correct if the pool ever
  // relocates its storage.
  static const assets::asset_handle_t<assets::animation_asset_t> handle =
      assets::get_animation(assets::animation_asset::Death);

  return *assets::get(handle);
}

void compute_clip_posed_skeleton(const assets::animation_asset_t &clip,
                                 const assets::skeleton_t &skeleton, float seconds, bool looping,
                                 assets::posed_skeleton_t &out)
{
  // A clip with nothing to play (one frame, one-shot) reports zero duration --
  // that is the cue to sample it rather than divide by it.
  const float duration = assets::clip_duration_seconds(clip, looping);
  const float phase    = duration > 0.0f ? seconds / duration : 0.0f;

  assets::pose_t pose;
  assets::sample_animation_clip_at(pose, clip, phase, looping);

  // Checked here rather than left to compute_posed_skeleton so the message can
  // name the CLIP: "the clip and the mesh are not on one skeleton" is the
  // authoring mistake, and a generic bone-count mismatch does not point at it.
  if (pose.parent_space.size() != skeleton.bones.size())
    fatal_error("clip '{}' has {} bones but skeleton '{}' has {}; the clip and the mesh are not on "
                "one skeleton",
                clip.name, pose.parent_space.size(), skeleton.name, skeleton.bones.size());

  assets::compute_posed_skeleton(skeleton, pose, out);
}

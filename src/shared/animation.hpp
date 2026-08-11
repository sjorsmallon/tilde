#pragma once

// .skeleton   ── model-space, inverted, rest only ──┐
//                                                   ├──▶ pose_t (parent-relative TRS)
// .animation  ── parent-relative TRS, per frame ────┘         │
//                                                   compute_model_space_matrices 
//                                                             │  (needs skeleton.parent_index)
//                                                      model space
//                                                             │  (* inverse_bind)
//                                                      skinning matrices
#include "array.hpp"
#include "entities/generated/entities_generated.hpp" // entities::Aim_Pose
#include "linalg.hpp"
#include "skeleton.hpp"
#include "span.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace assets
{

struct transform_t
{
  linalg::vec3f translation = {0, 0, 0};
  linalg::quatf rotation    = linalg::quatf::identity();
  linalg::vec3f scale       = {1, 1, 1};
};

//@NOTE(SJM): this "indexed by bone" does not sit well with me.
struct pose_t
{
  std::vector<transform_t> local; // indexed by bone
};

// A clip is frames x bones of local transforms, flat. `frames` is row-major by
// FRAME: frame f's bone b is at f * bone_count + b, which is the order the file
// is written in and the order sampling reads two adjacent frames in.
// A single-frame clip is not a special type.
struct animation_clip_t
{
  std::string              name;
  std::string              skeleton_name;
  uint64_t                 skeleton_hash = 0;
  float                    fps           = 30.0f;
  // Forward travel of the planted foot over one cycle, measured at export.
  // 0 means "not a locomotion clip" -- phase is then driven by time, not speed.
  float                    stride_distance = 0.0f;
  uint32_t                 bone_count      = 0;
  std::vector<transform_t> frames;

  uint32_t frame_count() const
  {
    return bone_count == 0 ? 0 : (uint32_t)(frames.size() / bone_count);
  }
};

using bone_mask_t = std::vector<float>;

// --- The primitives --------------------------------------------------------

// `out` is resized to the clip's bone count. `phase` is 0..1 over the whole
// clip; looping wraps and interpolates frame n-1 back into frame 0, non-looping
// clamps at both ends. A single-frame clip ignores phase entirely.
void sample_animation_clip_at(pose_t &out, const animation_clip_t &clip, float phase, bool looping);

// How long one pass through the clip lasts, in seconds -- i.e. what a caller
// advancing `phase` on a clock must divide by.
//
// It depends on `looping` for the same reason the sampler does, and the two
// numbers have to be derived from one place or playback drifts against the pose
// it draws: a loop spans `frames` intervals because frame n-1 interpolates back
// into frame 0, while a one-shot spans `frames - 1` because it stops ON the last
// frame. Returns 0 for a clip with nothing to play (one frame, one-shot), which
// is a caller's cue to skip the advance rather than divide by it.
float clip_duration_seconds(const animation_clip_t &clip, bool looping);

// destination = lerp(destination, source, per_bone_weight[bone] * layer_weight),
void blend_into(pose_t &destination, const pose_t &source, Span<const float> per_bone_weight,
                float layer_weight = 1.0f);

// TRS -> the local matrices compute_skinning_matrices consumes.
void get_local_transforms_of_bones_from_pose(const pose_t &pose, Span<linalg::mat4f> out_local);

void compute_bind_pose(const skeleton_t &skeleton, pose_t &out);


struct bone_weight_t
{
  std::string name;
  float       weight = 1.0f;
};
bool build_bone_mask(const skeleton_t &skeleton, const std::vector<bone_weight_t> &entries,
                     float default_weight, bone_mask_t &out);

struct aim_poses_blend_weights_t
{
  Enum_Array<entities::Aim_Pose, float> weights = {{1.0f, 0.0f, 0.0f, 0.0f, 0.0f}};
};

aim_poses_blend_weights_t compute_aim_blend(float pitch_degrees, float yaw_degrees, float max_pitch_degrees,
                              float max_yaw_degrees);
//
// Entries may be null; sample_aim_pose says what a missing one does.
using aim_pose_clips_t = Enum_Array<entities::Aim_Pose, const animation_clip_t *>;

void sample_aim_pose(pose_t &out, const aim_pose_clips_t &poses,
                     const aim_poses_blend_weights_t &blend);

} // namespace assets

#include "animation.hpp"

#include "log.hpp"
#include "skinning.hpp"

#include <cmath>

namespace assets
{

namespace
{

// Rotation matrix -> quaternion, Shepperd's method: pick the branch whose
// divisor is largest so the square root never runs into a near-zero. The naive
// single-branch form loses all precision at 180 degrees, which is exactly where
// a limb bent backwards lives.
linalg::quatf rotation_to_quaternion(const linalg::mat4f &m)
{
  // m is column-major, so m[column][row]; the transposed reads below are that,
  // not a transpose.
  const float m00 = m[0].x, m10 = m[0].y, m20 = m[0].z;
  const float m01 = m[1].x, m11 = m[1].y, m21 = m[1].z;
  const float m02 = m[2].x, m12 = m[2].y, m22 = m[2].z;

  const float trace = m00 + m11 + m22;
  linalg::quatf result;

  if (trace > 0.0f)
  {
    const float s = std::sqrt(trace + 1.0f) * 2.0f;
    result.w      = 0.25f * s;
    result.x      = (m21 - m12) / s;
    result.y      = (m02 - m20) / s;
    result.z      = (m10 - m01) / s;
  }
  else if (m00 > m11 && m00 > m22)
  {
    const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
    result.w      = (m21 - m12) / s;
    result.x      = 0.25f * s;
    result.y      = (m01 + m10) / s;
    result.z      = (m02 + m20) / s;
  }
  else if (m11 > m22)
  {
    const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
    result.w      = (m02 - m20) / s;
    result.x      = (m01 + m10) / s;
    result.y      = 0.25f * s;
    result.z      = (m12 + m21) / s;
  }
  else
  {
    const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
    result.w      = (m10 - m01) / s;
    result.x      = (m02 + m20) / s;
    result.y      = (m12 + m21) / s;
    result.z      = 0.25f * s;
  }

  return linalg::normalize(result);
}

transform_t decompose_affine_matrix_to_transform(const linalg::mat4f &m)
{
  transform_t out{};
  out.translation = {m[3].x, m[3].y, m[3].z};

  const linalg::vec3f column_x = {m[0].x, m[0].y, m[0].z};
  const linalg::vec3f column_y = {m[1].x, m[1].y, m[1].z};
  const linalg::vec3f column_z = {m[2].x, m[2].y, m[2].z};

  out.scale = {linalg::length(column_x), linalg::length(column_y), linalg::length(column_z)};

  // A mirrored bone (negative determinant) cannot be represented as a rotation
  // plus positive scale; fold the flip into scale.x so the composition still
  // reproduces the matrix.
  const float determinant = linalg::dot(column_x, linalg::cross(column_y, column_z));
  if (determinant < 0.0f)
    out.scale.x = -out.scale.x;

  linalg::mat4f rotation_only = linalg::mat4f::identity();
  rotation_only[0] = {column_x.x / out.scale.x, column_x.y / out.scale.x, column_x.z / out.scale.x,
                      0.0f};
  rotation_only[1] = {column_y.x / out.scale.y, column_y.y / out.scale.y, column_y.z / out.scale.y,
                      0.0f};
  rotation_only[2] = {column_z.x / out.scale.z, column_z.y / out.scale.z, column_z.z / out.scale.z,
                      0.0f};

  out.rotation = rotation_to_quaternion(rotation_only);
  return out;
}

float clamp01(float value) { return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value); }

transform_t lerp_transform(const transform_t &from, const transform_t &to, float t)
{
  transform_t out;
  out.translation = from.translation + (to.translation - from.translation) * t;
  out.rotation    = linalg::nlerp(from.rotation, to.rotation, t);
  out.scale       = from.scale + (to.scale - from.scale) * t;
  return out;
}

} // namespace

// --- The primitives --------------------------------------------------------

void sample_animation_clip_at(pose_t &out, const animation_asset_t &clip, const float phase, const bool looping)
{
  const uint32_t frame_count = clip.frame_count();
  out.parent_space.assign(clip.bone_count, transform_t{});

  if (frame_count == 0 || clip.bone_count == 0)
  {
    log_error("sample_animation_clip_at('{}'): the clip has {} frames over {} bones and cannot be sampled",
              clip.name, frame_count, clip.bone_count);
    return;
  }

  const transform_t* frames = clip.frames.data();

  // A single-frame clip -- every authored aim pose -- has nothing to interpolate
  // toward, and phase is meaningless rather than merely unused.
  if (frame_count == 1)
  {
    out.parent_space.assign(frames, frames + clip.bone_count);
    return;
  }

  float position;
  uint32_t first;
  uint32_t second;
  if (looping)
  {
    // Wrapping FIRST and then scaling keeps phase 1.0 and phase 0.0 the same
    // pose, which is what makes a loop seamless rather than a one-frame hitch.
    const float wrapped = phase - std::floor(phase);
    position            = wrapped * (float)frame_count;
    first               = (uint32_t)position % frame_count;
    second              = (first + 1) % frame_count;
  }
  else
  {
    position = clamp01(phase) * (float)(frame_count - 1);
    first    = (uint32_t)position;
    if (first >= frame_count - 1)
      first = frame_count - 2;
    second = first + 1;
  }

  // Relative to `first`, NOT to floor(position), and the two differ in exactly
  // one place: the non-looping branch above clamps `first` down to
  // frame_count-2 so that `second` stays in range, and at phase 1.0 position is
  // frame_count-1, one whole interval past it. floor() reported a blend of 0
  // there and handed back the SECOND-TO-LAST frame -- so the last frame of a
  // one-shot was unreachable, and a death animation ended one frame early
  // holding a pose nobody authored as the end.
  const float blend = position - (float)first;

  const transform_t* a = frames + (size_t)first  * clip.bone_count;
  const transform_t* b = frames + (size_t)second * clip.bone_count;

  for (uint32_t bone = 0; bone < clip.bone_count; ++bone)
    out.parent_space[bone] = lerp_transform(a[bone], b[bone], blend);
}

float clip_duration_seconds(const animation_asset_t &clip, const bool looping)
{
  const uint32_t frame_count = clip.frame_count();
  if (frame_count == 0 || !(clip.fps > 0.0f))
    return 0.0f;

  // The interval count, not the frame count -- see the header. Mirrors
  // sample_animation_clip_at's two branches exactly.
  const uint32_t intervals = looping ? frame_count : frame_count - 1;
  return (float)intervals / clip.fps;
}

void blend_into(pose_t &destination, const pose_t &source, Span<const float> per_bone_weight,
                float layer_weight)
{
  if (destination.parent_space.size() != source.parent_space.size())
  {
    log_error("blend_into: {} bones against {}; the two poses are not on one skeleton",
              destination.parent_space.size(), source.parent_space.size());
    return;
  }

  if (!per_bone_weight.empty() && per_bone_weight.count != destination.parent_space.size())
    fatal_error("blend_into: the mask is {} long but the poses have {} bones",
                per_bone_weight.count, destination.parent_space.size());

  for (size_t bone = 0; bone < destination.parent_space.size(); ++bone)
  {
    const float mask   = per_bone_weight.empty() ? 1.0f : per_bone_weight[(uint32_t)bone];
    const float weight = clamp01(mask * layer_weight);
    if (weight <= 0.0f)
      continue;
    destination.parent_space[bone] = weight >= 1.0f
                                  ? source.parent_space[bone]
                                  : lerp_transform(destination.parent_space[bone], source.parent_space[bone], weight);
  }
}

void compose_parent_space_matrices(const pose_t &pose, Span<linalg::mat4f> out_parent_space)
{
  if (out_parent_space.count != pose.parent_space.size())
    fatal_error("compose_parent_space_matrices: 'out_parent_space' is {} long but the pose has {} bones",
                out_parent_space.count, pose.parent_space.size());

  for (uint32_t bone = 0; bone < out_parent_space.count; ++bone)
    out_parent_space[bone] = linalg::compose_transform(pose.parent_space[bone].translation,
                                                       pose.parent_space[bone].rotation,
                                                       pose.parent_space[bone].scale);
}

void compute_bind_pose(const skeleton_t &skeleton, pose_t &out)
{
  std::vector<linalg::mat4f> parent_space(skeleton.bones.size());
  compute_parent_space_bind_matrices(skeleton, parent_space);

  out.parent_space.resize(skeleton.bones.size());
  for (size_t bone = 0; bone < skeleton.bones.size(); ++bone)
    out.parent_space[bone] = decompose_affine_matrix_to_transform(parent_space[bone]);
}

bool build_bone_mask(const skeleton_t &skeleton, const std::vector<bone_weight_t> &entries,
                     float default_weight, bone_mask_t &out)
{
  out.assign(skeleton.bones.size(), default_weight);

  bool complete = true;
  for (const bone_weight_t &entry : entries)
  {
    size_t index = skeleton.bones.size();
    for (size_t bone = 0; bone < skeleton.bones.size(); ++bone)
    {
      if (skeleton.bones[bone].name == entry.name)
      {
        index = bone;
        break;
      }
    }

    if (index == skeleton.bones.size())
    {
      // Loud, because a mask that silently covers nothing looks exactly like a
      // mask that is working -- the layer just has no visible effect.
      log_error("bone mask names '{}', which skeleton '{}' does not have", entry.name,
                skeleton.name);
      complete = false;
      continue;
    }
    out[index] = entry.weight;
  }

  if (!complete)
    out.clear();
  return complete;
}

// --- Aim -------------------------------------------------------------------

aim_poses_blend_weights_t compute_aim_blend(float pitch_degrees, float yaw_degrees, float max_pitch_degrees,
                              float max_yaw_degrees)
{
  // Not the default-constructed value, which is Forward alone: every weight is
  // assigned below and Forward's is computed from the other two.
  aim_poses_blend_weights_t blend;
  for (float &weight : blend.weights)
    weight = 0.0f;

  // A zero or negative limit would divide by zero and is a configuration error
  // rather than a runtime condition; treat it as "this axis does not move".
  const float vertical =
      max_pitch_degrees > 0.0f ? clamp01(std::fabs(pitch_degrees) / max_pitch_degrees) : 0.0f;
  const float horizontal =
      max_yaw_degrees > 0.0f ? clamp01(std::fabs(yaw_degrees) / max_yaw_degrees) : 0.0f;

  blend.weights[pitch_degrees >= 0.0f ? entities::Aim_Pose::Upward : entities::Aim_Pose::Downward] = vertical;
  blend.weights[yaw_degrees >= 0.0f ? entities::Aim_Pose::Right : entities::Aim_Pose::Left]         = horizontal;

  float total = vertical + horizontal;
  blend.weights[entities::Aim_Pose::Forward] = total < 1.0f ? 1.0f - total : 0.0f;

  // Past the plus's edge (both axes at their limit) the three weights sum to
  // more than 1; normalize rather than clamp, so a full diagonal splits the two
  // extremes evenly instead of dropping one of them.
  total = 0.0f;
  for (float weight : blend.weights)
    total += weight;
  if (total > 0.0f)
    for (float &weight : blend.weights)
      weight /= total;

  return blend;
}

void sample_aim_pose(pose_t &out, const aim_pose_clips_t &poses,
                     const aim_poses_blend_weights_t &blend)
{
  const animation_asset_t *forward = poses[entities::Aim_Pose::Forward];
  if (!forward)
    fatal_error("sample_aim_pose: the Forward pose is missing. Every other pose falls back to it, "
                "so there is no way to draw a player without one");

  // A missing extreme gives its weight back to Forward. The alternative --
  // sampling it anyway and getting a default-constructed pose -- would collapse
  // the model into a heap; this makes a half-exported set look STIFF, which is
  // legible as "the pose did not load" rather than as a rig bug.
  aim_poses_blend_weights_t effective = blend;
  for (uint32_t index = 0; index < entities::Aim_Pose_COUNT; ++index)
  {
    const entities::Aim_Pose pose = (entities::Aim_Pose)index;
    if (pose == entities::Aim_Pose::Forward || poses[pose])
      continue;
    effective.weights[entities::Aim_Pose::Forward] += effective.weights[pose];
    effective.weights[pose] = 0.0f;
  }

  // Start at Forward, then fold each extreme in at its share of the REMAINING
  // weight. Blending w1*p1 + w2*p2 + ... as a chain of two-way lerps is exact
  // for translation and scale, and for rotation it is nlerp's usual
  // order-dependence -- at three poses of one authored set, below the angular
  // resolution of the poses themselves.
  sample_animation_clip_at(out, *forward, 0.0f, /*looping*/ false);

  pose_t scratch;
  float  accumulated = effective.weights[entities::Aim_Pose::Forward];
  for (uint32_t index = 0; index < entities::Aim_Pose_COUNT; ++index)
  {
    const entities::Aim_Pose pose = (entities::Aim_Pose)index;
    if (pose == entities::Aim_Pose::Forward)
      continue;

    const float weight = effective.weights[pose];
    if (weight <= 0.0f)
      continue;

    accumulated += weight;
    if (accumulated <= 0.0f)
      continue;

    sample_animation_clip_at(scratch, *poses[pose], 0.0f, /*looping*/ false);
    blend_into(out, scratch, {}, weight / accumulated);
  }
}

} // namespace assets

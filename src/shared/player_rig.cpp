#include "player_rig.hpp"

#include "animation.hpp"
#include "assets/generated/asset_state_generated.hpp"
#include "log.hpp"
#include "skinning.hpp"

#include <cmath>

namespace shared
{
namespace
{

// The one rig the player is hit-tested against. An id rather than a path: the
// manifest already knows where `rig.hitboxes` is, and `decode_hitboxes` is the
// one place the parse, the skeleton resolve and the hash check happen -- this
// used to repeat all three, which is three chances to disagree with the loader
// every other caller goes through.
constexpr assets::hitbox_rig PLAYER_RIG = assets::hitbox_rig::rig;

player_rig_t load_player_rig()
{
  player_rig_t loaded;

  loaded.aim_poses = holding_gun_aim_poses();

  // Resolved, hash-checked and fatal on any of that failing before it gets
  // here, so there is nothing left to branch on. The skeleton comes with it:
  // a resolved rig's bone indices are indices into exactly one skeleton, and
  // loading a second copy by path was how they could stop being the same one.
  loaded.rig      = *assets::get(assets::get_hitbox_rig(PLAYER_RIG));
  loaded.skeleton = loaded.rig.skeleton;

  if (loaded.rig.volumes.empty() || !loaded.skeleton)
    fatal_error("player hit volumes '{}' resolved to nothing; without them nothing can be hit",
                assets::to_string(PLAYER_RIG));

  log_terminal("[hitbox] player rig '{}': {} volumes on skeleton '{}' ({} bones)",
               loaded.rig.name, loaded.rig.volumes.size(), loaded.skeleton->name,
               loaded.skeleton->bones.size());
  return loaded;
}

} // namespace

model_to_world_t model_to_world(float model_yaw_degrees, const linalg::vec3f &translation)
{
  const float angle = linalg::to_radians(model_yaw_degrees);
  return {std::cos(angle), std::sin(angle), translation};
}

void place_hitbox(assets::posed_hitbox_t &hitbox, const model_to_world_t &transform)
{
  hitbox.start         = transform.point(hitbox.start);
  hitbox.end           = transform.point(hitbox.end);
  hitbox.frame.right   = transform.direction(hitbox.frame.right);
  hitbox.frame.up      = transform.direction(hitbox.frame.up);
  hitbox.frame.forward = transform.direction(hitbox.frame.forward);
}

const player_rig_t &player_rig()
{
  static const player_rig_t rig = load_player_rig();
  return rig;
}

void compute_player_hitboxes(const player_rig_t &rig, const player_pose_t &pose,
                             const aim_settings_t &settings, Span<assets::posed_hitbox_t> out)
{
  if (out.size() != rig.volume_count())
    fatal_error("compute_player_hitboxes: {} volumes into {} outputs", rig.volume_count(),
                out.size());

  // The deviation the left/right poses are driven by. Derived rather than
  // passed: body_yaw and view_yaw are both replicated, so their difference is
  // not a third input that could disagree with them.
  const float yaw_deviation = linalg::wrap_degrees(pose.view_yaw - pose.body_yaw);

  // Sized to the skeleton every call; a thread_local pool would buy a few
  // allocations at the cost of making this the one function in here that is not
  // reentrant.
  assets::pose_t sampled;
  compute_aim_pose(rig.aim_poses, *rig.skeleton, pose.view_pitch, yaw_deviation, settings, sampled);

  // Model space only -- a hitbox wants where the bone IS. This deliberately
  // does NOT go through compute_posed_skeleton: that one also fills the skinning
  // matrices, which are model_space * inverse_bind and which nothing here reads.
  // They are not a by-product of the same walk, they are a second walk
  // (compute_model_space_matrices and compute_skinning_matrices are already
  // separate entry points), so taking them was ~35 matrix multiplies and a
  // vector per call, paid for every player every tick.
  //
  // Stack rather than caller storage, matching compute_posed_skeleton's own
  // scratch: sized by the bone budget, so nothing here allocates or is
  // non-reentrant.
  const uint32_t bone_count = (uint32_t)rig.skeleton->bones.size();
  linalg::mat4f  parent_space[assets::MAX_BONES];
  linalg::mat4f  model_space[assets::MAX_BONES];

  assets::compose_parent_space_matrices(sampled, Span<linalg::mat4f>{parent_space, bone_count});
  assets::compute_model_space_matrices(*rig.skeleton,
                                       Span<const linalg::mat4f>{parent_space, bone_count},
                                       Span<linalg::mat4f>{model_space, bone_count});

  assets::compute_posed_hitboxes(rig.rig, Span<const linalg::mat4f>{model_space, bone_count}, out);

  const model_to_world_t transform =
      model_to_world(linalg::model_yaw_from_view_yaw(pose.body_yaw), pose.feet_position);
  for (assets::posed_hitbox_t &hitbox : out)
    place_hitbox(hitbox, transform);
}

} // namespace shared

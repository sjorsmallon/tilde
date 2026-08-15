#include "player_rig.hpp"

#include "animation.hpp"
#include "log.hpp"
#include "model_format.hpp"
#include "skinning.hpp"

#include <cmath>
#include <string>

namespace shared
{
namespace
{

constexpr const char *MODELS_DIRECTORY = "resources/models";
constexpr const char *SKELETON_NAME    = "rig";
constexpr const char *AIM_POSE_SUFFIX  = "holding_gun";

player_rig_t load_player_rig()
{
  player_rig_t loaded;

  // The aim poses first, because loading a `.animation` loads and hash-checks
  // the skeleton it names as a side effect -- so by the time these are in, the
  // skeleton below is the one they were authored against or the process is
  // already dead.
  loaded.aim_poses = load_aim_pose_set(MODELS_DIRECTORY, AIM_POSE_SUFFIX);

  const std::string skeleton_path =
      std::string(MODELS_DIRECTORY) + "/" + SKELETON_NAME + ".skeleton";
  loaded.skeleton = assets::get(assets::load_skeleton(skeleton_path.c_str()));
  if (!loaded.skeleton)
    fatal_error("player skeleton '{}' did not load; there is no player to shoot at",
                skeleton_path);

  const std::string rig_path = std::string(MODELS_DIRECTORY) + "/" + SKELETON_NAME + ".hitboxes";
  std::optional<assets::hitbox_rig_file_t> parsed =
      models::try_parse_hitbox_rig_file(rig_path.c_str());
  if (!parsed)
    fatal_error("player hit volumes '{}' did not parse; without them nothing can be hit",
                rig_path);

  // The rig names the skeleton revision it was authored against. A mismatch is
  // not a rounding error: bone indices are the skeleton's, so volumes would sit
  // on whatever limb now occupies the index they were sized for.
  if (parsed->skeleton_hash != loaded.skeleton->hash)
    fatal_error("hit volumes '{}' were authored against skeleton hash {:016x}, but '{}' hashes to "
                "{:016x}; re-derive the rig",
                rig_path, parsed->skeleton_hash, skeleton_path, loaded.skeleton->hash);

  std::optional<assets::hitbox_rig_t> rig =
      assets::try_resolve_hitbox_rig(*parsed, *loaded.skeleton);
  if (!rig)
    fatal_error("hit volumes '{}' name bones that skeleton '{}' does not have", rig_path,
                loaded.skeleton->name);
  loaded.rig = std::move(*rig);

  log_terminal("[hitbox] player rig '{}': {} volumes on skeleton '{}' ({} bones)",
               loaded.rig.name, loaded.rig.volumes.size(), loaded.skeleton->name,
               loaded.skeleton->bones.size());
  return loaded;
}

// Model space to world: rotate about +Y by the body yaw, then translate to the
// feet.
//
// The rotation is the RENDERER's, not `direction_from_angles`': a model matrix
// with rotation.y sweeps +X toward -Z (renderer.cpp's T * Rz * Ry * Rx * S),
// while a view yaw sweeps +X toward +Z. `model_yaw_from_view_yaw` is that
// conversion and both sides call it -- a volume has to land on the limb you can
// see, so this angle and play_state's draw call must be the same angle or the
// overlay stops being evidence of anything.
struct model_to_world_t
{
  float         cosine = 1.0f;
  float         sine   = 0.0f;
  linalg::vec3f translation{};

  linalg::vec3f direction(const linalg::vec3f &v) const
  {
    return {cosine * v.x + sine * v.z, v.y, -sine * v.x + cosine * v.z};
  }
  linalg::vec3f point(const linalg::vec3f &v) const { return direction(v) + translation; }
};

model_to_world_t transform_for(const player_pose_t &pose)
{
  const float angle = linalg::to_radians(linalg::model_yaw_from_view_yaw(pose.body_yaw));
  return {std::cos(angle), std::sin(angle), pose.feet_position};
}

} // namespace

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

  // Only `model_space` is read below -- a hitbox wants where the bone IS. The
  // skinning matrices come along because they fall out of the same walk, and
  // separating them would mean a second entry point to keep in step with this
  // one.
  assets::posed_skeleton_t posed;
  assets::compute_posed_skeleton(*rig.skeleton, sampled, posed);

  assets::compute_posed_hitboxes(rig.rig, posed.model_space, out);

  const model_to_world_t transform = transform_for(pose);
  for (assets::posed_hitbox_t &hitbox : out)
  {
    hitbox.start = transform.point(hitbox.start);
    hitbox.end   = transform.point(hitbox.end);
    // A Box reads its half-extents in this frame, so the frame turns too or the
    // box stays pointing wherever the model was authored facing.
    hitbox.frame.right   = transform.direction(hitbox.frame.right);
    hitbox.frame.up      = transform.direction(hitbox.frame.up);
    hitbox.frame.forward = transform.direction(hitbox.frame.forward);
  }
}

} // namespace shared

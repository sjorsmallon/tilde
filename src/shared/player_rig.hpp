#pragma once

// Where a player's hit volumes ARE, in the world, for a given pose.
//
// This is the join between three things that already existed separately: the
// aim blend (`player_animator.hpp`), the bone -> volume mapping
// (`hitbox_rig.hpp`), and a player's gameplay state. The server calls it to
// decide hits; the client calls it to draw the debug overlay. One function, so
// the overlay cannot show you volumes the server is not testing.
//
// No mesh is loaded here, and that is deliberate -- the dedicated server has no
// meshes. A rig needs the SKELETON (bone hierarchy and inverse binds), the five
// aim clips and the authored `.hitboxes` file, all of which are small text.
// Radius derivation is the only part that needs vertices, and it is tool-time.

#include "entities/generated/entities_generated.hpp"
#include "hitbox_rig.hpp"
#include "linalg.hpp"
#include "player_animator.hpp"
#include "span.hpp"

namespace shared
{

// Everything needed to pose one player's volumes, loaded once.
struct player_rig_t
{
  const assets::skeleton_t* skeleton = nullptr;
  assets::hitbox_rig_t      rig;
  aim_pose_set_t            aim_poses;

  uint32_t volume_count() const { return (uint32_t)rig.volumes.size(); }
};

// The one player rig, loaded on first use and held forever. Loading it either
// succeeds or the process dies (a missing skeleton, an unresolvable bone name
// and a hash mismatch are all broken builds, not runtime conditions), so there
// is no failure for a caller to branch on and no half-built rig to guard
// against.
const player_rig_t &player_rig();

// Model space to world: rotate about +Y by a MODEL yaw, then translate.
//
// The rotation is the RENDERER's, not `direction_from_angles`': a model matrix
// with rotation.y sweeps +X toward -Z (renderer.cpp's T * Rz * Ry * Rx * S),
// while a view yaw sweeps +X toward +Z. A caller holding a VIEW yaw converts
// with `linalg::model_yaw_from_view_yaw` first -- a volume has to land on the
// limb you can see, so the server's hit test, play_state's draw call and the
// Animation tool's overlay must all be the same angle or the overlay stops
// being evidence of anything.
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

model_to_world_t model_to_world(float model_yaw_degrees, const linalg::vec3f &translation);

// Turns one volume out of model space: both endpoints, and -- for a Box -- the
// frame its half-extents are read in, which turns too or the box stays pointing
// wherever the model was authored facing.
void place_hitbox(assets::posed_hitbox_t &hitbox, const model_to_world_t &transform);

// What a player's volumes are placed from. Every field is either replicated or
// server-owned -- there is no client-local input here, which is the property
// that makes the client's overlay and the server's hit test the same volumes.
struct player_pose_t
{
  linalg::vec3f feet_position{}; // the entity position; the origin of the rig
  float         body_yaw   = 0.f; // degrees, where the FEET point
  float         view_yaw   = 0.f; // degrees; the twist is view_yaw - body_yaw
  float         view_pitch = 0.f; // degrees, positive looks up
};

// Poses the rig and drops it into world space. `out` must be
// `rig.volume_count()` long; a wrong length is fatal.
//
// Cost is one five-clip blend plus a hierarchy walk over ~35 bones. That is
// paid per target per shot rather than per frame, and it is the computation the
// dropped per-clip bake was meant to cache -- see the reversal block in
// animation_def.md §4 for why caching it lost.
void compute_player_hitboxes(const player_rig_t &rig, const player_pose_t &pose,
                             const aim_settings_t             &settings,
                             Span<assets::posed_hitbox_t> out);

} // namespace shared

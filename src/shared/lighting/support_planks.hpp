#pragma once

// The diving board under a player held by a solid shadow (projector_plan.md ss1d): one flat strip a body
// wide from the player's feet along the light's ray to where their shadow lands. It is drawn instead of the
// dark bodies of a from-the-lens pyramid and a player's own volume, which overlap every other pyramid
// between the lens and the casters and piled up into noise. Nothing here is collision: a plank is read off
// Movement::ground_mover_uid, which names the light's mover (or the casting player's), after the cut.

#include "array.hpp"
#include "entity_uid.hpp"
#include "linalg.hpp"
#include "shadow_volume.hpp"
#include "span.hpp"

#include <vector>

struct Bounding_Volume_Hierarchy;

namespace shared
{

// A player as the client draws them this frame: the own body at its predicted feet, a remote one where it
// is interpolated, each with the mover its Movement says it stands on (null when it stands on none).
struct supported_player_t
{
  entity_uid_t  player       = null_entity_uid;
  linalg::vec3f feet         = {0.f, 0.f, 0.f};
  entity_uid_t  ground_mover = null_entity_uid;
};

struct support_plank_t
{
  entity_uid_t player = null_entity_uid;
  entity_uid_t light  = null_entity_uid;
  // Feet end then landing end, wound so the strip faces up: feet left, feet right, landing right, landing left.
  Array<linalg::vec3f, 4> corners;
};

// APPENDS one plank per player whose ground mover is a solid shadow in `shadow_volumes` and whose shadow lands
// on the map within SHADOW_VOLUME_DRAWN_REACH; a player over a void gets none.
void compute_support_planks(Span<const supported_player_t> players, Span<const shadow_volume_t> shadow_volumes,
                            const Bounding_Volume_Hierarchy& bvh, std::vector<support_plank_t>& out);

} // namespace shared

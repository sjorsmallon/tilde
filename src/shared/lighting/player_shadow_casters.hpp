#pragma once

// A player as a caster into the solid_shadows lights (projector_plan.md ss1b): ONE piece per living player,
// its hull box, so other players stand on your shadow. The box rather than a bound of the posed hitboxes
// because the solid shadow's cost is its plane count squared (solid_shadows.hpp): a box throws a crate's
// shadow, a 26-DOP threw one thirteen times as costly. The piece is for collision alone; what is drawn is
// the light's shadow map. The volumes it throws are movers named by the PLAYER's uid (solid_shadows.hpp),
// and the player's own view ignores them (predicted_world_t::mover_uids_to_ignore) or it stands on its own
// shadow forever. A frozen player casts nothing here: its statue is a mover already.

#include "entity_uid.hpp"
#include "map/map_geometry.hpp"

#include <vector>

namespace shared
{

struct Entity_System;

struct player_shadow_caster_t
{
  entity_uid_t      uid = null_entity_uid;
  collision_piece_t piece;
};

// One caster per living, unfrozen player. Replaces `out`.
void compute_player_shadow_casters(const Entity_System& system, std::vector<player_shadow_caster_t>& out);

} // namespace shared

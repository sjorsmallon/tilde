#pragma once

#include "../shared/linalg.hpp"
#include "entities/generated/entities/player_entity_generated.hpp"

namespace server
{

// The one way a body is cut from where it stands to somewhere else: the place is written and the cut is
// counted, which is how a client tells a teleport from a move (Player_Entity::teleport_count).
inline void teleport_player(entities::Player_Entity& player, const vec3f& destination)
{
  player.position = destination;
  ++player.teleport_count;
}

} // namespace server

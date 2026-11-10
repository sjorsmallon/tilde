#pragma once

#include "../server_context.hpp"
#include "entities/generated/entities/player_entity_generated.hpp"

namespace server
{

// Begin_Trail's work: a Trail_Entity owned by `owner`, its first point at their feet. A live trail the owner
// already has is ended first, so one player grows one trail.
void begin_trail_for_player(server_context_t& context, const entities::Player_Entity& owner, uint32_t tick);

// End_Trail's work: every trail `owner_uid` has live loses its owner and takes `tick` as its end.
void end_trails_of_player(server_context_t& context, shared::entity_uid_t owner_uid, uint32_t tick);

// Grows every owned trail from its owner's feet by the flight path rule, ends one whose owner is gone or dead or
// whose points are full, then hurts every living player standing in a trail's wall (shared/collision/trail.hpp),
// the owner excepted on the mp_trail_lag_distance of trail behind their feet. The hits land through
// inflict_damage_batch, as a tick's hits do.
void update_trails(server_context_t& context);

} // namespace server

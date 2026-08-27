#pragma once

#include "../shared/entities/generated/entities_generated.hpp"
#include "../shared/entity_uid.hpp"
#include "server_context.hpp"

namespace server
{

bool destroy_entity(server_context_t &context, shared::entity_uid_t uid);

// Give this client a body: a Player_Entity at a spawn marker, its inventory,
// its team and its kinematic capsule. Returns the uid, or null_entity_uid if
// the slot is out of range or the pool refused the spawn.
//
// Unconditional -- it does not ask whether the mode allows joining right now.
// try_admit_player is the one that asks.
shared::entity_uid_t spawn_player_entity_for_client_slot(server_context_t &context, int32_t slot);

// Record that this client wants to play, and give it a body if the mode allows
// one right now. A mode with join_in_progress = false leaves a mid-round
// joiner as a spectator; admit_waiting_players picks them up at the boundary.
//
// This is the ONE door into the match for a human: join_game, and a map change
// re-seating the players who had bodies before it, both come through here, so
// "may I play yet" is answered in one place rather than at each of them.
void try_admit_player(server_context_t &context, int32_t slot);

// Give a body to every client that asked for one and is still waiting. Called
// from enter_phase at the round boundary, ahead of respawn_all_players, which
// is what then places them -- so an admitted player is spawned by the same pass
// as everyone else.
void admit_waiting_players(server_context_t &context);

} // namespace server

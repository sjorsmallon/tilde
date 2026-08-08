#pragma once

#include "../shared/entities/generated/entities_generated.hpp"

namespace client
{

struct client_context_t;

// The one weapon -> gunshot sound table. Shared with Play_State's predicted
// local shot deliberately: two tables would let your own gun and everyone
// else's drift apart, which is the same class of bug last_fire_weapon exists
// to prevent. Returns nullptr and log_errors for an unmapped weapon.
const char *fire_sound_for(entities::Weapon weapon);

// Plays a gunshot for every player whose Player_Entity::last_fire_tick advanced
// since the last call. Call once per received snapshot, right after
// latest_player_entities is refreshed.
//
// Firing is replicated STATE, not a cosmetic effect (see entities.def): the
// stamp rides the delta against the acked baseline, so a dropped packet delays
// a gunshot by a tick instead of losing it. That is also why this is a watcher
// and not an effect handler — nothing is dispatched, we just notice a field
// moved.
//
// The local player is skipped: our own shot is predicted in Play_State so it
// plays without a round trip.
void update_weapon_fire_audio(client_context_t &context);

} // namespace client

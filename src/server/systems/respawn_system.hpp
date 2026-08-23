#pragma once

#include "../../shared/events/generated/events_generated.hpp"
#include "../server_context.hpp"

#include <cstdint>

namespace server
{


// Called from a death site (rocket_system today; future: trigger volumes,
// melee, void-out) immediately after firing PLAYER_DIED. Records the death
// tick for `player_uid` in the side table. No-op if the player is already
// recorded — we keep the original death tick rather than refreshing it,
// so repeated hits on a corpse don't extend the respawn timer.
void schedule_respawn(server_context_t &context,
                      shared::entity_uid_t player_uid,
                      uint32_t death_tick);

// Drains the side table: for any entry where
// `current_tick >= death_tick + respawn_delay_seconds * tickrate_hz`, resets
// the player (position/orientation from a spawn marker, health, velocity),
// reposes the kinematic capsule, and fires PLAYER_SPAWNED. Called once per
// server tick after damage systems.
//
// The delay arrives as a VALUE, not as a cvar read: map_respawn_delay_seconds
// is the caller's to look up, which keeps the cvar family out of the systems
// and lets a test drain a respawn without standing one up. The deadline is
// evaluated HERE rather than stored at death, so changing the cvar takes effect
// on the players already waiting.
void update_respawns(server_context_t &context,
                     uint32_t current_tick,
                     uint32_t tickrate_hz,
                     const float respawn_delay_seconds);




[[nodiscard]]
const entities::Player_Spawn_Entity* try_pick_human_spawn(shared::game_session_t &session, uint32_t rotation_index);


const entities::Player_Spawn_Entity& origin_fallback_spawn();

void place_player_at_spawn(shared::game_session_t &session, entities::Player_Entity &player,
                           const entities::Player_Spawn_Entity &marker);

void fire_player_spawned_event(server_context_t &context,
                               const entities::Player_Entity &player);

} // namespace server

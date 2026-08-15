#pragma once

#include "../../shared/events/generated/events_generated.hpp"
#include "../server_context.hpp"

#include <cstdint>

namespace server
{

// Default delay between PLAYER_DIED and the matching PLAYER_SPAWNED. Hard-
// coded for now; if mode-specific delays land, route through cvar or a
// per-mode config.
inline constexpr float respawn_delay_seconds = 3.0f;

// Called from a death site (rocket_system today; future: trigger volumes,
// melee, void-out) immediately after firing PLAYER_DIED. Records the death
// tick for `player_uid` in the side table. No-op if the player is already
// recorded — we keep the original death tick rather than refreshing it,
// so repeated hits on a corpse don't extend the respawn timer.
void schedule_respawn(server_context_t &context,
                      shared::entity_uid_t player_uid,
                      uint32_t death_tick);

// Drains the side table: for any entry where
// `current_tick >= death_tick + respawn_delay_ticks`, resets the player
// (position/orientation from a spawn marker, health, velocity), reposes
// the kinematic capsule, and fires PLAYER_SPAWNED. Called once per server
// tick after damage systems.
void update_respawns(server_context_t &context,
                     uint32_t current_tick,
                     uint32_t tickrate_hz);

// Builds a PLAYER_SPAWNED event from a player entity's current state and
// fires it. Shared by the respawn drain, the connect-time human spawn
// path in server_impl.cpp, and the bot spawn path in bot_system.cpp so
// every (re)spawn produces the same event shape.
void fire_player_spawned_event(server_context_t &context,
                               shared::entity_uid_t player_uid,
                               vec3f spawn_position,
                               vec3f spawn_orientation);

} // namespace server

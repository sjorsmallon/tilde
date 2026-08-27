#pragma once

#include "../../shared/events/generated/events_generated.hpp"
#include "../game_mode.hpp"
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




// Put EVERY player back on a spawn marker, alive, right now — the round-start
// reset. Called from enter_phase, which is the one writer of the phase, so a
// round boundary and the snap that goes with it cannot come apart.
//
// Ignores the respawn delay entirely: this is not a respawn, it is the round
// starting, and a player who died two seconds before the bell must not spend the
// freeze as a corpse. It also DROPS every pending respawn, because a timer that
// survived would fire mid-round and teleport a live player.
//
// Markers are cycled across the players so a full server does not stack
// everyone on marker 0.
void respawn_all_players(server_context_t &context);

// A Spawn_Type::Human marker for a player of `team`, or null when the map
// declares none at all.
//
// The policy and the team arrive as VALUES rather than as the mode row or the
// player: this is the one function that knows how a marker is chosen, and every
// caller has a different reason for the rotation index it passes (slot, bot
// count, position in the round-start sweep). Team_Markers with no marker for
// that team logs and falls back to the rotation, because spawning a player
// inside the other team is worse than spawning them somewhere neutral.
[[nodiscard]]
const entities::Player_Spawn_Entity* try_pick_human_spawn(shared::game_session_t &session,
                                                          Spawn_Policy policy,
                                                          entities::Team_Allegiance team,
                                                          uint32_t rotation_index);


const entities::Player_Spawn_Entity& origin_fallback_spawn();

void place_player_at_spawn(shared::game_session_t &session, entities::Player_Entity &player,
                           const entities::Player_Spawn_Entity &marker);

void fire_player_spawned_event(server_context_t &context,
                               const entities::Player_Entity &player);

} // namespace server

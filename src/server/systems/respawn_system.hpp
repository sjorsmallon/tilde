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

// The one spawn picker. Every (re)spawn path routes through it, which is what
// makes team-based or round-cycled selection a change to this one function
// rather than to each caller.
//
// `rotation_index` is all that varies the choice today: the respawn drain
// passes 0 (always the first marker), the join path the client slot, spawn_bot
// the bot count. It is taken modulo the number of human spawns.
//
// Returns the marker itself rather than a copy of two of its fields, so a new
// field on Player_Spawn_Entity (team, spawn group) needs no change here.
// nullptr — and no log — when the map declares no Spawn_Type::Human marker:
// that is a broken map, and each caller says out loud what it does instead.
[[nodiscard]] const entities::Player_Spawn_Entity*
try_pick_human_spawn(shared::game_session_t &session, uint32_t rotation_index);

// The marker to use when a map declares no spawn point. Callers log FIRST and
// then pass this: try_pick_human_spawn returns nullptr without a log precisely
// so each caller says out loud what it does instead, and that stays true here.
// Default-constructed is what we want -- position and orientation both zero.
const entities::Player_Spawn_Entity& origin_fallback_spawn();

// The one "put this player into the world at a spawn point" reset. Every
// (re)spawn path calls it -- connect, the respawn drain, both bot paths -- so
// there is exactly one list of what a fresh player looks like.
//
// Takes the MARKER rather than a copy of two of its fields, for the same
// reason try_pick_human_spawn returns one: a new field on Player_Spawn_Entity
// (team_allegiance is already there and unread) becomes a line in here rather
// than a signature change plus four call sites. Reducing it to a position was
// how the map-load bot path silently dropped orientation.
//
// Pure over the entity: the caller picks the marker and owns the physics body,
// which is the only thing that genuinely differs between a first spawn
// (register a capsule) and a respawn (repose the one already there).
void place_player_at_spawn(entities::Player_Entity &player,
                           const entities::Player_Spawn_Entity &marker);

// Builds a PLAYER_SPAWNED event from a player entity's current state and fires
// it. Shared by the respawn drain and the connect-time spawn path in
// server_impl.cpp, so every (re)spawn produces the same event shape. Reads the
// uid off Player_Entity::entity_id, which IS the uid.
void fire_player_spawned_event(server_context_t &context,
                               const entities::Player_Entity &player);

} // namespace server

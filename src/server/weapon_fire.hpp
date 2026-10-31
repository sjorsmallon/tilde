#pragma once

#include "../shared/entity_uid.hpp"
#include "../shared/map/game_session.hpp"
#include "../shared/collision/predicted_world.hpp"
#include "../shared/subtick.hpp"
#include "../shared/weapon_instance.hpp"
#include "../shared/weapons.hpp"
#include "entities/generated/entities/player_entity_generated.hpp"
#include "server_context.hpp"
#include "spawn_projectile.hpp"

#include <cstdint>
#include <optional>

namespace server
{

void mark_shot_fired(const server_context_t& context, entities::Player_Entity& player,
                     entities::Weapon weapon_id);

// The server's half of a trigger the input step reported due (input_step_outcome_t::fire_due):
// the clocks, the ammo, an impulse and a launch are already applied to the player. Either
// button, through the one switch over its Fire_Resolution. `world` is the shooter's team
// view: a hitscan asks it the one question every flying thing asks, at radius zero, and pushes
// what it arrived at as a pending_contact_t for update_contacts.
void resolve_player_shot(server_context_t& context, int32_t client_slot,
                         const game::C2S_ClientInput& input, const shared::predicted_world_t& world,
                         entities::Player_Entity& player, float yaw, float pitch,
                         shared::subtick_time_t fire_time, entities::Fire_Trigger trigger);

} // namespace server

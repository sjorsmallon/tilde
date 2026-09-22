#pragma once

#include "../shared/entities/generated/entities_generated.hpp"
#include "../shared/entity_uid.hpp"
#include "../shared/game_session.hpp"
#include "../shared/span.hpp"
#include "../shared/subtick.hpp"
#include "../shared/weapons.hpp"
#include "server_context.hpp"
#include "spawn_projectile.hpp"

#include <cstdint>
#include <optional>

namespace server
{

void mark_shot_fired(const server_context_t& context, entities::Player_Entity& player,
                     entities::Weapon weapon_id);

// shared::try_find_held_fire_time for the weapon in the player's hand; empty for an empty hand.
[[nodiscard]] std::optional<shared::subtick_time_t>
try_find_held_fire_time(shared::game_session_t& session, const entities::Player_Entity& player,
                        entities::Fire_Trigger trigger, shared::subtick_time_t step_start,
                        shared::subtick_time_t step_end);

// Either button, through the one switch over its Fire_Resolution.
void resolve_player_shot(server_context_t& context, int32_t client_slot,
                         const game::C2S_ClientInput& input, Span<const uint8_t> disabled_geometry,
                         entities::Player_Entity* player, float yaw, float pitch,
                         shared::subtick_time_t fire_time, entities::Fire_Trigger trigger);

} // namespace server

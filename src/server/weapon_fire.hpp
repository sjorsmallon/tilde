#pragma once

#include "../shared/entities/generated/entities_generated.hpp"
#include "../shared/entity_uid.hpp"
#include "../shared/game_session.hpp"
#include "../shared/span.hpp"
#include "../shared/subtick.hpp"
#include "../shared/weapons.hpp"
#include "server_context.hpp"

#include <cstdint>

namespace server
{

bool is_reloading(const entities::Player_Entity& player);
void finish_reload(shared::game_session_t& session, entities::Player_Entity& player);
void cancel_reload(entities::Player_Entity& player);

void mark_shot_fired(const server_context_t& context, entities::Player_Entity& player,
                     entities::Weapon weapon_id);

// The one place a Fire_Resolution::Projectile row becomes an entity; bots call it too.
// The trigger is here rather than at the call sites because a projectile that
// flies differently per button is a property of the spawn, and this is the only
// site that has both the row and the entity.
shared::entity_uid_t spawn_projectile(
    server_context_t& context, shared::entity_uid_t owner_uid,
    const shared::weapon_definition_t& weapon, const vec3f& origin, const vec3f& direction,
    shared::fire_trigger_t trigger = shared::fire_trigger_t::Primary);

void resolve_player_shot(server_context_t& context, int32_t client_slot,
                         const game::C2S_ClientInput& input, Span<const uint8_t> disabled_geometry,
                         entities::Player_Entity* player, float yaw, float pitch,
                         shared::subtick_time_t fire_time);

} // namespace server

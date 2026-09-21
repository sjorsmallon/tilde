#pragma once

#include "../shared/entities/generated/entities_generated.hpp"
#include "../shared/entity_uid.hpp"
#include "../shared/weapons.hpp"
#include "server_context.hpp"

namespace server
{

// The one place a Fire_Resolution::Projectile fire becomes an entity; bots and launchers call it too.
// The trigger picks which half of the row it reads, and is stamped on the
// Projectile so the flight reads the same half.
shared::entity_uid_t spawn_projectile(
    server_context_t& context, shared::entity_uid_t owner_uid,
    const shared::weapon_definition_t& weapon, const vec3f& origin, const vec3f& direction,
    entities::Fire_Trigger trigger = entities::Fire_Trigger::Primary);

} // namespace server

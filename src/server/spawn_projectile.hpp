#pragma once

#include "../shared/entities/generated/entities_generated.hpp"
#include "../shared/entity_uid.hpp"
#include "../shared/weapons.hpp"
#include "server_context.hpp"

namespace server
{

// The one place a Fire_Resolution::Projectile fire becomes an entity; bots and launchers call it too.
// The trigger picks which half of the row it reads, and is stamped on the
// Projectile so the flight AND the contact read the same half. Names no type:
// what a kind does on arrival is its row's contact, not a field written here.
shared::entity_uid_t spawn_projectile(
    server_context_t& context, shared::entity_uid_t owner_uid,
    const shared::weapon_definition_t& weapon, const vec3f& origin, const vec3f& direction,
    entities::Fire_Trigger trigger = entities::Fire_Trigger::Primary);

// The one place a Fire_Resolution::Place fire becomes an entity: set down at
// `feet`, facing `yaw_degrees`, with no flight. Names no type either; a type's
// own placement rule (a remnant is one per owner) is the type's, applied by
// the caller on the uid this returns.
shared::entity_uid_t spawn_placed_entity(
    server_context_t& context, const shared::weapon_definition_t& weapon, const vec3f& feet,
    float yaw_degrees, entities::Fire_Trigger trigger = entities::Fire_Trigger::Primary);

} // namespace server

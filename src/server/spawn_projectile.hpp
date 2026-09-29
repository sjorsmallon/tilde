#pragma once

#include "../shared/entity_uid.hpp"
#include "../shared/weapons.hpp"
#include "entities/generated/entities_core_generated.hpp"
#include "server_context.hpp"

namespace server
{

inline constexpr shared::alive_limit_t NO_ALIVE_LIMIT = {};

// The one place a Fire_Resolution::Projectile fire becomes an entity; bots and launchers call it too.
// The trigger picks which half of the row it reads, and is stamped on the
// Projectile so the flight AND the contact read the same half. Names no type:
// what a kind does on arrival is its row's contact, not a field written here.
// The limit is the firing Weapon_Entity's (shared::alive_limit_of); a shooter holding no weapon has none.
shared::entity_uid_t spawn_projectile(
    server_context_t& context, shared::entity_uid_t owner_uid,
    const shared::weapon_definition_t& weapon, const vec3f& origin, const vec3f& direction,
    entities::Fire_Trigger trigger, const shared::alive_limit_t& limit);

// The one place a Fire_Resolution::Place fire becomes an entity: set down at
// `position` facing `orientation`, with no flight; the caller resolves the
// row's anchor to both. A type that carries a Projectile is stamped with its
// owner and its row, and is counted under the limit it is handed. Names no
// type either; a type's own placement rule (a remnant is one per owner) is
// the type's, applied by the caller on the uid this returns.
shared::entity_uid_t spawn_placed_entity(
    server_context_t& context, shared::entity_uid_t owner_uid,
    const shared::weapon_definition_t& weapon, const vec3f& position, const quatf& orientation,
    entities::Fire_Trigger trigger, const shared::alive_limit_t& limit);

} // namespace server

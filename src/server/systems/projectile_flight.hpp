#pragma once

#include "../../shared/entities/generated/entities_generated.hpp"
#include "../../shared/predicted_world.hpp"
#include "../../shared/projectile_sweep.hpp"
#include "../../shared/span.hpp"
#include "../server_context.hpp"

#include <optional>

namespace server
{

// One tick of flight for anything carrying a Projectile: the row's closed form, then a sphere sweep
// through the owner's team view of the world and the tick's targets, ignoring the owner. Writes the
// position either way and answers what it struck, if anything: a target's uid, a mover's, or null
// for the map.
[[nodiscard]] std::optional<shared::projectile_hit_t>
fly_projectile(server_context_t& context, const shared::predicted_world_storage_t& world,
               Span<const shared::projectile_target_t> targets, entities::Entity& entity,
               entities::Projectile& projectile, float collision_radius, float dt);

// What a flight's hit becomes for update_contacts: the sweep's centre moved a radius along the
// normal onto the surface or body struck, the row and button off the Projectile.
[[nodiscard]] pending_contact_t contact_of_projectile_hit(const entities::Projectile& projectile,
                                                          float collision_radius,
                                                          const shared::projectile_hit_t& hit);

// A flight that ran out with no hit: a contact at the projectile with no normal and no target.
[[nodiscard]] pending_contact_t contact_of_projectile_expiry(const entities::Entity& entity,
                                                             const entities::Projectile& projectile);

} // namespace server

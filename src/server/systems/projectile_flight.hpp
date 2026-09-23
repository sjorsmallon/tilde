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

} // namespace server

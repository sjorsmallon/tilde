#pragma once

#include "../../shared/entities/generated/entities_generated.hpp"
#include "../../shared/physics.hpp"
#include "../server_context.hpp"

#include <optional>

namespace server
{

// One tick of flight for anything carrying a Projectile: the row's closed form, then a sphere sweep
// that ignores the owner. Writes the position either way and answers what it struck, if anything.
[[nodiscard]] std::optional<hit_result_t> fly_projectile(server_context_t&     context,
                                                         entities::Entity&     entity,
                                                         entities::Projectile& projectile,
                                                         float collision_radius, float dt);

} // namespace server

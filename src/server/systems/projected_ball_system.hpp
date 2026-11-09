#pragma once

#include "../../shared/collision/predicted_world.hpp"
#include "../../shared/lighting/lighting.hpp"
#include "entities/generated/entities/projector_entity_generated.hpp"

#include <optional>
#include "../server_context.hpp"

namespace server
{

// The projector as a scene light this tick, posed through its Rides when it has one; empty when the entity does
// not convert (switched off).
[[nodiscard]] std::optional<shared::scene_light_t>
try_compute_projector_scene_light(server_context_t& context, const entities::Projector_Entity& projector);

// Tick 5: every projected ball, one projected_ball_step through its projector's mask rectangles, the footprint
// of the things in its light (this tick's shadow volumes) and the silhouettes of this tick's posed targets; a
// ball whose centre has reached the frame's edge, or whose projector is not a projector, is put back at its
// spawn.
void update_projected_balls(server_context_t& context, const shared::predicted_world_storage_t& world, float dt);

} // namespace server

#pragma once

#include "../../shared/fixed_arc_flight.hpp"
#include "../../shared/predicted_world.hpp"
#include "../server_context.hpp"

#include <cstdint>

namespace server
{

struct flight_launch_settings_t
{
  float flight_seconds = 0.f;
  // How far short of a wall the flight stops.
  float clearance      = 0.f;
};

// Decides the flight ONCE, against the static map: the last tick whose step reaches no wall.
// A wall the OWNER's team walks through is no wall to its projectile either; an
// owner that is no player (or is gone) flies through no team wall.
void launch_fixed_arc_flight(server_context_t& context, const entities::Projectile& projectile,
                             const linalg::vec3f& spawn_position, const flight_launch_settings_t& launch,
                             const shared::fixed_arc_flight_settings_t& flight_settings,
                             const shared::predicted_world_storage_t& world, entities::Fixed_Arc_Flight& out_flight);

} // namespace server

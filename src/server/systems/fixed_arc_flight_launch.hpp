#pragma once

#include "../../shared/fixed_arc_flight.hpp"
#include "../../shared/span.hpp"
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
void launch_fixed_arc_flight(const server_context_t& context, const entities::Projectile& projectile,
                             const linalg::vec3f& spawn_position, const flight_launch_settings_t& launch,
                             const shared::fixed_arc_flight_settings_t& flight_settings,
                             Span<const uint8_t> disabled_geometry, entities::Fixed_Arc_Flight& out_flight);

} // namespace server

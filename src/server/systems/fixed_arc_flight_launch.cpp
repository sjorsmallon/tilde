#include "fixed_arc_flight_launch.hpp"

#include "../../shared/collision_detection.hpp"
#include "../../shared/linalg.hpp"

#include <cmath>

namespace server
{

void launch_fixed_arc_flight(const server_context_t& context, const entities::Projectile& projectile,
                             const linalg::vec3f& spawn_position, const flight_launch_settings_t& launch,
                             const shared::fixed_arc_flight_settings_t& flight_settings,
                             Span<const uint8_t> disabled_geometry, entities::Fixed_Arc_Flight& out_flight)
{
  const uint32_t wanted_flight_ticks = static_cast<uint32_t>(
      std::lround(launch.flight_seconds / flight_settings.tick_interval_seconds));

  out_flight.launch_position = spawn_position;
  out_flight.launch_tick     = context.tick_number;
  out_flight.flight_ticks    = wanted_flight_ticks;

  vec3f previous = shared::flight_position_after(projectile, out_flight, 0, flight_settings);
  for (uint32_t ticks = 1; ticks <= wanted_flight_ticks; ++ticks)
  {
    const vec3f next   = shared::flight_position_after(projectile, out_flight, ticks, flight_settings);
    const vec3f travel = next - previous;
    const float length = linalg::length(travel);
    if (length > 1e-5f)
    {
      ray_hit_result_t hit;
      const bool       blocked =
          bvh_intersect_ray(context.world.session.bvh, previous, travel * (1.f / length), hit,
                            disabled_geometry) &&
          hit.hit && hit.t <= length + launch.clearance;
      if (blocked)
      {
        out_flight.flight_ticks = ticks - 1;
        return;
      }
    }
    previous = next;
  }
}

} // namespace server

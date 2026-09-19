#include "bubble_system.hpp"

#include "../../shared/bubble_flight.hpp"
#include "../../shared/collision_detection.hpp"
#include "../../shared/linalg.hpp"
#include "../entity_lifecycle.hpp"
#include "../server_api.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace server
{

// The flight is decided ONCE, against the static map: the last tick whose step reaches no wall.
static uint32_t flight_ticks_until_a_wall(const server_context_t&            context,
                                          const entities::Bubble_Entity&     bubble,
                                          uint32_t                           wanted_flight_ticks,
                                          const shared::bubble_flight_settings_t& flight,
                                          Span<const uint8_t>                disabled_geometry)
{
  entities::Bubble_Entity unbounded = bubble;
  unbounded.flight_ticks            = wanted_flight_ticks;

  vec3f previous = shared::bubble_position_after(unbounded, 0, flight);
  for (uint32_t ticks = 1; ticks <= wanted_flight_ticks; ++ticks)
  {
    const vec3f next   = shared::bubble_position_after(unbounded, ticks, flight);
    const vec3f travel = next - previous;
    const float length = linalg::length(travel);
    if (length > 1e-5f)
    {
      ray_hit_result_t hit;
      const bool       blocked =
          bvh_intersect_ray(context.world.session.bvh, previous, travel * (1.f / length), hit,
                            disabled_geometry) &&
          hit.hit && hit.t <= length + bubble.radius;
      if (blocked)
        return ticks - 1;
    }
    previous = next;
  }
  return wanted_flight_ticks;
}

void update_bubbles(server_context_t& context, Span<const uint8_t> disabled_geometry)
{
  const float tick_interval_seconds = static_cast<float>(get_tick_interval());
  const shared::bubble_flight_settings_t flight{.tick_interval_seconds = tick_interval_seconds,
                                                .gravity = context.cvars->g_gravity};

  std::vector<shared::entity_uid_t> expired;

  for (entities::Bubble_Entity& bubble :
       context.world.session.entity_system.entities_of<entities::Bubble_Entity>())
  {
    if (bubble.launch_tick == 0)
    {
      const uint32_t wanted_flight_ticks =
          static_cast<uint32_t>(std::lround(bubble.flight_seconds / tick_interval_seconds));

      bubble.launch_position = bubble.position;
      bubble.launch_tick     = context.tick_number;
      bubble.flight_ticks    = flight_ticks_until_a_wall(context, bubble, wanted_flight_ticks,
                                                         flight, disabled_geometry);
    }

    bubble.position = shared::bubble_position_at(bubble, context.tick_number, flight);

    const uint32_t rest_ticks =
        static_cast<uint32_t>(std::lround(bubble.rest_seconds / tick_interval_seconds));
    if (context.tick_number >= bubble.launch_tick + bubble.flight_ticks + rest_ticks)
      expired.push_back(bubble.entity_id);
  }

  for (const shared::entity_uid_t uid : expired)
    destroy_entity(context, uid);
}

void pop_bubble(server_context_t& context, shared::entity_uid_t volume_uid)
{
  if (context.world.session.entity_system.get<entities::Bubble_Entity>(volume_uid) == nullptr)
    return;

  destroy_entity(context, volume_uid);
}

} // namespace server

#include "entities/generated/entities/bubble_entity_generated.hpp"
#include "entities/generated/entities/jump_pad_entity_generated.hpp"
#include "movement_volumes.hpp"

#include "fixed_arc_flight.hpp"
#include "entity_system.hpp"
#include "shapes.hpp"

#include <cmath>

namespace shared
{

void collect_movement_volumes(Entity_System& system, const movement_volume_settings_t& settings,
                              std::vector<movement_volume_t>& out)
{
  out.clear();

  // Bubbles before pads, the order the volumes have always had. Which collect a @predicted type feeds is pinned by movement_volumes_test.
  const fixed_arc_flight_settings_t flight{
      .tick_interval_seconds = settings.tick_interval_seconds,
      .gravity               = settings.gravity};

  for (const entities::Bubble_Entity& bubble : system.entities_of<entities::Bubble_Entity>())
  {
    const uint32_t arm_ticks =
        settings.tick_interval_seconds > 0.f
            ? static_cast<uint32_t>(
                  std::lround(bubble.arm_seconds / settings.tick_interval_seconds))
            : 0u;
    const linalg::vec3f center = flight_position_at(bubble.projectile, bubble.flight,
                                                    bubble.position, settings.tick, flight);
    const linalg::vec3f reach  = {bubble.radius, bubble.radius, bubble.radius};
    out.push_back({
        .uid             = bubble.entity_id,
        .kind            = movement_volume_kind_t::Bounce,
        .bounds          = {.min = center - reach, .max = center + reach},
        .enabled         = bubble.flight.launch_tick != 0 && bubble.popped_tick == 0 &&
                           settings.tick >= bubble.flight.launch_tick + arm_ticks,
        .launch_velocity = {0.f, bubble.bounce_speed, 0.f},
    });
  }

  for (const entities::Jump_Pad_Entity& pad : system.entities_of<entities::Jump_Pad_Entity>())
  {
    out.push_back({
        .uid             = pad.entity_id,
        .kind            = movement_volume_kind_t::Jump_Pad,
        .bounds          = get_bounds(pad.volume, pad.position),
        .enabled         = pad.switch_state.value,
        .launch_velocity = linalg::forward(pad.orientation) * pad.launch_speed,
    });
  }
}

} // namespace shared

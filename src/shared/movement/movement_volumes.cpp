#include "entities/generated/entities/bubble_entity_generated.hpp"
#include "entities/generated/entities/jump_pad_entity_generated.hpp"
#include "movement_volumes.hpp"

#include "bubble.hpp"
#include "entity_system.hpp"
#include "shapes.hpp"

#include <cmath>

namespace shared
{

void compute_movement_volumes_at_tick(const Entity_System& system, const movement_volume_settings_t& settings,
                              std::vector<movement_volume_t>& out)
{
  out.clear();

  // Bubbles before pads, the order the volumes have always had. Which collect a @predicted type feeds is pinned by movement_volumes_test.
  const fixed_arc_flight_settings_t flight{
      .tickrate_hz = settings.tickrate_hz,
      .gravity     = settings.gravity};

  for (const entities::Bubble_Entity& bubble : system.entities_of_type<entities::Bubble_Entity>())
  {
    const uint32_t arm_ticks = ticks_from_seconds(bubble.arm_seconds, settings.tickrate_hz);
    const linalg::vec3f center = bubble_position_at_tick(bubble, settings.tick, flight);
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

  for (const entities::Jump_Pad_Entity& pad : system.entities_of_type<entities::Jump_Pad_Entity>())
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

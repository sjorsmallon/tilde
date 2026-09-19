#include "movement_volumes.hpp"

#include "bubble_flight.hpp"
#include "entities/generated/entities_generated.hpp"
#include "entity_system.hpp"
#include "shapes.hpp"

#include <cmath>

namespace shared
{

void collect_movement_volumes(Entity_System& system, const movement_volume_settings_t& settings,
                              std::vector<movement_volume_t>& out)
{
  out.clear();

  for (uint32_t index = 0; index < entities::ENTITY_TYPE_COUNT; ++index)
  {
    switch ((entities::entity_type)index)
    {
      case entities::entity_type::Jump_Pad_Entity:
      {
        for (const entities::Jump_Pad_Entity& pad :
             system.entities_of<entities::Jump_Pad_Entity>())
        {
          // The same expression launch_from_jump_pad used, moved rather than
          // copied, and evaluated once per tick instead of once per step.
          out.push_back({
              .uid             = pad.entity_id,
              .kind            = movement_volume_kind_t::Jump_Pad,
              .bounds          = get_bounds(pad.volume, pad.position),
              .enabled         = pad.switch_state.value,
              .launch_velocity = linalg::forward(pad.orientation) * pad.launch_speed,
          });
        }
        break;
      }

      // @predicted and deliberately silent HERE: a brush's switch is consulted
      // INSIDE the sweep, by every leaf test, so it feeds
      // collect_disabled_geometry instead. A volume is a box tested AFTER the
      // step, which for a wall means colliding with it and then reading a volume
      // saying "never mind".
      case entities::entity_type::Geometry_Owner_Entity:
        break;

      case entities::entity_type::Bubble_Entity:
      {
        const bubble_flight_settings_t flight{
            .tick_interval_seconds = settings.tick_interval_seconds,
            .gravity               = settings.gravity};

        for (const entities::Bubble_Entity& bubble :
             system.entities_of<entities::Bubble_Entity>())
        {
          const uint32_t arm_ticks =
              settings.tick_interval_seconds > 0.f
                  ? static_cast<uint32_t>(
                        std::lround(bubble.arm_seconds / settings.tick_interval_seconds))
                  : 0u;
          const linalg::vec3f center = bubble_position_at(bubble, settings.tick, flight);
          const linalg::vec3f reach  = {bubble.radius, bubble.radius, bubble.radius};
          out.push_back({
              .uid             = bubble.entity_id,
              .kind            = movement_volume_kind_t::Bounce,
              .bounds          = {.min = center - reach, .max = center + reach},
              .enabled         = bubble.launch_tick != 0 && bubble.popped_tick == 0 &&
                                 settings.tick >= bubble.launch_tick + arm_ticks,
              .launch_velocity = {0.f, bubble.bounce_speed, 0.f},
          });
        }
        break;
      }

      // @predicted, and feeds collect_movers: its geometry moves, it is not a box tested after the step.
      case entities::entity_type::Mover_Entity:
        break;

      // Every type that is not @predicted. Adding one makes this a compile
      // error, which is the point.
      case entities::entity_type::Invalid:
      case entities::entity_type::Player_Spawn_Entity:
      case entities::entity_type::Player_Spectate_Entity:
      case entities::entity_type::Player_Entity:
      case entities::entity_type::Weapon_Entity:
      case entities::entity_type::Rocket_Entity:
      case entities::entity_type::Hook_Entity:
      case entities::entity_type::Physics_Body_Entity:
      case entities::entity_type::Damageable_Entity:
      case entities::entity_type::Particle_Emitter_Entity:
      case entities::entity_type::Sound_Emitter_Entity:
      case entities::entity_type::Point_Light_Entity:
      case entities::entity_type::Spot_Light_Entity:
      case entities::entity_type::Directional_Light_Entity:
      case entities::entity_type::Trigger_Volume_Entity:
      case entities::entity_type::Reflection_Volume_Entity:
      case entities::entity_type::Game_Rules_Entity:
      case entities::entity_type::Logic_Counter_Entity:
      case entities::entity_type::Ping_Marker_Entity:
      case entities::entity_type::Logic_Timer_Entity:
      case entities::entity_type::Path_Node_Entity:
        break;
    }
  }
}

} // namespace shared

#include "movement_volumes.hpp"

#include "entities/generated/entities_generated.hpp"
#include "entity_system.hpp"
#include "shapes.hpp"

namespace shared
{

void collect_movement_volumes(Entity_System& system, std::vector<movement_volume_t>& out)
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

      // Every type that is not @predicted. Adding one makes this a compile
      // error, which is the point.
      case entities::entity_type::Invalid:
      case entities::entity_type::Player_Spawn_Entity:
      case entities::entity_type::Player_Spectate_Entity:
      case entities::entity_type::Player_Entity:
      case entities::entity_type::Weapon_Entity:
      case entities::entity_type::Rocket_Entity:
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
        break;
    }
  }
}

} // namespace shared

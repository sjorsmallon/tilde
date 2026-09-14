#include "disabled_geometry.hpp"

#include "entities/generated/entities_generated.hpp"
#include "entity_system.hpp"

namespace shared
{

void collect_disabled_geometry(Entity_System& system, Span<const entity_uid_t> owner_of,
                               disabled_geometry_t& out)
{
  out.assign(owner_of.size(), 0);

  for (uint32_t index = 0; index < owner_of.size(); ++index)
  {
    if (owner_of[index] == null_entity_uid)
      continue;

    const entities::Entity* entity = system.try_find(owner_of[index]);
    if (entity == nullptr)
      continue;

    // ONE exhaustive switch over entity_type, the shape collect_movement_volumes
    // already has and for its reason: reading a switch off an owner is per-type
    // logic, so the generator cannot write it and -Werror=switch is what polices
    // a type added with no arm.
    switch (entity->type)
    {
      case entities::entity_type::Brush_Entity:
      {
        const entities::Brush_Entity* owner =
            entities::entity_as<entities::Brush_Entity>(entity);
        out[index] = owner->switch_state.value ? 0 : 1;
        break;
      }

      // Every type that cannot own geometry. build_session already reported the
      // tie and left owner_of null, so reaching one of these means the entity at
      // that uid changed type, which nothing does.
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
      case entities::entity_type::Jump_Pad_Entity:
      case entities::entity_type::Reflection_Volume_Entity:
      case entities::entity_type::Game_Rules_Entity:
      case entities::entity_type::Logic_Counter_Entity:
      case entities::entity_type::Ping_Marker_Entity:
      case entities::entity_type::Logic_Timer_Entity:
        break;
    }
  }
}

} // namespace shared

#include "disabled_geometry.hpp"

#include "entities/generated/entities_generated.hpp"
#include "entity_system.hpp"

namespace shared
{

bool geometry_owner_blocks(const entities::Geometry_Owner_Entity& owner,
                           entities::Team_Allegiance          mover_team)
{
  if (!owner.switch_state.value)
    return false;
  return owner.passable_by == entities::Team_Allegiance::Free_For_All ||
         owner.passable_by != mover_team;
}

namespace
{

// ONE walk for both sets, the predicate being the only thing they disagree on.
template <typename Blocks_T>
void collect_geometry_where(Entity_System& system, Span<const entity_uid_t> owner_of,
                            disabled_geometry_t& out, Blocks_T&& owner_blocks)
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
      case entities::entity_type::Geometry_Owner_Entity:
      {
        const entities::Geometry_Owner_Entity* owner =
            entities::entity_as<entities::Geometry_Owner_Entity>(entity);
        out[index] = owner_blocks(*owner) ? 0 : 1;
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
      case entities::entity_type::Hook_Entity:
      case entities::entity_type::Kooh_Entity:
      case entities::entity_type::Ricochet_Entity:
      case entities::entity_type::Bubble_Entity:
      case entities::entity_type::Platform_Entity:
      case entities::entity_type::Canopy_Entity:
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
      case entities::entity_type::Path_Node_Entity:
      case entities::entity_type::Launcher_Entity:
      case entities::entity_type::Movement_Modifier_Entity:
      case entities::entity_type::Remnant_Entity:
      case entities::entity_type::Modifier_Shot_Entity:
      case entities::entity_type::Timed_Movement_Modifier_Entity:
        break;

      // A mover's switch freezes it rather than removing it, and its pieces are not in the tree anyway.
      case entities::entity_type::Mover_Entity:
        break;
    }
  }
}

} // namespace

void collect_disabled_geometry(Entity_System& system, Span<const entity_uid_t> owner_of,
                               entities::Team_Allegiance mover_team, disabled_geometry_t& out)
{
  collect_geometry_where(system, owner_of, out,
                         [mover_team](const entities::Geometry_Owner_Entity& owner)
                         { return geometry_owner_blocks(owner, mover_team); });
}

void collect_hidden_geometry(Entity_System& system, Span<const entity_uid_t> owner_of,
                             disabled_geometry_t& out)
{
  collect_geometry_where(system, owner_of, out,
                         [](const entities::Geometry_Owner_Entity& owner)
                         { return owner.switch_state.value; });
}

} // namespace shared

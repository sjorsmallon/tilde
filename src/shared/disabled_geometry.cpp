#include "disabled_geometry.hpp"

#include "collision_detection.hpp"
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

// ONE walk for both sets, the byte an owner's geometry gets being the only thing they disagree on.
template <typename State_Of_T>
void collect_geometry_where(Entity_System& system, Span<const entity_uid_t> owner_of,
                            disabled_geometry_t& out, State_Of_T&& state_of_owner)
{
  out.assign(owner_of.size(), 0);

  for (uint32_t index = 0; index < owner_of.size(); ++index)
  {
    if (owner_of[index] == null_entity_uid)
      continue;

    // Only a Geometry_Owner_Entity removes its pieces; a mover's switch freezes it and its pieces are not in the tree.
    const entities::Geometry_Owner_Entity* owner =
        entities::entity_as<entities::Geometry_Owner_Entity>(system.try_find(owner_of[index]));
    if (owner == nullptr)
      continue;

    out[index] = state_of_owner(*owner);
  }
}

} // namespace

void collect_disabled_geometry(Entity_System& system, Span<const entity_uid_t> owner_of,
                               entities::Team_Allegiance mover_team, disabled_geometry_t& out)
{
  collect_geometry_where(system, owner_of, out,
                         [mover_team](const entities::Geometry_Owner_Entity& owner) -> uint8_t
                         {
                           if (!geometry_owner_blocks(owner, mover_team))
                             return GEOMETRY_NOT_THERE;
                           if (owner.erased_by_light)
                             return GEOMETRY_SOLID_UNLESS_ERASED;
                           if (owner.erased_in_shadow)
                             return GEOMETRY_SOLID_UNLESS_SHADOWED;
                           if (owner.solid_only_in_shadow)
                             return GEOMETRY_SOLID_IN_SHADOW;
                           return owner.solid_only_when_revealed ? GEOMETRY_SOLID_WHERE_LIT
                                                                 : GEOMETRY_SOLID;
                         });
}

void collect_hidden_geometry(Entity_System& system, Span<const entity_uid_t> owner_of,
                             disabled_geometry_t& out)
{
  collect_geometry_where(system, owner_of, out,
                         [](const entities::Geometry_Owner_Entity& owner) -> uint8_t
                         { return owner.switch_state.value ? GEOMETRY_SOLID : GEOMETRY_NOT_THERE; });
}

} // namespace shared

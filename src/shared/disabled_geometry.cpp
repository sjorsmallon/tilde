#include "disabled_geometry.hpp"

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

    // Only a Geometry_Owner_Entity removes its pieces; a mover's switch freezes it and its pieces are not in the tree.
    const entities::Geometry_Owner_Entity* owner =
        entities::entity_as<entities::Geometry_Owner_Entity>(system.try_find(owner_of[index]));
    if (owner == nullptr)
      continue;

    out[index] = owner_blocks(*owner) ? 0 : 1;
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

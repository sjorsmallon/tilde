#include "ricochet_system.hpp"

#include "../../shared/projectile_sweep.hpp"
#include "../entity_lifecycle.hpp"
#include "projectile_flight.hpp"

#include <optional>
#include <vector>

namespace server
{

// Fly, push, destroy. One that touched nothing in its whole life left the map, and nobody is
// sent after it.
void update_ricochets(server_context_t& context, const shared::predicted_world_storage_t& world,
                      float dt)
{
  shared::Entity_System& entity_system = context.world.session.entity_system;

  std::vector<shared::entity_uid_t> spent;

  std::vector<shared::projectile_target_t> targets;
  shared::collect_projectile_targets(entity_system, targets);

  for (entities::Ricochet_Entity& ricochet :
       entity_system.entities_of<entities::Ricochet_Entity>())
  {
    ricochet.lifetime -= dt;
    if (ricochet.lifetime <= 0.f)
    {
      spent.push_back(ricochet.entity_id);
      continue;
    }

    const std::optional<shared::projectile_hit_t> hit = fly_projectile(
        context, world, targets, ricochet, ricochet.projectile, ricochet.collision_radius, dt);
    if (!hit)
      continue;

    context.outgoing.pending_contacts.push_back(
        contact_of_projectile_hit(ricochet.projectile, ricochet.collision_radius, *hit));
    spent.push_back(ricochet.entity_id);
  }

  for (const shared::entity_uid_t uid : spent)
    destroy_entity(context, uid);
}

} // namespace server

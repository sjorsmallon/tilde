#include "kooh_system.hpp"

#include "../../shared/projectile_sweep.hpp"
#include "../entity_lifecycle.hpp"
#include "projectile_flight.hpp"

#include <optional>
#include <vector>

namespace server
{

// Fly, push, destroy. The hook's mirror: its contacts move the shooter, and that is the row's.
void update_koohs(server_context_t& context, const shared::predicted_world_storage_t& world,
                  float dt)
{
  shared::Entity_System& entity_system = context.world.session.entity_system;

  std::vector<shared::entity_uid_t> spent;

  std::vector<shared::projectile_target_t> targets;
  shared::collect_projectile_targets(entity_system, targets);

  for (entities::Kooh_Entity& kooh : entity_system.entities_of<entities::Kooh_Entity>())
  {
    kooh.lifetime -= dt;
    if (kooh.lifetime <= 0.f)
    {
      spent.push_back(kooh.entity_id);
      continue;
    }

    const std::optional<shared::projectile_hit_t> hit = fly_projectile(
        context, world, targets, kooh, kooh.projectile, kooh.collision_radius, dt);
    if (!hit)
      continue;

    context.outgoing.pending_contacts.push_back(
        contact_of_projectile_hit(kooh.projectile, kooh.collision_radius, *hit));
    spent.push_back(kooh.entity_id);
  }

  for (const shared::entity_uid_t uid : spent)
    destroy_entity(context, uid);
}

} // namespace server

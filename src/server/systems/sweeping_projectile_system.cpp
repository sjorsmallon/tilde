#include "sweeping_projectile_system.hpp"

#include "../../shared/collision/projectile_sweep.hpp"
#include "../../shared/weapons.hpp"
#include "../entity_lifecycle.hpp"
#include "entities/generated/entities/shot_entity_generated.hpp"
#include "projectile_flight.hpp"

#include <optional>
#include <vector>

namespace server
{

void update_sweeping_projectiles(server_context_t& context,
                                 const shared::predicted_world_storage_t& world, float dt)
{
  shared::Entity_System& entity_system = context.world.session.entity_system;

  std::vector<shared::entity_uid_t> spent;

  std::vector<shared::projectile_target_t> targets;
  shared::collect_projectile_targets(entity_system, targets);

  for (entities::Shot_Entity& shot : entity_system.entities_of_type<entities::Shot_Entity>())
  {
    const shared::projectile_t& row = shared::get_projectile_parameters(shot.projectile);

    shot.lifetime -= dt;
    if (shot.lifetime <= 0.f)
    {
      if (row.expiry == shared::projectile_expiration_t::Arrives)
        context.outgoing.pending_contacts.push_back(
            contact_of_projectile_expiry(shot, shot.projectile));
      spent.push_back(shot.entity_id);
      continue;
    }

    const std::optional<shared::projectile_hit_t> hit =
        fly_projectile(context, world, targets, shot, shot.projectile, row.shot.collision_radius, dt);
    if (!hit)
      continue;

    context.outgoing.pending_contacts.push_back(
        contact_of_projectile_hit(shot.projectile, row.shot.collision_radius, *hit));
    spent.push_back(shot.entity_id);
  }

  for (const shared::entity_uid_t uid : spent)
    destroy_entity(context, uid);
}

} // namespace server

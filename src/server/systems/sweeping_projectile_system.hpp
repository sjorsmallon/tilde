#pragma once

#include "../../shared/collision/predicted_world.hpp"
#include "../../shared/collision/projectile_sweep.hpp"
#include "../../shared/weapons.hpp"
#include "../entity_lifecycle.hpp"
#include "../server_context.hpp"
#include "projectile_flight.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace server
{

// Fly, push, destroy: one tick for every Projectile_Entity, sweeping the owner's team view.
// A hit becomes one pending contact; what it DOES is the button's row, read by update_contacts.
template <typename Projectile_Entity>
void update_sweeping_projectiles(server_context_t& context,
                                 const shared::predicted_world_storage_t& world, float dt)
{
  shared::Entity_System& entity_system = context.world.session.entity_system;

  std::vector<shared::entity_uid_t> spent;

  std::vector<shared::projectile_target_t> targets;
  shared::collect_projectile_targets(entity_system, targets);

  for (Projectile_Entity& shot : entity_system.entities_of_type<Projectile_Entity>())
  {
    shot.lifetime -= dt;
    if (shot.lifetime <= 0.f)
    {
      if (shared::get_projectile_parameters(shot.projectile).expiry ==
          shared::projectile_expiration_t::Arrives)
        context.outgoing.pending_contacts.push_back(
            contact_of_projectile_expiry(shot, shot.projectile));
      spent.push_back(shot.entity_id);
      continue;
    }

    const std::optional<shared::projectile_hit_t> hit =
        fly_projectile(context, world, targets, shot, shot.projectile, shot.collision_radius, dt);
    if (!hit)
      continue;

    context.outgoing.pending_contacts.push_back(
        contact_of_projectile_hit(shot.projectile, shot.collision_radius, *hit));
    spent.push_back(shot.entity_id);
  }

  for (const shared::entity_uid_t uid : spent)
    destroy_entity(context, uid);
}

} // namespace server

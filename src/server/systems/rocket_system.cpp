#include "rocket_system.hpp"

#include "../../shared/projectile_sweep.hpp"
#include "../entity_lifecycle.hpp"
#include "projectile_flight.hpp"

#include <optional>
#include <vector>

namespace server
{

// Fly, push, destroy. A rocket that runs out of lifetime still arrives: an airburst is a contact
// with no surface, and the row's Explode arm splashes from wherever it was.
void update_rockets(server_context_t& context, const shared::predicted_world_storage_t& world,
                    float dt)
{
  shared::Entity_System& entity_system = context.world.session.entity_system;

  std::vector<shared::entity_uid_t> spent;

  std::vector<shared::projectile_target_t> targets;
  shared::collect_projectile_targets(entity_system, targets);

  for (entities::Rocket_Entity& rocket : entity_system.entities_of<entities::Rocket_Entity>())
  {
    rocket.lifetime -= dt;
    if (rocket.lifetime <= 0.f)
    {
      context.outgoing.pending_contacts.push_back(
          contact_of_projectile_expiry(rocket, rocket.projectile));
      spent.push_back(rocket.entity_id);
      continue;
    }

    const std::optional<shared::projectile_hit_t> hit = fly_projectile(
        context, world, targets, rocket, rocket.projectile, rocket.collision_radius, dt);
    if (!hit)
      continue;

    context.outgoing.pending_contacts.push_back(
        contact_of_projectile_hit(rocket.projectile, rocket.collision_radius, *hit));
    spent.push_back(rocket.entity_id);
  }

  for (const shared::entity_uid_t uid : spent)
    destroy_entity(context, uid);
}

} // namespace server

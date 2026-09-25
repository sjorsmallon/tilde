#include "projectile_flight.hpp"

#include "../../shared/weapons.hpp"

namespace server
{

std::optional<shared::projectile_hit_t>
fly_projectile(server_context_t& context, const shared::predicted_world_storage_t& world,
               Span<const shared::projectile_target_t> targets, entities::Entity& entity,
               entities::Projectile& projectile, float collision_radius, float dt)
{
  const shared::projectile_step_t step = shared::advance_projectile(
      shared::projectile_parameters_of(projectile), context.cvars->g_gravity,
      entity.position, projectile.velocity, dt);
  projectile.velocity = step.velocity;

  // You shoot through what you can walk through: the owner's team view, Free_For_All for nobody's.
  const entities::Player_Entity* owner =
      context.world.session.entity_system.get<entities::Player_Entity>(projectile.owner_uid);
  const shared::predicted_world_t view = shared::predicted_world_of(
      world, owner != nullptr ? owner->team_allegiance : entities::Team_Allegiance::Free_For_All);

  std::optional<shared::projectile_hit_t> hit = shared::sweep_projectile(
      context.world.session.bvh, view, entity.position, step.position, collision_radius);

  const std::optional<shared::projectile_hit_t> target_hit = shared::sweep_sphere_against_targets(
      targets, entity.position, step.position, collision_radius, projectile.owner_uid);
  if (target_hit && (!hit || target_hit->t < hit->t))
    hit = target_hit;

  entity.position = hit ? hit->position : step.position;
  return hit;
}

pending_contact_t contact_of_projectile_hit(const entities::Projectile& projectile,
                                            float collision_radius,
                                            const shared::projectile_hit_t& hit)
{
  return {.shooter_uid = projectile.owner_uid,
          .target_uid  = hit.entity_uid,
          .point       = hit.position - hit.normal * collision_radius,
          .normal      = hit.normal,
          .region      = shared::hit_region_t::Torso,
          .weapon      = projectile.weapon_id,
          .trigger     = projectile.trigger};
}

pending_contact_t contact_of_projectile_expiry(const entities::Entity& entity,
                                               const entities::Projectile& projectile)
{
  return {.shooter_uid = projectile.owner_uid,
          .target_uid  = shared::null_entity_uid,
          .point       = entity.position,
          .normal      = {0.f, 0.f, 0.f},
          .region      = shared::hit_region_t::Torso,
          .weapon      = projectile.weapon_id,
          .trigger     = projectile.trigger};
}

} // namespace server

#include "projectile_flight.hpp"

#include "../../shared/weapons.hpp"

namespace server
{

std::optional<hit_result_t> fly_projectile(server_context_t& context, entities::Entity& entity,
                                           entities::Projectile& projectile,
                                           float collision_radius, float dt)
{
  const shared::projectile_step_t step = shared::advance_projectile(
      shared::get_weapon_definition(projectile.weapon_id).projectile, context.cvars->g_gravity,
      entity.position, projectile.velocity, dt);
  projectile.velocity = step.velocity;

  // Back faces collide so a projectile spawned barely inside geometry still stops.
  const query_filter_t filter{.layers     = query_layers_t::All,
                              .ignore_uid = projectile.owner_uid,
                              .back_faces = back_face_mode_t::Collide};

  hit_result_t hit;
  if (cast_sphere(*context.world.physics, entity.position, step.position, collision_radius, filter,
                  hit))
  {
    entity.position = hit.position;
    return hit;
  }

  entity.position = step.position;
  return std::nullopt;
}

} // namespace server

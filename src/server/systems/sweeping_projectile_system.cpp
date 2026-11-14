#include "sweeping_projectile_system.hpp"

#include "../../shared/collision/projectile_sweep.hpp"
#include "../../shared/movement/player_constants.hpp"
#include "../../shared/weapon_instance.hpp"
#include "../../shared/weapons.hpp"
#include "../entity_lifecycle.hpp"
#include "entities/generated/entities/player_entity_generated.hpp"
#include "entities/generated/entities/shot_entity_generated.hpp"
#include "projectile_flight.hpp"

#include <optional>
#include <vector>

namespace server
{

namespace
{

std::optional<shared::laser_dot_t>
find_laser_dot_for_shot(server_context_t& context, const shared::predicted_world_storage_t& world,
                        const entities::Shot_Entity& shot, float guide_range)
{
  const shared::Entity_System&   entity_system = context.world.session.entity_system;
  const entities::Player_Entity* owner =
      entity_system.get<entities::Player_Entity>(shot.projectile.owner_uid);
  if (owner == nullptr)
    return std::nullopt;

  const shared::weapon_definition_t* held = shared::try_find_held_weapon_definition(entity_system, *owner);
  if (held == nullptr || held->weapon != shot.projectile.weapon_id)
    return std::nullopt;

  return shared::find_laser_dot(context.world.session.bvh,
                                shared::get_predicted_world_for_player(world, *owner),
                                shared::player_eye_position(owner->position),
                                linalg::direction_from_angles(owner->view_angle_yaw, owner->view_angle_pitch),
                                guide_range);
}

} // namespace

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

    if (row.guide_range > 0.f)
    {
      if (const std::optional<shared::laser_dot_t> laser_dot =
              find_laser_dot_for_shot(context, world, shot, row.guide_range))
      {
        shot.projectile.velocity =
            shared::steer_velocity_toward_point(shot.position, shot.projectile.velocity, laser_dot->position,
                                                row.guide_turn_degrees_per_second * dt);
        const linalg::view_angles_t heading = linalg::view_angles_from_direction(shot.projectile.velocity);
        shot.orientation = linalg::from_view_angles(heading.yaw_degrees, heading.pitch_degrees);
      }
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

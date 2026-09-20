#include "systems/hit_test_world.hpp"

#include "../shared/entities/generated/entities_generated.hpp"
#include "../shared/hitscan.hpp"
#include "../shared/player_animator.hpp"
#include "../shared/player_rig.hpp"
#include "log.hpp"
#include "server_context.hpp"

#include <vector>

namespace server
{

struct target_shape_t
{
  uint32_t volume_count = 0;
  bool     has_pose = false; // so we know if we have to reconstruct.
};

static target_shape_t target_shape_of(entities::entity_type type,
                                      const shared::player_rig_t &rig)
{
  switch (type)
  {
  case entities::entity_type::Player_Entity:     return {rig.volume_count(), true};
  case entities::entity_type::Damageable_Entity: return {1, false};

  case entities::entity_type::Invalid:
  case entities::entity_type::Player_Spawn_Entity:
  case entities::entity_type::Player_Spectate_Entity:
  case entities::entity_type::Weapon_Entity:
  case entities::entity_type::Rocket_Entity:
  case entities::entity_type::Hook_Entity:
  case entities::entity_type::Bubble_Entity:
  case entities::entity_type::Physics_Body_Entity:
  case entities::entity_type::Particle_Emitter_Entity:
  case entities::entity_type::Sound_Emitter_Entity:
  case entities::entity_type::Point_Light_Entity:
  case entities::entity_type::Spot_Light_Entity:
  case entities::entity_type::Directional_Light_Entity:
  case entities::entity_type::Trigger_Volume_Entity:
  case entities::entity_type::Jump_Pad_Entity:
  case entities::entity_type::Reflection_Volume_Entity:
  case entities::entity_type::Game_Rules_Entity:
  case entities::entity_type::Logic_Counter_Entity:
  case entities::entity_type::Geometry_Owner_Entity:
  case entities::entity_type::Ping_Marker_Entity:
  case entities::entity_type::Logic_Timer_Entity:
  case entities::entity_type::Path_Node_Entity:
  case entities::entity_type::Mover_Entity:
    break;
  }

  fatal_error("target_shape_of: {} is Mortal and has no hit volumes; add an arm",
              entities::entity_info(type).classname);
}

// Write one target's volumes into `slice`, and its pose if it has one.
static void build_target_volumes(const entities::Entity &entity,
  const shared::player_rig_t &rig,
  const aim_settings_t &settings,
  const Span<assets::posed_hitbox_t> posed_hitboxes,
  std::vector<shared::player_pose_t> &poses)
{
  if (const entities::Player_Entity* player = entities::entity_as<entities::Player_Entity>(&entity))
  {
    const shared::player_pose_t pose{.feet_position = player->position,
                                     .body_yaw      = player->body_yaw,
                                     .view_yaw      = player->view_angle_yaw,
                                     .view_pitch    = player->view_angle_pitch};

    shared::compute_player_hitboxes(rig, pose, settings, posed_hitboxes);
    poses.push_back(pose);
    return;
  }

  if (const entities::Damageable_Entity* damageable =
          entities::entity_as<entities::Damageable_Entity>(&entity))
  {
    // this is actually malformed because orientation is just blatantly ignored.
    posed_hitboxes[0] = assets::make_box_hit_volume(damageable->position + damageable->volume.position,
                                           damageable->volume.half_extents,
                                           shared::hit_region_t::Torso);
    return;
  }

  fatal_error("build_target_volumes: {} is Mortal and target_shape_of gave it volumes, but "
              "nothing here builds them",
              entities::entity_info(entity.type).classname);
}

void pose_all_targets(server_context_t &context)
{
  shared::posed_players_t &posed = context.posed_players;
  posed.targets.clear();
  posed.poses.clear();
  posed.built_for_tick = context.tick_number;

  const shared::player_rig_t &rig = shared::player_rig();
  const aim_settings_t settings   = aim_settings_from(*context.cvars);

  shared::Entity_System &system = context.world.session.entity_system;

  size_t total_volume_count = 0;
  size_t total_target_count = 0;
  size_t posed_target_count = 0;

  // query the trait, not the component.
  for (auto [entity, health] : system.entities_with_trait<entities::Mortal>())
  {
    // alraedy dead?
    if (health.current_health <= 0) continue;

    const target_shape_t shape = target_shape_of(entity.type, rig);
    total_volume_count += shape.volume_count;
    total_target_count += 1;
    posed_target_count += shape.has_pose ? 1 : 0;
  }

  posed.volumes.resize(total_volume_count);
  posed.targets.reserve(total_target_count);
  posed.poses.reserve(posed_target_count);

  // TWO passes, posed targets first, because `poses` describes a PREFIX of
  // `targets` -- send_shot_debug walks min(targets, poses) and
  // append_static_targets keys off poses.size() to tell a rewindable target from
  // a static one. Splitting the passes is what makes that prefix true BY
  // CONSTRUCTION; one pass got it right only because Player_Entity happens to be
  // declared before Damageable_Entity, which is not something to rest an
  // invariant on.
  size_t next_volume = 0;

  for (const bool should_be_posed : {true, false})
  {
    for (auto [entity, health] : system.entities_with_trait<entities::Mortal>())
    {
      if (health.current_health <= 0)
        continue;

      const target_shape_t shape = target_shape_of(entity.type, rig);
      if (shape.has_pose != should_be_posed)
        continue;

      const Span<assets::posed_hitbox_t> slice{posed.volumes.data() + next_volume, shape.volume_count};
      next_volume += shape.volume_count;

      build_target_volumes(entity, rig, settings, slice, posed.poses);
      posed.targets.push_back(shared::make_hitscan_target(
          entity.entity_id, Span<const assets::posed_hitbox_t>{slice}));
    }
  }
}

} // namespace server

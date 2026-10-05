#include "entities/generated/entities/damageable_entity_generated.hpp"
#include "entities/generated/entities/player_entity_generated.hpp"
#include "entities/generated/entities_tables_generated.hpp"
#include "systems/hit_test_world.hpp"

#include "../shared/damageable.hpp"
#include "../shared/entities/generated/entities_generated.hpp"
#include "../shared/hitscan.hpp"
#include "../shared/player_animator.hpp"
#include "../shared/player_rig.hpp"
#include "log.hpp"
#include "server_context.hpp"

#include <vector>

namespace server
{

enum class target_kind_t : uint8_t
{
  Rigged_Player,
  Box,
};

struct target_shape_t
{
  uint32_t volume_count = 0;
  bool     has_pose = false; // so we know if we have to reconstruct.
};

// The ONE place a Mortal type says what kind of target it is; the two switches below are exhaustive over the kinds.
static target_kind_t get_target_kind_for_entity_type(entities::entity_type type)
{
  if (type == entities::entity_type::Player_Entity)
    return target_kind_t::Rigged_Player;
  if (type == entities::entity_type::Damageable_Entity)
    return target_kind_t::Box;

  fatal_error("get_target_kind_for_entity_type: {} is Mortal and has no hit volumes; name its kind here",
              entities::entity_info(type).classname);
}

static target_shape_t get_target_shape_for_target_kind(target_kind_t kind, const shared::player_rig_t &rig)
{
  switch (kind)
  {
  case target_kind_t::Rigged_Player: return {rig.volume_count(), true};
  case target_kind_t::Box:           return {1, false};
  }

  fatal_error("get_target_shape_for_target_kind: target kind {} is not a target_kind_t", (uint32_t)kind);
}

// Write one target's volumes into `slice`, and its pose if it has one.
static void build_target_volumes(const entities::Entity &entity,
  target_kind_t kind,
  const shared::player_rig_t &rig,
  const aim_settings_t &settings,
  const Span<assets::posed_hitbox_t> posed_hitboxes,
  std::vector<shared::player_pose_t> &poses)
{
  switch (kind)
  {
  case target_kind_t::Rigged_Player:
  {
    const entities::Player_Entity &player = static_cast<const entities::Player_Entity &>(entity);
    const shared::player_pose_t pose{.feet_position = player.position,
                                     .body_yaw      = player.body_yaw,
                                     .view_yaw      = player.view_angle_yaw,
                                     .view_pitch    = player.view_angle_pitch};

    shared::compute_player_hitboxes(rig, pose, settings, posed_hitboxes);
    poses.push_back(pose);
    return;
  }
  case target_kind_t::Box:
    posed_hitboxes[0] = shared::damageable_hit_volume(
        static_cast<const entities::Damageable_Entity &>(entity));
    return;
  }
}

void pose_all_targets(server_context_t &context)
{
  shared::posed_players_t &posed = context.posed_players;
  posed.targets.clear();
  posed.poses.clear();
  posed.built_for_tick = context.tick_number;

  const shared::player_rig_t &rig = shared::player_rig();
  const aim_settings_t settings   = aim_settings_from_cvars(*context.cvars);

  shared::Entity_System &system = context.world.session.entity_system;

  size_t total_volume_count = 0;
  size_t total_target_count = 0;
  size_t posed_target_count = 0;

  // query the trait, not the component.
  for (auto [entity, health] : system.entities_with_trait<entities::Mortal>())
  {
    // alraedy dead?
    if (health.current_health <= 0) continue;

    const target_shape_t shape = get_target_shape_for_target_kind(get_target_kind_for_entity_type(entity.type), rig);
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

      const target_kind_t  kind  = get_target_kind_for_entity_type(entity.type);
      const target_shape_t shape = get_target_shape_for_target_kind(kind, rig);
      if (shape.has_pose != should_be_posed)
        continue;

      const Span<assets::posed_hitbox_t> slice{posed.volumes.data() + next_volume, shape.volume_count};
      next_volume += shape.volume_count;

      build_target_volumes(entity, kind, rig, settings, slice, posed.poses);
      posed.targets.push_back(shared::make_hitscan_target(
          entity.entity_id, Span<const assets::posed_hitbox_t>{slice}));
    }
  }
}

} // namespace server

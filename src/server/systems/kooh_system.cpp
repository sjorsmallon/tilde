#include "kooh_system.hpp"

#include "../../shared/linalg.hpp"
#include "../../shared/player_constants.hpp"
#include "../../shared/player_move.hpp"
#include "../entity_lifecycle.hpp"
#include "projectile_flight.hpp"

#include <algorithm>
#include <vector>

namespace server
{

static constexpr float SHORTEST_PULL_SECONDS = 0.3f;
static constexpr float LONGEST_PULL_SECONDS  = 1.0f;

// player_move works on the hull CENTRE, so the tether aims at the caster's, not at their feet.
static vec3f hook_anchor_of(const entities::Player_Entity& shooter)
{
  return shooter.position + vec3f{0.f, shared::player_half_height, 0.f};
}

// The arc that lands `victim` on `shooter` after the flight time, gravity included.
static void throw_shooter_at_victim(
    const server_context_t& context,
    entities::Player_Entity& shooter,
    const entities::Player_Entity& victim)
{
  const vec3f to_victim  = victim.position - shooter.position;
  const float flight_seconds = std::clamp(
      linalg::length(to_victim) / context.cvars->sv_hook_pull_speed, SHORTEST_PULL_SECONDS,
      LONGEST_PULL_SECONDS);

  shared::apply_impulse(shared::movement_settings_from(*context.cvars), shooter.velocity,
                        shooter.movement,
                        {.velocity = to_victim * (1.f / flight_seconds) +
                                     vec3f{0.f, 0.5f * context.cvars->g_gravity * flight_seconds,
                                           0.f}});
}

static void refresh_kooh_anchors(server_context_t& context)
{
  shared::Entity_System& entity_system = context.world.session.entity_system;

  for (entities::Player_Entity& player : entity_system.entities_of<entities::Player_Entity>())
  {
    if (player.movement.active_override != entities::Movement_Override::Reel)
      continue;

    const entities::Player_Entity* target =
        entity_system.get<entities::Player_Entity>(player.movement.override_target_uid);

    if (target == nullptr || target->health.current_health <= 0)
    {
      player.movement.active_override            = entities::Movement_Override::None;
      player.movement.override_target_uid        = shared::null_entity_uid;
      player.movement.override_seconds_remaining = 0.f;
      shared::apply_impulse(shared::movement_settings_from(*context.cvars), player.velocity,
                            player.movement, {.velocity = player.velocity});
      continue;
    }

    player.movement.override_target_position = hook_anchor_of(*target);
  }
}

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

    spent.push_back(kooh.entity_id);

    entities::Player_Entity* victim  = entity_system.get<entities::Player_Entity>(hit->entity_uid);
    entities::Player_Entity* shooter = entity_system.get<entities::Player_Entity>(kooh.projectile.owner_uid);

    const bool shooter_can_be_pulled = shooter != nullptr && shooter->health.current_health > 0;
    if (!shooter_can_be_pulled || shooter == nullptr || victim == nullptr)
      continue;

    if (kooh.reels_player)
    {
      shooter->movement.active_override            = entities::Movement_Override::Reel;
      shooter->movement.override_target_uid        = victim->entity_id;
      shooter->movement.override_target_position   = hook_anchor_of(*victim);
      shooter->movement.override_seconds_remaining = context.cvars->sv_hook_max_pull_seconds;
      shooter->movement.override_speed             = context.cvars->sv_hook_pull_speed;
      shooter->movement.override_arrive_radius     = context.cvars->sv_hook_arrive_radius;
    }
    else
    {
      throw_shooter_at_victim(context, *shooter, *victim);
    }
  }

  for (const shared::entity_uid_t uid : spent)
    destroy_entity(context, uid);

  refresh_kooh_anchors(context);
}

} // namespace server

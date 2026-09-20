#include "hook_system.hpp"

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
static void throw_victim_at_shooter(const server_context_t& context,
                                    const entities::Hook_Entity& hook,
                                    const entities::Player_Entity& shooter,
                                    entities::Player_Entity& victim)
{
  const vec3f to_shooter     = shooter.position - victim.position;
  const float flight_seconds = std::clamp(linalg::length(to_shooter) / hook.pull_speed,
                                          SHORTEST_PULL_SECONDS, LONGEST_PULL_SECONDS);

  shared::apply_impulse(shared::movement_settings_from(*context.cvars), victim.velocity,
                        victim.movement,
                        {.velocity = to_shooter * (1.f / flight_seconds) +
                                     vec3f{0.f, 0.5f * context.cvars->g_gravity * flight_seconds,
                                           0.f}});
}

// The reel's other half: player_move is pure and cannot resolve a uid, so the
// anchor travels as a position and this is the ONE place it is written. A
// caster who left, died or dropped the hook detaches here rather than leaving
// the victim reeling toward a stale point.
static void refresh_hook_anchors(server_context_t& context)
{
  shared::Entity_System& entity_system = context.world.session.entity_system;

  for (entities::Player_Entity& victim : entity_system.entities_of<entities::Player_Entity>())
  {
    if (victim.movement.seconds_of_hook_pull_remaining <= 0.f)
      continue;

    const entities::Player_Entity* shooter =
        entity_system.get<entities::Player_Entity>(victim.movement.hook_anchor_uid);

    if (shooter == nullptr || shooter->health.current_health <= 0)
    {
      victim.movement.seconds_of_hook_pull_remaining = 0.f;
      victim.movement.hook_anchor_uid                = shared::null_entity_uid;
      shared::apply_impulse(shared::movement_settings_from(*context.cvars), victim.velocity,
                            victim.movement, {.velocity = victim.velocity});
      continue;
    }

    victim.movement.hook_anchor_position = hook_anchor_of(*shooter);
  }
}

void update_hooks(server_context_t& context, float dt)
{
  shared::Entity_System& entity_system = context.world.session.entity_system;

  std::vector<shared::entity_uid_t> spent;

  for (entities::Hook_Entity& hook : entity_system.entities_of<entities::Hook_Entity>())
  {
    hook.lifetime -= dt;
    if (hook.lifetime <= 0.f)
    {
      spent.push_back(hook.entity_id);
      continue;
    }

    const std::optional<hit_result_t> hit =
        fly_projectile(context, hook, hook.projectile, hook.collision_radius, dt);
    if (!hit)
      continue;

    spent.push_back(hook.entity_id);

    entities::Player_Entity* victim  = entity_system.get<entities::Player_Entity>(hit->entity_id);
    entities::Player_Entity* shooter =
        entity_system.get<entities::Player_Entity>(hook.projectile.owner_uid);

    const bool victim_can_be_pulled = victim != nullptr && victim->health.current_health > 0;
    if (!victim_can_be_pulled || shooter == nullptr)
      continue;

    if (hook.reels_target)
    {
      victim->movement.hook_anchor_uid                = shooter->entity_id;
      victim->movement.hook_anchor_position           = hook_anchor_of(*shooter);
      victim->movement.seconds_of_hook_pull_remaining = context.cvars->pm_hook_max_pull_seconds;
    }
    else
    {
      throw_victim_at_shooter(context, hook, *shooter, *victim);
    }
  }

  for (const shared::entity_uid_t uid : spent)
    destroy_entity(context, uid);

  refresh_hook_anchors(context);
}

} // namespace server

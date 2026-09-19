#include "hook_system.hpp"

#include "../../shared/linalg.hpp"
#include "../../shared/player_move.hpp"
#include "../entity_lifecycle.hpp"
#include "projectile_flight.hpp"

#include <algorithm>
#include <vector>

namespace server
{

static constexpr float SHORTEST_PULL_SECONDS = 0.3f;
static constexpr float LONGEST_PULL_SECONDS  = 1.0f;

// The arc that lands `victim` on `shooter` after the flight time, gravity included.
static void throw_victim_at_shooter(const server_context_t& context,
                                    const entities::Hook_Entity& hook,
                                    const entities::Player_Entity& shooter,
                                    entities::Player_Entity& victim)
{
  const vec3f to_shooter     = shooter.position - victim.position;
  const float flight_seconds = std::clamp(linalg::length(to_shooter) / hook.pull_speed,
                                          SHORTEST_PULL_SECONDS, LONGEST_PULL_SECONDS);

  victim.velocity = to_shooter * (1.f / flight_seconds) +
                    vec3f{0.f, 0.5f * context.cvars->g_gravity * flight_seconds, 0.f};
  borrow_speed(victim.movement, flight_seconds);
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
    if (victim_can_be_pulled && shooter != nullptr)
      throw_victim_at_shooter(context, hook, *shooter, *victim);
  }

  for (const shared::entity_uid_t uid : spent)
    destroy_entity(context, uid);
}

} // namespace server

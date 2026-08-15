#include "../shared/entities/entity_reflection.hpp"
#include "damage.hpp"

#include "../shared/log.hpp"
#include "server_api.hpp"
#include "systems/respawn_system.hpp"

namespace server
{

// Player_Entity-specific path: subtract HP, write knockback velocity directly
// (Jolt impulses are no-ops on kinematic capsules, and AddLinearVelocity gets
// clobbered by the next set_kinematic_pose), detect the >0 → <=0 crossing.
static void apply_damage_to_player(server_context_t &context,
                                   const damage_info_t &info,
                                   entities::Player_Entity &player)
{
  if (player.health <= 0)
    return; // corpses don't take additional damage

  // Knockback uses the inflictor's position when set (e.g. rocket projectile),
  // otherwise the attacker's source_position. info.source_position carries
  // whichever the caller meant; degenerate (zero) source produces a zero
  // direction and no knockback, which is the right behavior for "you just
  // died because the world said so" actions like trigger_kill.
  if (info.knockback_force != 0.f)
  {
    const vec3f victim_center = player.position + vec3f{0.f, 38.f, 0.f};
    vec3f to_victim = victim_center - info.source_position;
    const float distance = linalg::length(to_victim);
    if (distance > 1e-4f)
    {
      const vec3f direction = to_victim * (1.f / distance);
      player.velocity = player.velocity + direction * info.knockback_force;
    }
  }

  const int32_t health_before = player.health;
  player.health -= static_cast<int32_t>(info.amount);

  if (health_before > 0 && player.health <= 0)
  {
    // Latched here rather than in the respawn scheduler because THIS is the
    // crossing; the scheduler already keys its own map by uid and would be a
    // second place that has to agree on when the player died. Clients play the
    // death clip off this stamp, so it must be written before the snapshot this
    // tick produces.
    player.death_tick = get_tick_number();

    shared::Player_Died died{};
    died.victim_id    = info.victim_uid;
    died.attacker_id  = info.attacker_uid;
    died.weapon_id    = info.weapon_id;
    died.was_headshot = info.was_headshot;
    shared::fire_player_died(context.outgoing.events, died);
    // Bots and humans share this path — both are Player_Entity instances.
    schedule_respawn(context, info.victim_uid, get_tick_number());
  }
}

// Physics_Body_Entity path: dynamic bodies take impulse, not HP. Matches the
// pre-Jolt code's "knockback_force is a velocity delta" convention, so we
// use AddLinearVelocity rather than AddImpulse (which would scale by mass).
static void apply_damage_to_physics_body(server_context_t &context,
                                         const damage_info_t &info,
                                         entities::Physics_Body_Entity &body)
{
  if (info.knockback_force == 0.f) return;

  vec3f to_body = body.position - info.source_position;
  const float distance = linalg::length(to_body);
  const vec3f direction = (distance > 1e-4f)
                              ? to_body * (1.f / distance)
                              : vec3f{0.f, 1.f, 0.f};
  add_linear_velocity(*context.world.physics, info.victim_uid,
                      direction * info.knockback_force);
}

void inflict_damage(server_context_t &context, const damage_info_t &info)
{
  if (info.victim_uid == 0)
  {
    log_error("inflict_damage: victim_uid == 0; dropping (this is a bug at "
              "the call site — every damage source must name a victim)");
    return;
  }

  shared::game_session_t &session = context.world.session;

  // Dispatch on entity type. Two damageable types today; if this grows past
  // ~6 cases or any case past ~50 lines, promote to a registration table
  // keyed by entity_type (see events_plan.md §"Per-entity damage dispatch").
  //
  // get<T>(uid) returns nullptr for both "no such uid" and "uid names a
  // different type", so each `if` is simultaneously the lookup and the type
  // test — and the fallthrough below is the genuine "not damageable" case.
  if (auto *player = session.entity_system.get<entities::Player_Entity>(info.victim_uid))
  {
    apply_damage_to_player(context, info, *player);
    return;
  }
  if (auto *body = session.entity_system.get<entities::Physics_Body_Entity>(info.victim_uid))
  {
    apply_damage_to_physics_body(context, info, *body);
    return;
  }

  log_error("inflict_damage: no damageable entity matches victim_uid {} "
            "(neither Player_Entity nor Physics_Body_Entity)",
            static_cast<uint64_t>(info.victim_uid));
}

} // namespace server

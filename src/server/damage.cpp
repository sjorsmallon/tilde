#include "damage.hpp"

#include "../shared/entities/entity_list.hpp"
#include "../shared/entities/physics_body_entity.hpp"
#include "../shared/entities/player_entity.hpp"
#include "../shared/log.hpp"
#include "game_events.hpp"
#include "server_api.hpp"
#include "systems/respawn_system.hpp"

namespace server
{

static network::Player_Entity *
find_player_by_uid(shared::game_session_t &session, shared::entity_uid_t uid)
{
  auto *players = session.entity_system.get_entities<network::Player_Entity>(
      entity_type::PLAYER);
  if (!players) return nullptr;
  for (auto &p : *players)
    if (static_cast<shared::entity_uid_t>(p.entity_id) == uid) return &p;
  return nullptr;
}

static network::Physics_Body_Entity *
find_physics_body_by_uid(shared::game_session_t &session,
                         shared::entity_uid_t uid)
{
  auto *pool = session.entity_system.get_entities<network::Physics_Body_Entity>(
      entity_type::PHYSICS_BODY);
  if (!pool) return nullptr;
  for (auto &b : *pool)
    if (static_cast<shared::entity_uid_t>(b.entity_id) == uid) return &b;
  return nullptr;
}

// Player_Entity-specific path: subtract HP, write knockback velocity directly
// (Jolt impulses are no-ops on kinematic capsules, and AddLinearVelocity gets
// clobbered by the next set_kinematic_pose), detect the >0 → <=0 crossing.
static void apply_damage_to_player(server_context_t &context,
                                   const damage_info_t &info,
                                   network::Player_Entity &player)
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
    shared::game_event_t died_event{};
    died_event.kind = shared::game_event_kind_t::PLAYER_DIED;
    died_event.player_died.victim_id    = info.victim_uid;
    died_event.player_died.attacker_id  = info.attacker_uid;
    died_event.player_died.weapon_id    = info.weapon_id;
    died_event.player_died.was_headshot = info.was_headshot;
    fire_game_event(context, died_event);
    // Bots and humans share this path — both are Player_Entity instances.
    schedule_respawn(context, info.victim_uid, get_tick_number());
  }
}

// Physics_Body_Entity path: dynamic bodies take impulse, not HP. Matches the
// pre-Jolt code's "knockback_force is a velocity delta" convention, so we
// use AddLinearVelocity rather than AddImpulse (which would scale by mass).
static void apply_damage_to_physics_body(server_context_t &context,
                                         const damage_info_t &info,
                                         network::Physics_Body_Entity &body)
{
  if (info.knockback_force == 0.f) return;

  vec3f to_body = body.position - info.source_position;
  const float distance = linalg::length(to_body);
  const vec3f direction = (distance > 1e-4f)
                              ? to_body * (1.f / distance)
                              : vec3f{0.f, 1.f, 0.f};
  add_linear_velocity(*context.physics, info.victim_uid,
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

  shared::game_session_t &session = context.session;

  // Dispatch on entity type. Two damageable types today; if this grows past
  // ~6 cases or any case past ~50 lines, promote to a registration table
  // keyed by entity_type (see events_plan.md §"Per-entity damage dispatch").
  if (auto *player = find_player_by_uid(session, info.victim_uid))
  {
    apply_damage_to_player(context, info, *player);
    return;
  }
  if (auto *body = find_physics_body_by_uid(session, info.victim_uid))
  {
    apply_damage_to_physics_body(context, info, *body);
    return;
  }

  log_error("inflict_damage: no damageable entity matches victim_uid {} "
            "(neither Player_Entity nor Physics_Body_Entity)",
            static_cast<uint64_t>(info.victim_uid));
}

} // namespace server

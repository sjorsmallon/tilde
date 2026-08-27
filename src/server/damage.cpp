#include "../shared/entities/entity_reflection.hpp"
#include "damage.hpp"

#include "../shared/log.hpp"
#include "server_api.hpp"
#include "systems/game_rules_system.hpp"
#include "systems/respawn_system.hpp"

namespace server
{

// The center a knockback direction is measured to. One expression, because the
// batch has to compute it before it knows which hits it is summing.
static vec3f player_knockback_center(const entities::Player_Entity &player)
{
  return player.position + vec3f{0.f, 38.f, 0.f};
}

// The velocity delta ONE hit imparts. Knockback uses the inflictor's position
// when set (e.g. rocket projectile), otherwise the attacker's source_position;
// info.source_position carries whichever the caller meant. A degenerate (zero)
// source produces no knockback, which is the right behavior for "you just died
// because the world said so" actions like trigger_kill.
//
// Split out of the apply so a batch can SUM the deltas. Two rockets landing on
// one player in a tick used to impart ONE rocket's push, because the second call
// met the corpse gate and returned before reaching this.
static vec3f knockback_velocity_for(const damage_info_t &info, const vec3f &victim_center)
{
  if (info.knockback_force == 0.f)
    return {0.f, 0.f, 0.f};

  const vec3f to_victim = victim_center - info.source_position;
  const float distance  = linalg::length(to_victim);
  if (distance <= 1e-4f)
    return {0.f, 0.f, 0.f};

  return to_victim * (info.knockback_force / distance);
}

// Player_Entity-specific path: write knockback velocity directly (Jolt impulses
// are no-ops on kinematic capsules, and AddLinearVelocity gets clobbered by the
// next set_kinematic_pose), subtract HP, detect the >0 → <=0 crossing.
//
// Takes a TOTAL rather than one hit, so the single-hit and batched paths cannot
// disagree about what dying involves. `credited` names who the kill goes to and
// with what weapon: for a single hit that is the hit itself, for a batch it is
// the largest contributor — deliberately not "whichever was applied last".
static void apply_player_damage_total(server_context_t &context,
                                      entities::Player_Entity &player,
                                      shared::entity_uid_t victim_uid,
                                      float total_damage,
                                      const vec3f &knockback_velocity,
                                      const damage_info_t &credited)
{
  player.velocity = player.velocity + knockback_velocity;

  // Knockback is deliberately ABOVE the gate and health is below it: a shove is
  // not damage, so a frozen player can still be rocket-jumped by someone whose
  // round has not started. Everything that follows -- the crossing, PLAYER_DIED
  // and the respawn timer -- hangs off the health write, so gating it here gates
  // them together rather than in three places that can drift.
  //
  // The SCORE is the one thing that does not hang off it, because damage
  // applies in warmup and a warmup frag counts for nothing. See below.
  if (!can_take_damage(context))
    return;

  const int32_t health_before = player.health;
  player.health -= static_cast<int32_t>(total_damage);

  if (health_before > 0 && player.health <= 0)
  {
    // Latched here rather than in the respawn scheduler because THIS is the
    // crossing; the scheduler already keys its own map by uid and would be a
    // second place that has to agree on when the player died. Clients play the
    // death clip off this stamp, so it must be written before the snapshot this
    // tick produces.
    player.death_tick = get_tick_number();

    shared::Player_Died died{};
    died.victim_id    = victim_uid;
    died.attacker_id  = credited.attacker_uid;
    died.weapon_id    = credited.weapon_id;
    died.was_headshot = credited.was_headshot;
    shared::fire_player_died(context.outgoing.events, died);

    // Scored at the crossing rather than off Player_Died, so the score and the
    // event cannot disagree about what happened. A world kill and a suicide
    // both count as a death and award nobody; `get` returning null for a
    // non-player attacker (a trigger volume, a destroyed rocket owner) is the
    // same answer, not a special case.
    //
    // is_round_live, not can_take_damage: the kill happened -- it fired
    // Player_Died and the kill feed shows it -- but a warmup frag is not a frag.
    // This is the one place the two gates are supposed to differ.
    if (is_round_live(context))
    {
      ++player.deaths;
      if (credited.attacker_uid != victim_uid)
      {
        if (entities::Player_Entity* attacker =
                context.world.session.entity_system.get<entities::Player_Entity>(
                    credited.attacker_uid))
          ++attacker->kills;
      }
    }

    // Bots and humans share this path — both are Player_Entity instances.
    //
    // The one field that separates a deathmatch from an elimination round: with
    // respawn_during_round false nothing is scheduled, so the corpse stays down
    // until the round boundary puts every player back on a marker. No branch on
    // Game_Mode anywhere, which is the point of the table.
    //
    // OUTSIDE a live round it always schedules, whatever the mode says: staying
    // down is what makes a round a round, and there is no round to be out of in
    // warmup. Without this an elimination mode's warmup is a room that fills up
    // with corpses until the match starts.
    if (current_mode(context).respawn_during_round || !is_round_live(context))
      schedule_respawn(context, victim_uid, get_tick_number());
  }
}

static void apply_damage_to_player(server_context_t &context,
                                   const damage_info_t &info,
                                   entities::Player_Entity &player)
{
  if (player.health <= 0)
    return; // corpses don't take additional damage

  apply_player_damage_total(
      context, player, info.victim_uid, info.amount,
      knockback_velocity_for(info, player_knockback_center(player)), info);
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

void inflict_damage_batch(server_context_t &context, Span<const pending_hit_t> hits)
{
  shared::game_session_t &session = context.world.session;

  for (uint32_t i = 0; i < hits.size(); ++i)
  {
    const shared::entity_uid_t victim_uid = hits[i].info.victim_uid;

    // ONE pass per victim, not per hit: skip this entry if an earlier one named
    // the same victim. A linear rescan rather than a map because a tick holds a
    // handful of hits, and because a map would put a container's iteration order
    // between the input list and the outcome — the exact class of dependency
    // this function exists to remove.
    bool already_resolved = false;
    for (uint32_t earlier = 0; earlier < i; ++earlier)
      already_resolved |= hits[earlier].info.victim_uid == victim_uid;
    if (already_resolved)
      continue;

    entities::Player_Entity *player =
        session.entity_system.get<entities::Player_Entity>(victim_uid);

    if (!player)
    {
      // Not a player. Nothing to resolve — impulses are additive and a physics
      // body has no health to contend over — so each hit takes the single-hit
      // path unchanged, including its uid == 0 and not-damageable diagnostics.
      for (uint32_t j = i; j < hits.size(); ++j)
        if (hits[j].info.victim_uid == victim_uid)
          inflict_damage(context, hits[j].info);
      continue;
    }

    if (player->health <= 0)
      continue; // corpses don't take additional damage

    const vec3f victim_center = player_knockback_center(*player);

    float                total_damage = 0.f;
    vec3f                knockback{0.f, 0.f, 0.f};
    const damage_info_t *credited = nullptr;

    for (uint32_t j = i; j < hits.size(); ++j)
    {
      const damage_info_t &info = hits[j].info;
      if (info.victim_uid != victim_uid)
        continue;

      total_damage += info.amount;
      knockback = knockback + knockback_velocity_for(info, victim_center);

      // Kill credit: most damage dealt to this victim this tick, ties broken by
      // the lower attacker uid. An explicit rule, because the alternative is the
      // move sort silently deciding it — and deliberately NOT the shot's rewind
      // bracket, which would hand every contested kill to whoever has the higher
      // ping. See lag_compensation_def.md.
      const bool outranks_credited =
          !credited || info.amount > credited->amount ||
          (info.amount == credited->amount && info.attacker_uid < credited->attacker_uid);
      if (outranks_credited)
        credited = &info;
    }

    // Never null: the j == i iteration always matches victim_uid.
    apply_player_damage_total(context, *player, victim_uid, total_damage, knockback,
                              *credited);
  }
}

} // namespace server

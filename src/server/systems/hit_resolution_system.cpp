#include "systems/hit_resolution_system.hpp"

#include "../shared/effects/generated/effects_generated.hpp"
#include "../shared/player_constants.hpp"
#include "../shared/movement_override.hpp"
#include "../shared/player_move.hpp"
#include "systems/inventory_system.hpp"
#include "../shared/round_phase_rules.hpp"
#include "systems/game_rules_system.hpp"
#include "../shared/subtick.hpp"
#include "damage.hpp"
#include "entity_lifecycle.hpp"
#include "log.hpp"
#include "server_context.hpp"
#include "weapon_fire.hpp"

#include <utility>

namespace server
{

// Before the damage pass, so a victim's knockback lands where the swap put them.
static void apply_pending_swaps(server_context_t& context)
{
  for (const pending_swap_t& swap : context.outgoing.pending_swaps)
  {
    entities::Player_Entity* shooter =
        context.world.session.entity_system.get<entities::Player_Entity>(swap.shooter_uid);
    entities::Player_Entity* target =
        context.world.session.entity_system.get<entities::Player_Entity>(swap.target_uid);
    if (shooter == nullptr || target == nullptr)
    {
      log_error("swap between uid {} and uid {} dropped: one of them is no longer a player",
                swap.shooter_uid, swap.target_uid);
      continue;
    }

    if (target->health.current_health <= 0)
      continue;

    std::swap(shooter->position, target->position);
    std::swap(shooter->velocity, target->velocity);
  }
  context.outgoing.pending_swaps.clear();
}

// Beside the swaps and before the damage pass, for the swap's reason. The
// remnant is SPENT: a teleport is a return to where you stood, and standing
// there again is the next set. Velocity is kept, so a fall or a run carries.
static void apply_pending_teleports(server_context_t& context)
{
  shared::game_session_t& session = context.world.session;

  for (const pending_teleport_t& teleport : context.outgoing.pending_teleports)
  {
    entities::Player_Entity* shooter =
        session.entity_system.get<entities::Player_Entity>(teleport.shooter_uid);
    const entities::Remnant_Entity* remnant =
        session.entity_system.get<entities::Remnant_Entity>(teleport.remnant_uid);
    if (shooter == nullptr || remnant == nullptr)
    {
      log_error("teleport of uid {} to remnant uid {} dropped: one of them is gone",
                teleport.shooter_uid, teleport.remnant_uid);
      continue;
    }

    if (shooter->health.current_health <= 0)
      continue;

    shooter->position = remnant->position;

    destroy_entity(context, teleport.remnant_uid);
  }
  context.outgoing.pending_teleports.clear();
}

// After the swaps, so the line between the two is the one they end the tick on.
static void apply_pending_magnets(server_context_t& context)
{
  shared::game_session_t& session = context.world.session;

  for (const pending_magnet_t& magnet : context.outgoing.pending_magnets)
  {
    entities::Player_Entity* shooter =
        session.entity_system.get<entities::Player_Entity>(magnet.shooter_uid);
    entities::Player_Entity* target =
        session.entity_system.get<entities::Player_Entity>(magnet.target_uid);
    if (shooter == nullptr || target == nullptr)
      continue;

    if (shooter->health.current_health <= 0 || target->health.current_health <= 0)
      continue;

    if (!carries_weapon(session, *target, entities::Weapon::Magnet))
      continue;

    const vec3f to_target = target->position - shooter->position;
    const float distance  = linalg::length(to_target);
    if (distance < 1.f)
      continue;

    const vec3f toward_target = to_target * (magnet.speed / distance);
    const shared::movement_settings_t settings = shared::movement_settings_from(*context.cvars);

    shared::apply_impulse(settings, shooter->velocity, shooter->movement,
                          {.horizontal = shared::impulse_mode_t::Add,
                           .vertical   = shared::impulse_mode_t::Add,
                           .velocity   = toward_target});
    shared::apply_impulse(settings, target->velocity, target->movement,
                          {.horizontal = shared::impulse_mode_t::Add,
                           .vertical   = shared::impulse_mode_t::Add,
                           .velocity   = toward_target * -1.f});
  }
  context.outgoing.pending_magnets.clear();
}

// A hit RENEWS the reel rather than pushing: the pull itself is player_move's, so the shooter predicts it.
static void apply_pending_tethers(server_context_t& context)
{
  shared::game_session_t& session = context.world.session;

  for (const pending_tether_t& tether : context.outgoing.pending_tethers)
  {
    entities::Player_Entity* shooter =
        session.entity_system.get<entities::Player_Entity>(tether.shooter_uid);
    const entities::Player_Entity* target =
        session.entity_system.get<entities::Player_Entity>(tether.target_uid);
    if (shooter == nullptr || target == nullptr)
      continue;

    if (shooter->health.current_health <= 0 || target->health.current_health <= 0)
      continue;

    shooter->movement.active_override            = entities::Movement_Override::Reel;
    shooter->movement.override_target_uid        = target->entity_id;
    shooter->movement.override_target_position =
        target->position + vec3f{0.f, shared::player_half_height, 0.f};
    shooter->movement.override_seconds_remaining = tether.seconds;
    shooter->movement.override_speed             = tether.speed;
    shooter->movement.override_arrive_radius     = context.cvars->sv_hook_arrive_radius;
  }
  context.outgoing.pending_tethers.clear();
}

// After the swaps, so the box freezes where the swap put them. A hit on a
// player already frozen RELEASES them: the clock is spent and the next step
// thaws through the one exit door, with the velocity the freeze holds. A Statue
// is zeroed at attach; the step then falls it (movement_override.hpp).
static void apply_pending_freezes(server_context_t& context)
{
  shared::game_session_t& session = context.world.session;

  for (const pending_freeze_t& freeze : context.outgoing.pending_freezes)
  {
    entities::Player_Entity* target =
        session.entity_system.get<entities::Player_Entity>(freeze.target_uid);
    if (target == nullptr || target->health.current_health <= 0)
      continue;

    if (shared::override_freezes(target->movement.active_override))
    {
      target->movement.override_seconds_remaining = 0.f;
      continue;
    }

    target->movement.active_override            = freeze.kind;
    target->movement.override_target_uid        = freeze.shooter_uid;
    target->movement.override_seconds_remaining = freeze.seconds;

    if (freeze.kind == entities::Movement_Override::Statue)
      shared::apply_impulse(shared::movement_settings_from(*context.cvars), target->velocity,
                            target->movement,
                            {.horizontal = shared::impulse_mode_t::Set,
                             .vertical   = shared::impulse_mode_t::Set,
                             .velocity   = {}});
  }
  context.outgoing.pending_freezes.clear();
}

static void apply_pending_hits(server_context_t& context)
{
  for (const pending_hit_t &pending : context.outgoing.pending_hits)
  {
    auto impact_fx = shared::Shot_Impact{};
    impact_fx.origin          = pending.impact_point;
    impact_fx.normal          = pending.impact_normal;
    impact_fx.attached_entity = pending.info.victim_uid;
    impact_fx.region          = static_cast<uint16_t>(pending.region);
    impact_fx.weapon          = pending.info.weapon_id;
    shared::fire_shot_impact(context.outgoing.effects, impact_fx);

    // The hitmarker, for the shooter only, as replicated state. Their own client
    // plays it off this stamp advancing -- see Player_Entity::last_hit_tick in
    // entities.def for why it is not an effect. Every contributor gets one, not
    // just the one credited with the kill: you hit them, so you saw it land.
    //
    // Gated on the same query the health write is: a hitmarker is a claim that
    // damage landed, so outside the round it would be feedback for a hit that
    // did nothing. The Shot_Impact above is NOT gated -- it says where the
    // bullet went, which is true either way.
    entities::Player_Entity* attacker =
        can_take_damage(context)
            ? context.world.session.entity_system.get<entities::Player_Entity>(
                  pending.info.attacker_uid)
            : nullptr;
    if (attacker)
    {
      attacker->last_hit_tick         = context.tick_number;
      attacker->last_hit_was_headshot = pending.info.was_headshot;
    }
  }

  inflict_damage_batch(context, context.outgoing.pending_hits);
  context.outgoing.pending_hits.clear();
}

static void finish_reloads_that_came_due(server_context_t& context)
{
  const shared::subtick_time_t end_of_tick =
      shared::subtick_time(context.tick_number + 1, 0);
  for (entities::Player_Entity& player :
       context.world.session.entity_system.entities_of<entities::Player_Entity>())
  {
    if (is_reloading(player) && player.reload_complete_time <= end_of_tick)
      finish_reload(context.world.session, player);
  }
}

void update_hit_resolution(server_context_t& context)
{
  apply_pending_swaps(context);
  apply_pending_teleports(context);
  apply_pending_magnets(context);
  apply_pending_tethers(context);
  apply_pending_freezes(context);
  apply_pending_hits(context);
  finish_reloads_that_came_due(context);
}

} // namespace server

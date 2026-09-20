#include "systems/hit_resolution_system.hpp"

#include "../shared/effects/generated/effects_generated.hpp"
#include "../shared/physics.hpp"
#include "../shared/player_constants.hpp"
#include "../shared/round_phase_rules.hpp"
#include "systems/game_rules_system.hpp"
#include "../shared/subtick.hpp"
#include "damage.hpp"
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

    for (entities::Player_Entity* swapped : {shooter, target})
      set_kinematic_pose(*context.world.physics, swapped->entity_id,
                         swapped->position + vec3f{0.f, shared::player_capsule_center_offset, 0.f},
                         swapped->velocity);
  }
  context.outgoing.pending_swaps.clear();
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
  apply_pending_hits(context);
  finish_reloads_that_came_due(context);
}

} // namespace server

// =============================================================================
// Built-in Trigger Actions
// =============================================================================
// See trigger_actions.hpp for what this replaced (a string-keyed, static-init
// registry fed by an X-macro of names) and why the closed enum is the better
// shape. Each action below reads its configuration from the trigger's typed
// param slots (param_float, param_string, param_target_name) and reaches into
// the world through the server context.
// =============================================================================

#include "../shared/entities/entity_reflection.hpp"
#include "damage.hpp"
#include "trigger_actions.hpp"

#include "../shared/entity_system.hpp"
#include "../shared/game_session.hpp"
#include "../shared/log.hpp"

#include <algorithm>
#include <string>

namespace
{

// `kill` routes through the centralized damage helper so PLAYER_DIED and the
// respawn timer fire exactly as they do for any other death source. Massive
// damage amount + zero attacker/inflictor = world kill, suicide convention.
void action_kill(server::server_context_t &context,
                 entities::Trigger_Volume_Entity & /*trigger*/,
                 entities::Player_Entity &player)
{
  server::damage_info_t info{};
  info.victim_uid      = player.entity_id;
  info.attacker_uid    = 0; // world kill — kill feed renders as suicide/world
  info.inflictor_uid   = 0;
  info.amount          = 9999.f;
  info.knockback_force = 0.f;
  info.type            = server::damage_type_t::GENERIC;
  server::inflict_damage(context, info);
}

// `set_health` is healing-only by design: max(current, requested). Killing a
// player from a trigger goes through `kill`, which routes through the damage
// helper and thereby fires PLAYER_DIED + schedules respawn. If `set_health`
// were allowed to set health to 0, it would silently bypass that whole dance.
void action_set_health(server::server_context_t & /*context*/,
                       entities::Trigger_Volume_Entity &trigger,
                       entities::Player_Entity &player)
{
  const int32_t requested = static_cast<int32_t>(trigger.param_float);
  player.health = std::max(player.health, requested);
}

void action_print_message(server::server_context_t & /*context*/,
                          entities::Trigger_Volume_Entity &trigger,
                          entities::Player_Entity &player)
{
  log_terminal("trigger fired by player {}: {}", player.entity_id,
               trigger.param_string.c_str());
}

// Warps the player to a Player_Spawn_Entity's position. If the trigger's
// param_target_name is non-empty, pick the spawn with a matching entity_id
// (stringified); otherwise just use the first spawn we find.
void action_warp_to_spawn(server::server_context_t &context,
                          entities::Trigger_Volume_Entity &trigger,
                          entities::Player_Entity &player)
{
  log_terminal("trigger fired by player {}: warping to spawn '{}'",
               player.entity_id, trigger.param_target_name.c_str());
  Span<entities::Player_Spawn_Entity> spawns =
      context.session.entity_system.entities_of<entities::Player_Spawn_Entity>();
  if (spawns.empty())
  {
    log_error("warp_to_spawn: no Player_Spawn_Entity in session, cannot warp "
              "player {}",
              player.entity_id);
    return;
  }

  const entities::Player_Spawn_Entity *target = nullptr;
  const char *requested = trigger.param_target_name.c_str();
  if (trigger.param_target_name.length > 0)
  {
    for (const entities::Player_Spawn_Entity &spawn : spawns)
    {
      if (std::to_string(spawn.entity_id) == requested)
      {
        target = &spawn;
        break;
      }
    }
    if (!target)
    {
      log_error("warp_to_spawn: param_target_name '{}' did not match any "
                "Player_Spawn_Entity entity_id; falling back to first spawn",
                requested);
    }
  }
  if (!target)
    target = &spawns[0];

  log_terminal("warp_to_spawn: warping player {} to spawn {} at position "
               "({}, {}, {})",
               player.entity_id, target->entity_id, target->position.x,
               target->position.y, target->position.z);
  player.position = target->position;
}

} // namespace

namespace server
{

void fire_trigger_action(server_context_t &context,
                         entities::Trigger_Volume_Entity &trigger,
                         entities::Player_Entity &player)
{
  switch (trigger.action)
  {
    case entities::Trigger_Action::Kill:
      action_kill(context, trigger, player);
      return;
    case entities::Trigger_Action::Set_Health:
      action_set_health(context, trigger, player);
      return;
    case entities::Trigger_Action::Print_Message:
      action_print_message(context, trigger, player);
      return;
    case entities::Trigger_Action::Warp_To_Spawn:
      action_warp_to_spawn(context, trigger, player);
      return;
  }

  log_error("fire_trigger_action: trigger {} carries Trigger_Action value {}, which is not "
            "in the enum — no action taken",
            trigger.entity_id, (int)trigger.action);
}

} // namespace server

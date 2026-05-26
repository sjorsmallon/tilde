// =============================================================================
// Built-in Trigger Actions
// =============================================================================
// Each function below is a leaf action registered into Trigger_Action_Registry
// via a static-init helper at the bottom of the file. To add a new action:
//
//   1. Add one line to TRIGGER_ACTION_LIST in
//      src/shared/trigger_action_list.hpp.
//   2. Write a function with signature
//        void action_<name>(server_context_t&,
//                           network::Trigger_Volume_Entity&,
//                           network::Player_Entity&);
//   3. Register it at the bottom of this file via
//      Trigger_Action_Registration.
//
// Actions read configuration from the trigger's typed param slots (param_float,
// param_string, param_target_name) and reach into the world via the server
// context (entity_system queries, physics, the event queue, inflict_damage).
//
// Lives in `src/server/` (not `src/shared/`) because actions legitimately
// touch server-only state — inflict_damage, physics, the gameplay event
// queue. The editor inspector dropdown reads names from the X-macro in
// src/shared/trigger_action_list.hpp instead of from this file's registry,
// so the client never needs to see server_context_t.
// =============================================================================

#include "damage.hpp"
#include "trigger_action_registry.hpp"

#include "../shared/entities/entity_list.hpp"
#include "../shared/entities/player_entity.hpp"
#include "../shared/entities/trigger_volume_entity.hpp"
#include "../shared/entity_system.hpp"
#include "../shared/game_session.hpp"
#include "../shared/log.hpp"
#include "../shared/trigger_action_list.hpp"

#include <algorithm>
#include <string>

namespace
{

// `kill` routes through the centralized damage helper so PLAYER_DIED and the
// respawn timer fire exactly as they do for any other death source. Massive
// damage amount + zero attacker/inflictor = world kill, suicide convention.
void action_kill(server::server_context_t &context,
                 network::Trigger_Volume_Entity & /*trigger*/,
                 network::Player_Entity &player)
{
  server::damage_info_t info{};
  info.victim_uid      = static_cast<shared::entity_uid_t>(player.entity_id);
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
                       network::Trigger_Volume_Entity &trigger,
                       network::Player_Entity &player)
{
  const network::int32 requested = static_cast<network::int32>(trigger.param_float);
  player.health = std::max(player.health, requested);
}

void action_print_message(server::server_context_t & /*context*/,
                          network::Trigger_Volume_Entity &trigger,
                          network::Player_Entity &player)
{
  log_terminal("trigger fired by player {}: {}", player.entity_id,
               trigger.param_string.c_str());
}

// Warps the player to a Player_Spawn_Entity's position. If the trigger's
// param_target_name is non-empty, pick the spawn with a matching entity_id
// (stringified); otherwise just use the first spawn we find.
void action_warp_to_spawn(server::server_context_t &context,
                          network::Trigger_Volume_Entity &trigger,
                          network::Player_Entity &player)
{
  log_terminal("trigger fired by player {}: warping to spawn '{}'",
               player.entity_id, trigger.param_target_name.c_str());
  auto *spawns = context.session.entity_system
                     .get_entities<network::Player_Spawn_Entity>(
                         entity_type::PLAYER_SPAWN);
  if (!spawns || spawns->empty())
  {
    log_error("warp_to_spawn: no Player_Spawn_Entity in session, cannot warp "
              "player {}",
              player.entity_id);
    return;
  }

  const network::Player_Spawn_Entity *target = nullptr;
  const char *requested = trigger.param_target_name.c_str();
  if (trigger.param_target_name.length > 0)
  {
    for (const auto &spawn : *spawns)
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
    target = &(*spawns)[0];

  log_terminal("warp_to_spawn: warping player {} to spawn {} at position "
               "({}, {}, {})",
               player.entity_id, target->entity_id, target->position.x,
               target->position.y, target->position.z);
  player.position = target->position;
}

// Static-init registrations. The TRIGGER_ACTION_LIST X-macro lives in
// src/shared/trigger_action_list.hpp; we expand each entry into a
// Trigger_Action_Registration object so the linker keeps this TU even when
// no code outside it references the symbols by name. Names here MUST stay in
// sync with the X-macro — that's the entire reason both sides read from the
// same list.
#define X(symbol, string_name) \
  static server::Trigger_Action_Registration s_##symbol{string_name, &action_##symbol};
TRIGGER_ACTION_LIST
#undef X

} // namespace

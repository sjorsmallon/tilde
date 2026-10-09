#pragma once

#include "entities/generated/entities/player_entity_generated.hpp"
#include "entity_system.hpp"

// The Merge weapon: two players in ONE body. The DRIVER (who fired) walks it and the PASSENGER (who
// was hit) aims it and fires from it. The relation is stored once, on the passenger, as a
// Movement_Override: `Merged` with override_target_uid naming the driver. The driver's half is asked.
//
// The driver steers along the passenger's aim as the driver's own client was SHOWN it: that aim
// rides the driver's input like any other view, so the driver's prediction stays exact and
// player_move never learns the merge exists.
namespace shared
{

[[nodiscard]] inline bool player_is_merged_passenger(const entities::Player_Entity& player)
{
  return player.movement.active_override == entities::Movement_Override::Merged;
}

[[nodiscard]] inline const entities::Player_Entity*
try_find_passenger_by_driver_uid(const Entity_System& system, entity_uid_t driver_uid)
{
  if (driver_uid == null_entity_uid)
    return nullptr;
  for (const entities::Player_Entity& player : system.entities_of_type<entities::Player_Entity>())
    if (player_is_merged_passenger(player) && player.movement.override_target_uid == driver_uid &&
        player.health.current_health > 0)
      return &player;
  return nullptr;
}

// The body a player's shots leave from and must pass through: the driver's while merged, else their own.
[[nodiscard]] inline entity_uid_t get_body_uid_for_player_uid(const Entity_System& system,
                                                              entity_uid_t player_uid)
{
  const entities::Player_Entity* player = system.get<entities::Player_Entity>(player_uid);
  if (player != nullptr && player_is_merged_passenger(*player))
    return player->movement.override_target_uid;
  return player_uid;
}

} // namespace shared

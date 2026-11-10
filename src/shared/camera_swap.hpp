#pragma once

#include "entities/generated/entities/player_entity_generated.hpp"
#include "entity_system.hpp"

// The Camera Swapper weapon: two players keep their own bodies and trade eyes and mouses, crossed once.
// Each body is walked by its owner's keys; its head is turned by the OTHER's mouse, and that other looks
// out of it. So A sits in B's head, turning it with A's own mouse, while A's keys walk A's body unseen.
// The relation is stored on the body whose view is steered: view_steered_by_uid names the player whose
// hand turns it. The hand's half is asked (try_find_player_steered_by_hand_uid).
//
// The server resolves a steered body's steps under the hand's latest view instead of the body's own
// input; the owner's input keeps carrying their mouse untouched, because that mouse is turning the
// other body. The owner's client predicts its own body's walk along the body's replicated head, a few
// ticks behind the hand, and nobody looks out of that head, so the lag lands where no hand waits on it.
// The camera is the OTHER body's interpolated eye turned by the local mouse, so the look is as immediate
// as unswapped play. player_move never learns the swap exists.
namespace shared
{

[[nodiscard]] inline bool player_view_is_steered_by_another(const entities::Player_Entity& player)
{
  return player.view_steered_by_uid != null_entity_uid;
}

// The body this player's mouse turns, if any: the one naming them as its hand.
[[nodiscard]] inline const entities::Player_Entity*
try_find_player_steered_by_hand_uid(const Entity_System& system, entity_uid_t hand_uid)
{
  if (hand_uid == null_entity_uid)
    return nullptr;
  for (const entities::Player_Entity& player : system.entities_of_type<entities::Player_Entity>())
    if (player.view_steered_by_uid == hand_uid)
      return &player;
  return nullptr;
}

} // namespace shared

#pragma once

// A weapon the grill took out of a hand, left where it was dropped and fizzling away: how far gone it is at a
// tick and when it is gone, from its spawn stamp and lifetime alone. The server reaps and the client dissolves
// through the same two answers.

#include "entities/generated/entities/emancipated_weapon_entity_generated.hpp"

#include <cstdint>

namespace shared
{

struct emancipated_weapon_state_t
{
  // Since it was left; never negative.
  float elapsed_seconds = 0.f;
  // 0 fresh, 1 gone.
  float gone = 0.f;
  bool  has_vanished = false;
};

[[nodiscard]] bool emancipated_weapon_has_vanished_at_tick(const entities::Emancipated_Weapon_Entity& fizzled,
                                                           uint32_t tick, uint32_t tickrate_hz);

[[nodiscard]] emancipated_weapon_state_t emancipated_weapon_state_at_tick(
    const entities::Emancipated_Weapon_Entity& fizzled, uint32_t tick, float tick_fraction, uint32_t tickrate_hz);

} // namespace shared

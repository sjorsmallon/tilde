#include "emancipated_weapon.hpp"

#include "tick_clock.hpp"

#include <algorithm>

namespace shared
{

bool emancipated_weapon_has_vanished_at_tick(const entities::Emancipated_Weapon_Entity& fizzled, uint32_t tick,
                                             uint32_t tickrate_hz)
{
  return tick >= fizzled.spawned_tick + ticks_from_seconds(fizzled.lifetime_seconds, tickrate_hz);
}

emancipated_weapon_state_t emancipated_weapon_state_at_tick(const entities::Emancipated_Weapon_Entity& fizzled,
                                                            uint32_t tick, float tick_fraction, uint32_t tickrate_hz)
{
  emancipated_weapon_state_t state;
  state.elapsed_seconds = std::max(0.f, seconds_since_tick(fizzled.spawned_tick, tick, tick_fraction, tickrate_hz));
  state.gone            = fizzled.lifetime_seconds > 0.f
                              ? std::clamp(state.elapsed_seconds / fizzled.lifetime_seconds, 0.f, 1.f)
                              : 1.f;
  state.has_vanished    = emancipated_weapon_has_vanished_at_tick(fizzled, tick, tickrate_hz);
  return state;
}

} // namespace shared

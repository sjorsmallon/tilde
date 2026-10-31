#pragma once

// A bubble's life as a function of the tick and its replicated state alone: where it is (its fixed arc), when it
// bursts (the tick its rest runs out, or the pop a bounce stamped), how far it has swollen and peeled, and when it
// is gone. The server's system pops and reaps through these, the movement volume places through these, and the
// client draws through these, so there is one spelling of each and `position` is where it was spawned, written
// once.

#include "collision/fixed_arc_flight.hpp"
#include "entities/generated/entities/bubble_entity_generated.hpp"

#include <cstdint>

namespace shared
{

// Its centre at `tick`; `tick_fraction` of the way to the next tick for the draw.
[[nodiscard]] linalg::vec3f bubble_position_at_tick(const entities::Bubble_Entity& bubble, uint32_t tick,
                                                    const fixed_arc_flight_settings_t& settings);
[[nodiscard]] linalg::vec3f bubble_position_at_tick(const entities::Bubble_Entity& bubble, uint32_t tick,
                                                    float tick_fraction, const fixed_arc_flight_settings_t& settings);

// The tick a whole bubble pops on its own: landed plus its rest. 0 until the launch is latched.
[[nodiscard]] uint32_t bubble_timed_pop_tick(const entities::Bubble_Entity& bubble, uint32_t tickrate_hz);

// Popped and lingered out: the tick the server reaps it.
[[nodiscard]] bool bubble_has_expired_at_tick(const entities::Bubble_Entity& bubble, uint32_t tick,
                                              uint32_t tickrate_hz);

struct bubble_state_t
{
  // Positive while whole, negative once burst. A timed pop bursts the tick it is stamped and the swell runs up to
  // it, which the client sees coming; a bounce pop bursts swell_seconds after its stamp, on notice.
  float seconds_to_burst = 0.f;
  // 0 whole, 1 at the burst.
  float swell = 0.f;
  // 0 until the burst, 1 once the peel is through and the bubble is gone.
  float peeled = 0.f;
  bool  has_burst    = false;
  bool  has_vanished = false;
};

[[nodiscard]] bubble_state_t bubble_state_at_tick(const entities::Bubble_Entity& bubble, uint32_t tick,
                                                  float tick_fraction, uint32_t tickrate_hz);

} // namespace shared

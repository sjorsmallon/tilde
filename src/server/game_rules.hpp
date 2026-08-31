#pragma once

#include "../shared/events/generated/events_generated.hpp"
#include "game_mode.hpp"

#include <cstdint>

namespace server
{

struct game_rules_state_t
{
  shared::Round_Phase phase = shared::Round_Phase::Warmup;
  uint32_t phase_end_tick = 0;
  uint32_t round_number = 0;
  Game_Mode mode = Game_Mode::deathmatch;
  bool map_restart_requested = false;
  bool objective_reached = false;
};

} // namespace server

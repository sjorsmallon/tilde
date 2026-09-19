#include "../../shared/entities/generated/traits/timer_generated.hpp"
#include "../../shared/subtick.hpp"
#include "../entity_io_context.hpp"
#include "../server_context.hpp"

#include <algorithm>

namespace
{

void arm(entities::Timer_State& timer_state, const server::input_context_t& context)
{
  timer_state.running = true;
  timer_state.paused_remaining_ticks = 0;
  timer_state.deadline_tick =
      context.tick + shared::ticks_from_seconds(timer_state.duration_seconds,
                                                context.server.cvars->sv_tickrate);
}

} // namespace

namespace entities
{

// Idempotent on a running timer, so a trigger re-entered five times does not
// push the deadline out five times. Restart is the verb that does.
void start(Entity&, Timer_State& timer_state, const Start_Data&, input_context_t& context)
{
  if (timer_state.running || timer_state.paused_remaining_ticks > 0)
    return;
  arm(timer_state, context);
}

void stop(Entity&, Timer_State& timer_state, const Stop_Data&, input_context_t&)
{
  timer_state.running = false;
  timer_state.deadline_tick = 0;
  timer_state.paused_remaining_ticks = 0;
}

void pause(Entity&, Timer_State& timer_state, const Pause_Data&, input_context_t& context)
{
  if (!timer_state.running)
    return;
  const uint32_t remaining_ticks =
      timer_state.deadline_tick > context.tick ? timer_state.deadline_tick - context.tick : 0;
  timer_state.paused_remaining_ticks = std::max<uint32_t>(remaining_ticks, 1);
  timer_state.running = false;
  timer_state.deadline_tick = 0;
}

void resume(Entity&, Timer_State& timer_state, const Resume_Data&, input_context_t& context)
{
  if (timer_state.paused_remaining_ticks == 0)
    return;
  timer_state.running = true;
  timer_state.deadline_tick = context.tick + timer_state.paused_remaining_ticks;
  timer_state.paused_remaining_ticks = 0;
}

void restart(Entity&, Timer_State& timer_state, const Restart_Data&, input_context_t& context)
{
  arm(timer_state, context);
}

} // namespace entities

#include "systems/timer_system.hpp"

#include "../shared/entities/generated/traits/timer_generated.hpp"
#include "../shared/entity_system.hpp"
#include "../shared/game_session.hpp"
#include "../shared/subtick.hpp"
#include "entity_io_context.hpp"
#include "server_context.hpp"

namespace server
{

void update_timers(server_context_t& context)
{
  for (auto [timer, timer_state] :
       context.world.session.entity_system.entities_with_trait<entities::Timer>())
  {
    if (!timer_state.running || context.tick_number < timer_state.deadline_tick)
      continue;

    if (timer_state.repeat)
    {
      timer_state.deadline_tick +=
          shared::ticks_from_seconds(timer_state.duration_seconds, context.cvars->sv_tickrate);
    }
    else
    {
      timer_state.running = false;
      timer_state.deadline_tick = 0;
    }

    input_context_t emit_context{context, shared::null_entity_uid, context.tick_number};
    entities::emit_elapsed(timer, entities::Elapsed_Data{}, emit_context);
  }
}

} // namespace server

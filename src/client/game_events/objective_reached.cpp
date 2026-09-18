#include "../../shared/assets/generated/assets_generated.hpp"
#include "../../shared/run_times.hpp"
#include "../audio/audio_system.hpp"
#include "../client_context.hpp"
#include "../event_handlers.hpp"
#include "../ghost_playback.hpp"
#include "../hud/announcement.hpp"

#include <format>

namespace client::game_events
{

void on_objective_reached(client_context_t &context, const shared::Objective_Reached &value)
{
  const float tick_dt = 1.0f / static_cast<float>(context.connection.server_tickrate);
  const bool  timed   = value.attempt_ticks > 0;
  // No best on file is a record too: the first finish is the fastest one.
  const bool is_record = timed && (value.best_ticks == 0 || value.attempt_ticks < value.best_ticks);

  std::string text = "LEVEL COMPLETE";
  if (timed)
  {
    text += "\n" + shared::format_run_time(static_cast<float>(value.attempt_ticks) * tick_dt);
    if (value.best_ticks == 0)
      text += "\nNEW RECORD";
    else
    {
      const std::string best =
          shared::format_run_time(static_cast<float>(value.best_ticks) * tick_dt);
      text += is_record ? std::format("\nNEW RECORD (was {})", best) : std::format("\nBEST {}", best);
    }
  }
  hud::set_announcement(text);

  if (timed)
    reload_map_ghost(context);

  if (context.audio)
    context.audio->play_2d(is_record ? assets::sound_asset::a_new_record
                                     : assets::sound_asset::wow_incredible,
                           1.0f);
}

} // namespace client::game_events

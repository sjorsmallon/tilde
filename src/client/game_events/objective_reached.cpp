#include "../../shared/assets/generated/assets_generated.hpp"
#include "../../shared/run_times.hpp"
#include "../audio/audio_system.hpp"
#include "../client_context.hpp"
#include "../event_handlers.hpp"
#include "../hud/announcement.hpp"

#include <format>

namespace client::game_events
{

void on_objective_reached(client_context_t &context, const shared::Objective_Reached &value)
{
  std::string text = "LEVEL COMPLETE";
  if (value.attempt_ticks > 0)
  {
    const float tick_dt = 1.0f / static_cast<float>(context.connection.server_tickrate);
    text += "\n" + shared::format_run_time(static_cast<float>(value.attempt_ticks) * tick_dt);
    if (value.best_ticks == 0 || value.attempt_ticks < value.best_ticks)
      text += "\nNEW BEST";
  }
  hud::set_run_result(text);
  hud::set_announcement(text);

  if (context.audio)
    context.audio->play_2d(assets::sound_asset::wow_incredible, 1.0f);
}

} // namespace client::game_events

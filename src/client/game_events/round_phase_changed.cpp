#include "../event_handlers.hpp"
#include "../hud/announcement.hpp"

#include <format>

namespace client::game_events
{

namespace
{

[[nodiscard]] std::string announcement_for(const shared::Round_Phase_Changed &value)
{
  switch (value.phase)
  {
  case shared::Round_Phase::Warmup:
    return {};
  case shared::Round_Phase::Countdown:
    return std::format("ROUND {}", value.round_number);
  case shared::Round_Phase::Live:
    return "FIGHT";
  case shared::Round_Phase::Round_End:
    return "ROUND OVER";
  case shared::Round_Phase::Game_Over:
    return "GAME OVER";
  }
  return {};
}

} // namespace

void on_round_phase_changed(client_context_t &context, const shared::Round_Phase_Changed &value)
{
  (void)context;

  const std::string text = announcement_for(value);
  if (!text.empty())
    hud::set_announcement(text);

  // future consumers: round_timer::on_round_phase_changed(...) -- value carries
  //                   phase_end_tick so a countdown needs no per-tick traffic
  //                   scoreboard::on_round_phase_changed(...);
}

} // namespace client::game_events

#include "../../shared/assets/generated/assets_generated.hpp"
#include "../audio/audio_system.hpp"
#include "../client_context.hpp"
#include "../event_handlers.hpp"
#include "../hud/announcement.hpp"

namespace client::game_events
{

void on_objective_reached(client_context_t &context, const shared::Objective_Reached &value)
{
  (void)value;
  hud::set_announcement("LEVEL COMPLETE");

  if (context.audio)
    context.audio->play_2d(assets::sound_asset::wow_incredible, 1.0f);
}

} // namespace client::game_events

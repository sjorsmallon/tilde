#include "../../shared/effects/generated/effects_generated.hpp"
#include "../audio/audio_system.hpp"
#include "../client_context.hpp"

namespace client::effects
{

void on_ping(client_context_t& context, const shared::Ping& data)
{
  // Positional for everyone, the pinger included: a ping is a claim about a
  // PLACE, so hearing it from that place is the whole point, and there is no
  // predicted copy here to double up with the way a jump pad launch has.
  if (context.audio)
    context.audio->play_3d(assets::sound_asset::ui_ping, data.origin);
}

} // namespace client::effects

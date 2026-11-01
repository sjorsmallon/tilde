#include "../message_handlers.hpp"
#include "../audio/audio_system.hpp"

namespace client::messages
{

void on_ping(client_context_t& context, const shared::Ping& data)
{
  // Positional for everyone, the pinger included: a ping is a claim about a
  // PLACE, so hearing it from that place is the whole point, and there is no
  // predicted copy here to double up with the way a jump pad launch has.
  context.audio.play_3d(assets::sound_id("ui_ping"), data.site.origin);
}

} // namespace client::messages

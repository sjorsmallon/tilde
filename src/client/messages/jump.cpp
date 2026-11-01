#include "../message_handlers.hpp"
#include "../audio/audio_system.hpp"

namespace client::messages
{

void on_jump(client_context_t &context, const shared::Jump &data)
{
  if (data.site.attached_entity == context.connection.my_entity_uid)
    return; // our own jump — already played locally
  context.audio.play_3d(assets::sound_id("player_jump"), data.site.origin);
}

} // namespace client::messages

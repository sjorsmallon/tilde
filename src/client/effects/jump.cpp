#include "../../shared/effects/generated/effects_generated.hpp"
#include "../audio/audio_system.hpp"
#include "../client_context.hpp"

namespace client::effects
{

void on_jump(client_context_t &context, const shared::Jump &data)
{
  if (data.attached_entity == context.connection.my_entity_uid)
    return; // our own jump — already played locally
  if (context.audio)
    context.audio->play_3d(assets::sound_asset::player_jump, data.origin);
}

} // namespace client::effects

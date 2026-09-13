#include "../../shared/effects/generated/effects_generated.hpp"
#include "../audio/audio_system.hpp"
#include "../client_context.hpp"

namespace client::effects
{

void on_jump_pad_launch(client_context_t& context, const shared::Jump_Pad_Launch& data)
{
  // Our own launch was PREDICTED and already played, a round trip ago, off the
  // player_move step that applied it. Hearing the server's copy on top of it is
  // the Source "effect fires twice" bug, and this one comparison is that
  // engine's IsFirstTimePredicted (prediction_def.md ss1.7). Everyone else has
  // no step to derive it from, so they play the server's.
  if (data.attached_entity == context.connection.my_entity_uid)
    return;
  if (context.audio)
    context.audio->play_3d(assets::sound_asset::twang, data.origin);
}

} // namespace client::effects

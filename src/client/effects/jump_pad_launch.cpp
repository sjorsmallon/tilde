#include "../../shared/effects/generated/effects_generated.hpp"
#include "../audio/audio_system.hpp"
#include "../client_context.hpp"

namespace client::effects
{

void on_jump_pad_launch(client_context_t& context, const shared::Jump_Pad_Launch& data)
{
  if (context.audio)
    context.audio->play_3d(assets::sound_asset::twang, data.origin);
}

} // namespace client::effects

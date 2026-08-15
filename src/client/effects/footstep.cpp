#include "../../shared/effects/generated/effects_generated.hpp"
#include "../../shared/linalg.hpp"
#include "../audio/audio_system.hpp"
#include "../client_context.hpp"

namespace client::effects
{

  
void on_footstep(client_context_t &context, const shared::Footstep &data)
{
  if (context.audio)
    context.audio->play_3d("resources/sounds/footstep.wav", data.origin, 0.5f);
}

} // namespace client::effects

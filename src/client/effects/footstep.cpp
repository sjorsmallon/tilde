#include "../../shared/cosmetic_events.hpp"
#include "../../shared/linalg.hpp"
#include "../audio/audio_system.hpp"
#include "../client_context.hpp"

namespace client::effects
{

  
void on_footstep(client_context_t &context, const shared::effect_data_t &data)
{
  if (context.audio)
    context.audio->play_3d("resources/sounds/footstep.wav", data.origin, 0.5f);
}

} // namespace client::effects

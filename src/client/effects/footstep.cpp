#include "../../shared/effects/generated/effects_generated.hpp"
#include "../../shared/linalg.hpp"
#include "../audio/audio_system.hpp"
#include "../client_context.hpp"

namespace client::effects
{

  
void on_footstep(client_context_t &context, const shared::Footstep &data)
{
  // Missing because there is no footstep sound on disk: the content gap is
  // named at the call site rather than hidden behind a path that resolves to
  // nothing.
  if (context.audio)
    context.audio->play_3d(assets::sound_asset::Missing, data.origin, 0.5f);
}

} // namespace client::effects

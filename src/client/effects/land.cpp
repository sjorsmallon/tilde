#include "../../shared/effects/generated/effects_generated.hpp"
#include "../../shared/linalg.hpp"
#include "../audio/audio_system.hpp"
#include "../client_context.hpp"

namespace client::effects
{

void on_land(client_context_t &context, const shared::Land &data)
{
  if (data.attached_entity == context.connection.my_entity_uid)
    return; // our own landing — already played locally
  if (!context.audio)
    return;

  // data.scale carries the landing impact speed (units/s). Map it to volume so
  // a heavy drop is louder than a light hop. Reference ~600 ≈ a solid fall.
  float volume = linalg::clamp(data.scale / 600.f, 0.3f, 1.0f);
  context.audio->play_3d("resources/sounds/player_land.wav", data.origin, volume);
}

} // namespace client::effects

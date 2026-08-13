#include "../../shared/cosmetic_events.hpp"
#include "../../shared/linalg.hpp"
#include "../audio/audio_system.hpp"
#include "../client_context.hpp"

namespace client::effects
{

//FIXME(SMIA): this sounds like a footgun waiting to happen, the discontinuity
// between 
void on_jump(client_context_t &context, const shared::effect_data_t &data)
{
  if (data.attached_entity == context.connection.my_entity_uid)
    return; // our own jump — already played locally
  if (context.audio)
    context.audio->play_3d("resources/sounds/player_jump.wav", data.origin);
}

void on_land(client_context_t &context, const shared::effect_data_t &data)
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

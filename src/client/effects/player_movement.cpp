#include "../../shared/cosmetic_events.hpp"
#include "../../shared/linalg.hpp"
#include "../audio/audio_system.hpp"
#include "../client_context.hpp"

namespace client::effects
{

// JUMP / LAND handlers — the spatialized, "other players" half of movement
// sound. The local player plays its own jump/land off prediction in
// PlayState::update (centered, zero-latency); the server broadcasts the same
// events tagged with the originating player's uid. We skip the effect whose
// attached_entity is our own player so we don't double-hear it.

void on_jump(client_context_t &context, const shared::effect_data_t &data)
{
  if (data.attached_entity == context.my_entity_uid)
    return; // our own jump — already played locally
  if (context.audio)
    context.audio->play_3d("resources/sounds/player_jump.wav", data.origin);
}

void on_land(client_context_t &context, const shared::effect_data_t &data)
{
  if (data.attached_entity == context.my_entity_uid)
    return; // our own landing — already played locally
  if (!context.audio)
    return;

  // data.scale carries the landing impact speed (units/s). Map it to volume so
  // a heavy drop is louder than a light hop. Reference ~600 ≈ a solid fall.
  float volume = linalg::clamp(data.scale / 600.f, 0.3f, 1.0f);
  context.audio->play_3d("resources/sounds/player_land.wav", data.origin, volume);
}

} // namespace client::effects

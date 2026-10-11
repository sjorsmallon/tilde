#include "../../shared/effects/generated/effects_generated.hpp"
#include "../../shared/linalg.hpp"
#include "../audio/audio_system.hpp"
#include "../client_context.hpp"
#include "../surface_audio.hpp"

namespace client::effects
{

void on_land(client_context_t &context, const shared::Land &data)
{
  if (data.attached_entity == context.connection.my_entity_uid)
    return; // our own landing was played from the prediction
  const shared::Surface_Kind kind = surface_kind_for_material(context, data.surface_material);
  context.audio.play_3d(land_sound_for(kind), data.origin, land_volume_for(data.scale));
}

} // namespace client::effects

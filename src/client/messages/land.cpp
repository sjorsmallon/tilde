#include "../message_handlers.hpp"
#include "../../shared/linalg.hpp"
#include "../audio/audio_system.hpp"
#include "../surface_audio.hpp"

namespace client::messages
{

void on_land(client_context_t &context, const shared::Land &data)
{
  if (data.site.attached_entity == context.connection.my_entity_uid)
    return; // our own landing was played from the prediction
  const shared::Surface_Kind kind = surface_kind_for_material(context, data.site.surface_material);
  context.audio.play_3d(land_sound_for(kind), data.site.origin, land_volume_for(data.site.scale));
}

} // namespace client::messages

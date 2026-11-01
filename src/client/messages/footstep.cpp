#include "../message_handlers.hpp"
#include "../../shared/linalg.hpp"
#include "../audio/audio_system.hpp"
#include "../surface_audio.hpp"

namespace client::messages
{

void on_footstep(client_context_t &context, const shared::Footstep &data)
{
  if (data.site.attached_entity == context.connection.my_entity_uid)
    return; // our own step was played from the prediction
  const shared::Surface_Kind kind = surface_kind_for_material(context, data.site.surface_material);
  uint32_t& count = context.visuals.footstep_counts[data.site.attached_entity];
  context.audio.play_3d(step_sound_for(kind, count++), data.site.origin,
                        step_volume_for(data.site.scale, context.cvars->pm_maxspeed));
}

} // namespace client::messages

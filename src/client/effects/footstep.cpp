#include "../../shared/effects/generated/effects_generated.hpp"
#include "../../shared/linalg.hpp"
#include "../audio/audio_system.hpp"
#include "../client_context.hpp"
#include "../surface_audio.hpp"

namespace client::effects
{

void on_footstep(client_context_t &context, const shared::Footstep &data)
{
  if (data.attached_entity == context.connection.my_entity_uid)
    return; // our own step was played from the prediction
  const shared::Surface_Kind kind = surface_kind_for_material(context, data.surface_material);
  uint32_t& count = context.visuals.footstep_counts[data.attached_entity];
  context.audio.play_3d(step_sound_for(kind, count++), data.origin,
                        step_volume_for(data.scale, context.cvars->pm_maxspeed));
}

} // namespace client::effects

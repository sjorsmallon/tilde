#include "../../shared/cosmetic_events.hpp"
#include "../../shared/linalg.hpp"
#include "../audio/audio_system.hpp"
#include "../client_context.hpp"

namespace client::effects
{

// FOOTSTEP handler. Purely audible — there is no visual, which is why this
// effect type has existed in effect_type_t since before audio landed. The
// server dispatches one per footfall with:
//   - data.origin: world-space foot position
//   - data.surface_material: surface id (0 = unknown) — reserved for choosing
//     a per-material footstep sound once we have a material->sound table.
//
// For now every footfall plays the same spatialized clip. Once the server
// actually emits FOOTSTEP this fires; until then it is a registered no-op
// (better than an unhandled type, which would assert in dispatch).
void on_footstep(client_context_t &context, const shared::effect_data_t &data)
{
  if (context.audio)
    context.audio->play_3d("resources/sounds/footstep.wav", data.origin, 0.5f);
}

} // namespace client::effects

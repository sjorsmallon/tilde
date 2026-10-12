#include "../../shared/effects/generated/effects_generated.hpp"
#include "../client_context.hpp"

namespace client::effects
{

// For everyone, the pilot included: the ring front moves slowly enough that half a round trip is a few
// units, so nothing is predicted and nothing doubles up.
void on_pilot_impact(client_context_t& context, const shared::Pilot_Impact& data)
{
  context.visuals.wall_ripples.ripples.push_back(
      {.center = data.origin, .normal = data.normal, .age_seconds = 0.f});
}

} // namespace client::effects

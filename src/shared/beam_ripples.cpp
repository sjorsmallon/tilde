#include "entities/generated/entities/spot_light_entity_generated.hpp"
#include "beam_ripples.hpp"

#include "entity_system.hpp"

#include <algorithm>

namespace shared
{

void detect_beam_landings(const Entity_System& system, Span<const beam_stander_t> standers,
                          beam_ripple_state_t& state)
{
  std::vector<beam_ripple_state_t::standing_t> now;

  for (const beam_stander_t& stander : standers)
  {
    if (stander.ground_mover_uid == null_entity_uid)
      continue;
    const entities::Spot_Light_Entity* spot = system.get<entities::Spot_Light_Entity>(stander.ground_mover_uid);
    if (spot == nullptr || !spot->solid_beam)
      continue;

    now.push_back({stander.uid, stander.ground_mover_uid});

    const bool was_standing =
        std::any_of(state.standing.begin(), state.standing.end(),
                    [&](const beam_ripple_state_t::standing_t& previous)
                    { return previous.stander == stander.uid && previous.beam == stander.ground_mover_uid; });
    if (was_standing)
      continue;

    state.ripples.push_back({.center = stander.feet, .beam = stander.ground_mover_uid, .age_seconds = 0.f});
  }

  state.standing = std::move(now);
}

void age_beam_ripples(beam_ripple_state_t& state, float dt)
{
  for (beam_ripple_t& ripple : state.ripples)
    ripple.age_seconds += dt;
  std::erase_if(state.ripples, [](const beam_ripple_t& ripple)
                { return ripple.age_seconds > BEAM_RIPPLE_MAX_AGE_SECONDS; });
}

} // namespace shared

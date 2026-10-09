// The solid beam ripple's detector: a player whose ground mover BECOMES a solid beam is one ripple at their
// feet -- and nothing else is.

#include "entities/generated/entities/mover_entity_generated.hpp"
#include "entities/generated/entities/spot_light_entity_generated.hpp"
#include "beam_ripples.hpp"
#include "entity_system.hpp"

#include <cmath>
#include <cstdio>

static int failure_count = 0;

static void check(bool condition, const char* what)
{
  printf(condition ? "  ok   %s\n" : "  FAIL %s\n", what);
  if (!condition)
    ++failure_count;
}

int main()
{
  printf("[pin] a solid beam ripples once per landing, at the feet, on the beam landed on\n");

  shared::Entity_System      system;
  const shared::entity_uid_t solid_spot = system.spawn(entities::entity_type::Spot_Light_Entity);
  system.get<entities::Spot_Light_Entity>(solid_spot)->solid_beam = true;
  const shared::entity_uid_t light_spot = system.spawn(entities::entity_type::Spot_Light_Entity);
  const shared::entity_uid_t lift       = system.spawn(entities::entity_type::Mover_Entity);

  shared::beam_ripple_state_t state;

  const shared::beam_stander_t in_the_air{.uid = 100, .ground_mover_uid = shared::null_entity_uid, .feet = {0.f, 50.f, 0.f}};
  const shared::beam_stander_t on_the_lift{.uid = 100, .ground_mover_uid = lift, .feet = {0.f, 40.f, 0.f}};
  const shared::beam_stander_t on_the_light{.uid = 100, .ground_mover_uid = light_spot, .feet = {0.f, 40.f, 0.f}};
  const shared::beam_stander_t on_the_beam{.uid = 100, .ground_mover_uid = solid_spot, .feet = {3.f, 40.f, 5.f}};
  const shared::beam_stander_t walked_along{.uid = 100, .ground_mover_uid = solid_spot, .feet = {30.f, 40.f, 5.f}};

  shared::detect_beam_landings(system, {&in_the_air, 1}, state);
  shared::detect_beam_landings(system, {&on_the_lift, 1}, state);
  shared::detect_beam_landings(system, {&on_the_light, 1}, state);
  check(state.ripples.empty(), "the air, a lift and a spot that is not solid make no ripple");

  shared::detect_beam_landings(system, {&on_the_beam, 1}, state);
  check(state.ripples.size() == 1, "landing on a solid beam makes one ripple");
  if (state.ripples.size() == 1)
  {
    check(linalg::length(state.ripples[0].center - on_the_beam.feet) < 0.01f, "the ripple's centre is the feet");
    check(state.ripples[0].beam == solid_spot, "the ripple names the beam landed on");
  }

  shared::detect_beam_landings(system, {&walked_along, 1}, state);
  check(state.ripples.size() == 1, "still standing on it the next frame is not a second landing");

  shared::detect_beam_landings(system, {&in_the_air, 1}, state);
  shared::detect_beam_landings(system, {&on_the_beam, 1}, state);
  check(state.ripples.size() == 2, "jumping and landing again is a second landing");

  shared::age_beam_ripples(state, shared::BEAM_RIPPLE_MAX_AGE_SECONDS * 0.5f);
  check(state.ripples.size() == 2 &&
            std::fabs(state.ripples[0].age_seconds - shared::BEAM_RIPPLE_MAX_AGE_SECONDS * 0.5f) < 1e-5f,
        "ageing advances every ripple");
  shared::age_beam_ripples(state, shared::BEAM_RIPPLE_MAX_AGE_SECONDS);
  check(state.ripples.empty(), "a ripple past its lifetime is dropped");

  printf(failure_count == 0 ? "\nALL PASSED\n" : "\n%d FAILED\n", failure_count);
  return failure_count == 0 ? 0 : 1;
}

// The team wall ripple's detector: a crosser inside a wall it PASSES, and not
// inside it last frame, is one impact on the face it came through -- and
// nothing else is.

#include "entities/generated/entities_generated.hpp"
#include "entity_system.hpp"
#include "map_geometry.hpp"
#include "team_wall_ripples.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

static int failure_count = 0;

static void check(bool condition, const char* what)
{
  printf(condition ? "  ok   %s\n" : "  FAIL %s\n", what);
  if (!condition)
    ++failure_count;
}

static bool near(const linalg::vec3f& a, const linalg::vec3f& b)
{
  return linalg::length(a - b) < 0.01f;
}

int main()
{
  printf("[pin] a team wall ripples once per entry, on the face entered, for the team that passes\n");

  shared::Entity_System      system;
  const shared::entity_uid_t owner = system.spawn(entities::entity_type::Geometry_Owner_Entity);
  system.get<entities::Geometry_Owner_Entity>(owner)->passable_by = entities::Team_Allegiance::Red;

  // A wall 32 thick along x, its -x face at x = -16.
  std::vector<shared::map_geometry_t> geometry;
  geometry.push_back({.uid = 7, .value = shared::make_box_brush({0.f, 0.f, 0.f}, {16.f, 64.f, 64.f})});
  const shared::entity_uid_t owner_of[] = {owner};

  shared::wall_ripple_state_t state;

  const shared::wall_crosser_t red_entering{.uid = 100, .team = entities::Team_Allegiance::Red,
                                            .center = {-12.f, 10.f, 5.f}};
  const shared::wall_crosser_t blu_entering{.uid = 101, .team = entities::Team_Allegiance::Blu,
                                            .center = {-12.f, 10.f, 5.f}};
  const shared::wall_crosser_t red_outside{.uid = 100, .team = entities::Team_Allegiance::Red,
                                           .center = {-40.f, 10.f, 5.f}};

  shared::detect_team_wall_crossings(system, geometry, owner_of, {&blu_entering, 1}, state);
  check(state.ripples.empty(), "the team the wall blocks makes no ripple");

  shared::detect_team_wall_crossings(system, geometry, owner_of, {&red_entering, 1}, state);
  check(state.ripples.size() == 1, "the team the wall passes makes one ripple on entry");
  if (state.ripples.size() == 1)
  {
    check(near(state.ripples[0].normal, {-1.f, 0.f, 0.f}), "the ripple's plane is the face entered");
    check(near(state.ripples[0].center, {-16.f, 10.f, 5.f}),
          "the ripple's centre is the crossing point on that face");
  }

  shared::detect_team_wall_crossings(system, geometry, owner_of, {&red_entering, 1}, state);
  check(state.ripples.size() == 1, "still inside the next frame is not a second entry");

  shared::detect_team_wall_crossings(system, geometry, owner_of, {&red_outside, 1}, state);
  shared::detect_team_wall_crossings(system, geometry, owner_of, {&red_entering, 1}, state);
  check(state.ripples.size() == 2, "leaving and re-entering is a second entry");

  system.get<entities::Geometry_Owner_Entity>(owner)->switch_state.value = false;
  shared::detect_team_wall_crossings(system, geometry, owner_of, {&red_outside, 1}, state);
  shared::detect_team_wall_crossings(system, geometry, owner_of, {&red_entering, 1}, state);
  check(state.ripples.size() == 2, "a switched-off wall is not there to ripple");
  system.get<entities::Geometry_Owner_Entity>(owner)->switch_state.value = true;

  shared::age_wall_ripples(state, shared::RIPPLE_MAX_AGE_SECONDS * 0.5f);
  check(state.ripples.size() == 2 && std::fabs(state.ripples[0].age_seconds - shared::RIPPLE_MAX_AGE_SECONDS * 0.5f) < 1e-5f,
        "ageing advances every ripple");
  shared::age_wall_ripples(state, shared::RIPPLE_MAX_AGE_SECONDS);
  check(state.ripples.empty(), "a ripple past its lifetime is dropped");

  printf(failure_count == 0 ? "\nALL PASSED\n" : "\n%d FAILED\n", failure_count);
  return failure_count == 0 ? 0 : 1;
}

// The pin that ties collect_movement_volumes' switch to the @predicted flag.
//
// The switch cannot be generated -- flattening a jump pad into a launch
// velocity is per-type logic -- so what stops it drifting from the .def is this
// file: spawn one of EVERY entity type, collect, and assert that a volume comes
// out for exactly the types entity_type_is_predicted names. A @predicted type
// with no arm fails here, and so does an arm on a type nobody marked.
//
// -Werror=switch already catches a type ADDED with no arm at all. What it
// cannot see is an arm that falls through to `break` under a flag that says it
// should not, which is the failure this measures.
#include "entities/generated/entities_generated.hpp"
#include "entity_system.hpp"
#include "movement_volumes.hpp"
#include "shapes.hpp"

#include <cstdio>
#include <set>
#include <vector>

static int failure_count = 0;

static void check(bool condition, const char* what)
{
  printf(condition ? "  ok   %s\n" : "  FAIL %s\n", what);
  if (!condition)
    ++failure_count;
}

static void test_every_predicted_type_produces_a_volume()
{
  printf("\n[pin] a volume comes out for exactly the @predicted types\n");

  shared::Entity_System system;

  // One of each, skipping Invalid -- it names no pool.
  std::vector<entities::entity_type> spawned_types;
  for (uint32_t index = 1; index < entities::ENTITY_TYPE_COUNT; ++index)
  {
    const entities::entity_type type = (entities::entity_type)index;
    (void)system.spawn(type);
    spawned_types.push_back(type);
  }

  std::vector<shared::movement_volume_t> volumes;
  shared::collect_movement_volumes(system, volumes);

  std::set<entities::entity_type> types_that_produced_a_volume;
  for (const shared::movement_volume_t& volume : volumes)
  {
    const entities::Entity* entity = system.try_find(volume.uid);
    check(entity != nullptr, "every volume names an entity that exists");
    if (entity != nullptr)
      types_that_produced_a_volume.insert(entity->type);
  }

  for (entities::entity_type type : spawned_types)
  {
    const bool predicted = entities::entity_type_is_predicted(type);
    const bool produced  = types_that_produced_a_volume.count(type) > 0;

    printf("    %-26s predicted=%s volume=%s\n", entities::entity_info(type).classname,
           predicted ? "yes" : "no ", produced ? "yes" : "no ");

    check(predicted == produced,
          predicted ? "a @predicted type has an arm that produces a volume"
                    : "a type that is not @predicted produces none");
  }

  // Not vacuous: if nothing is @predicted the loop above passes by saying
  // nothing, which is the one way this pin could quietly stop measuring.
  check(!volumes.empty(), "at least one type is @predicted, so the pin has something to check");
}

static void test_a_pad_is_flattened_into_what_the_step_reads()
{
  printf("\n[pin] a pad's volume carries its bounds, its switch and its launch\n");

  shared::Entity_System system;
  const shared::entity_uid_t uid = system.spawn(entities::entity_type::Jump_Pad_Entity);

  entities::Jump_Pad_Entity* pad = system.get<entities::Jump_Pad_Entity>(uid);
  pad->position           = {100.f, 8.f, -40.f};
  pad->volume.position    = {0.f, 4.f, 0.f};
  pad->volume.half_extents= {32.f, 8.f, 32.f};
  pad->launch_speed       = 900.f;
  pad->switch_state.value = true;

  std::vector<shared::movement_volume_t> volumes;
  shared::collect_movement_volumes(system, volumes);
  check(volumes.size() == 1, "one pad, one volume");

  const shared::movement_volume_t& volume = volumes[0];
  const shared::aabb_bounds_t expected = shared::get_bounds(pad->volume, pad->position);

  check(volume.uid == uid, "the volume names the pad, which is what the latch stores");
  check(volume.enabled, "an enabled pad comes out enabled");
  check(volume.bounds.min.x == expected.min.x && volume.bounds.min.y == expected.min.y &&
            volume.bounds.min.z == expected.min.z && volume.bounds.max.x == expected.max.x &&
            volume.bounds.max.y == expected.max.y && volume.bounds.max.z == expected.max.z,
        "the bounds are shared::get_bounds of the same Box_Volume the trigger system tests");

  const linalg::vec3f expected_launch = linalg::forward(pad->orientation) * pad->launch_speed;
  check(volume.launch_velocity.x == expected_launch.x &&
            volume.launch_velocity.y == expected_launch.y &&
            volume.launch_velocity.z == expected_launch.z,
        "the launch velocity is forward(orientation) * launch_speed, flattened once");

  // The switch is replicated state, so it has to survive the flattening rather
  // than being filtered out: the step is what honours it.
  pad->switch_state.value = false;
  shared::collect_movement_volumes(system, volumes);
  check(volumes.size() == 1 && !volumes[0].enabled,
        "a disabled pad is still collected, and says so");
}

int main()
{
  test_every_predicted_type_produces_a_volume();
  test_a_pad_is_flattened_into_what_the_step_reads();

  printf("\nmovement_volumes_test %s (%d)\n", failure_count == 0 ? "PASSED" : "FAILED",
         failure_count);
  return failure_count == 0 ? 0 : 1;
}

// The pin that ties the two @predicted collects' switches to the flag.
//
// Neither switch can be generated -- flattening a jump pad into a launch
// velocity and reading a switch off a brush's owner are both per-type logic --
// so what stops them drifting from the .def is this file: spawn one of EVERY
// entity type, run BOTH collects, and assert that exactly one of them answers
// for exactly the types entity_type_is_predicted names. A @predicted type that
// feeds neither fails here, and so does an arm on a type nobody marked.
//
// -Werror=switch already catches a type ADDED with no arm at all. What it
// cannot see is an arm that falls through to `break` under a flag that says it
// should not, which is the failure this measures.
//
// The two are one pin rather than two files because the question is WHICH of
// them a type feeds, and a per-collect pin cannot ask that: it would pass on a
// type that feeds both, or neither.
#include "disabled_geometry.hpp"
#include "entities/entity_reflection.hpp"
#include "entities/generated/entities_generated.hpp"
#include "entity_system.hpp"
#include "movement_volumes.hpp"
#include "movers.hpp"
#include "shapes.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <vector>

static int failure_count = 0;

static constexpr shared::movement_volume_settings_t SIXTY_HERTZ_AT_TICK_ONE{
    .tick = 1, .tick_interval_seconds = 1.f / 60.f, .gravity = 800.f};

static void check(bool condition, const char* what)
{
  printf(condition ? "  ok   %s\n" : "  FAIL %s\n", what);
  if (!condition)
    ++failure_count;
}

// Switch off whatever an entity carries a Switchable's Enabled for, through
// reflection rather than through a hand table of types: the disabled collect
// only answers for an owner that is OFF, so the pin has to be able to turn any
// candidate off without knowing which ones can be. A type with no such leaf is a
// type that cannot be switched, and no bit for it is the right answer.
static void try_switch_off(shared::Entity_System& system, shared::entity_uid_t uid)
{
  entities::Entity* entity = system.try_find(uid);
  if (entity == nullptr)
    return;

  uint8_t* base = reinterpret_cast<uint8_t*>(entity);
  for (const entities::leaf_field_t& leaf : entities::collect_leaf_fields(entity->type))
  {
    if (leaf.name != "switch_state.value")
      continue;

    const bool off = false;
    std::memcpy(base + leaf.offset, &off, sizeof(off));
    return;
  }
}

static void test_every_predicted_type_feeds_exactly_one_collect()
{
  printf("\n[pin] each @predicted type feeds exactly one of the two collects\n");

  shared::Entity_System system;

  // One of each, skipping Invalid -- it names no pool.
  std::vector<entities::entity_type> spawned_types;
  std::vector<shared::entity_uid_t>  spawned_uids;
  for (uint32_t index = 1; index < entities::ENTITY_TYPE_COUNT; ++index)
  {
    const entities::entity_type type = (entities::entity_type)index;
    spawned_uids.push_back(system.spawn(type));
    spawned_types.push_back(type);
    try_switch_off(system, spawned_uids.back());
  }

  std::vector<shared::movement_volume_t> volumes;
  shared::collect_movement_volumes(system, SIXTY_HERTZ_AT_TICK_ONE, volumes);

  std::set<entities::entity_type> types_that_produced_a_volume;
  for (const shared::movement_volume_t& volume : volumes)
  {
    const entities::Entity* entity = system.try_find(volume.uid);
    check(entity != nullptr, "every volume names an entity that exists");
    if (entity != nullptr)
      types_that_produced_a_volume.insert(entity->type);
  }

  // One "geometry object" per spawned entity, each tied to it: the collect is
  // keyed by geometry index and resolves the owner, so an owner_of naming every
  // candidate in turn is the whole map this pin needs -- no map_t, no BVH.
  shared::disabled_geometry_t disabled;
  shared::collect_disabled_geometry(system, spawned_uids, disabled);

  std::vector<shared::mover_t> movers;
  shared::collect_movers(system, {}, {}, 1, 60.0f, movers);
  std::set<entities::entity_type> types_that_produced_a_mover;
  for (const shared::mover_t& mover : movers)
  {
    const entities::Entity* entity = system.try_find(mover.uid);
    check(entity != nullptr, "every mover names an entity that exists");
    if (entity != nullptr)
      types_that_produced_a_mover.insert(entity->type);
  }
  check(disabled.size() == spawned_uids.size(),
        "the bitset is sized to the geometry list it was cut for");

  for (uint32_t index = 0; index < spawned_types.size(); ++index)
  {
    const entities::entity_type type = spawned_types[index];

    const bool predicted = entities::entity_type_is_predicted(type);
    const bool volume    = types_that_produced_a_volume.count(type) > 0;
    const bool bit       = index < disabled.size() && disabled[index] != 0;
    const bool mover     = types_that_produced_a_mover.count(type) > 0;

    printf("    %-26s predicted=%s volume=%s disabled_bit=%s mover=%s\n",
           entities::entity_info(type).classname, predicted ? "yes" : "no ",
           volume ? "yes" : "no ", bit ? "yes" : "no ", mover ? "yes" : "no ");

    check(predicted == (volume || bit || mover),
          predicted ? "a @predicted type feeds one of the three collects"
                    : "a type that is not @predicted feeds none");
    check((int)volume + (int)bit + (int)mover <= 1, "no type feeds two collects");
  }

  // Which one, for the two that exist -- the half the loop above cannot say,
  // since it only asks that ONE of them answered.
  check(types_that_produced_a_volume.count(entities::entity_type::Jump_Pad_Entity) > 0,
        "a jump pad is a movement volume");
  check(!types_that_produced_a_volume.count(entities::entity_type::Geometry_Owner_Entity),
        "a brush entity is not a movement volume");
  check(types_that_produced_a_mover.count(entities::entity_type::Mover_Entity) > 0,
        "a mover entity is a mover");

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
  shared::collect_movement_volumes(system, SIXTY_HERTZ_AT_TICK_ONE, volumes);
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
  shared::collect_movement_volumes(system, SIXTY_HERTZ_AT_TICK_ONE, volumes);
  check(volumes.size() == 1 && !volumes[0].enabled,
        "a disabled pad is still collected, and says so");
}

static void test_a_switch_reaches_the_geometry_it_owns()
{
  printf("\n[pin] the bit follows the owner's switch, and an untied object has none\n");

  shared::Entity_System system;
  const shared::entity_uid_t owner = system.spawn(entities::entity_type::Geometry_Owner_Entity);

  // Three objects: one untied, two tied to the same owner -- N brushes per
  // entity is what the tie is FOR, so one switch has to reach both.
  const shared::entity_uid_t owner_of[] = {shared::null_entity_uid, owner, owner};

  shared::disabled_geometry_t disabled;
  shared::collect_disabled_geometry(system, owner_of, disabled);
  check(disabled.size() == 3 && !disabled[0] && !disabled[1] && !disabled[2],
        "an enabled owner disables nothing");

  system.get<entities::Geometry_Owner_Entity>(owner)->switch_state.value = false;
  shared::collect_disabled_geometry(system, owner_of, disabled);
  check(disabled.size() == 3 && !disabled[0] && disabled[1] && disabled[2],
        "switching the owner off takes out every object tied to it and nothing else");

  // An owner the system no longer holds sets no bit: the object stays solid,
  // which is the safe direction -- a wall you cannot see is worse than one you
  // can walk through.
  (void)system.destroy(owner);
  shared::collect_disabled_geometry(system, owner_of, disabled);
  check(disabled.size() == 3 && !disabled[0] && !disabled[1] && !disabled[2],
        "a destroyed owner leaves its geometry solid");
}

static void test_a_drawn_mover_carries_its_rider_by_the_same_fraction()
{
  const shared::path_pose_t at_tick   = {.position = {0, 0, 0}};
  const shared::path_pose_t next_tick = {.position = {0, 10, 0},
                                         .orientation = linalg::from_axis_angle({0, 1, 0}, 30.0f)};
  const shared::path_pose_t drawn = shared::blend_path_poses(at_tick, next_tick, 0.5f);
  check(linalg::length(drawn.position - linalg::vec3f{0, 5, 0}) < 1e-4f, "half a tick is half the travel");

  const linalg::vec3f on_the_axis = {0, 3, 0};
  check(linalg::length(shared::carry_point_between_poses(at_tick, drawn, on_the_axis) -
                       linalg::vec3f{0, 8, 0}) < 1e-4f,
        "a rider on the axis rises with the lift");
  check(linalg::length(shared::carry_point_between_poses(at_tick, at_tick, {40, 0, 7}) -
                       linalg::vec3f{40, 0, 7}) < 1e-4f,
        "no fraction carries nothing");

  const linalg::vec3f off_axis = {40, 0, 0};
  const linalg::vec3f carried  = shared::carry_point_between_poses(at_tick, next_tick, off_axis);
  check(std::abs(linalg::length(linalg::vec3f{carried.x, 0, carried.z}) - 40.0f) < 1e-3f,
        "a turning lift swings its rider at the same radius");
}

static void test_a_bubble_is_placed_by_the_tick_it_is_cut_for()
{
  printf("\n[pin] a bubble's volume is a function of the tick, armed late and stopped on time\n");

  shared::Entity_System      system;
  const shared::entity_uid_t uid    = system.spawn(entities::entity_type::Bubble_Entity);
  entities::Bubble_Entity*   bubble = system.get<entities::Bubble_Entity>(uid);

  std::vector<shared::movement_volume_t> volumes;
  shared::collect_movement_volumes(system, SIXTY_HERTZ_AT_TICK_ONE, volumes);
  check(volumes.size() == 1 && !volumes[0].enabled,
        "a bubble whose launch is not latched yet bounces nobody");

  bubble->launch_position     = {0.f, 100.f, 0.f};
  bubble->projectile.velocity = {600.f, 0.f, 0.f};
  bubble->launch_tick         = 10;
  bubble->flight_ticks        = 60;

  const auto volume_at = [&](uint32_t tick)
  {
    shared::movement_volume_settings_t settings = SIXTY_HERTZ_AT_TICK_ONE;
    settings.tick                               = tick;
    shared::collect_movement_volumes(system, settings, volumes);
    return volumes[0];
  };
  const auto center_x = [](const shared::movement_volume_t& volume)
  { return (volume.bounds.min.x + volume.bounds.max.x) * 0.5f; };

  check(volume_at(10).kind == shared::movement_volume_kind_t::Bounce,
        "a bubble is a Bounce, which keeps the horizontal speed");
  check(!volume_at(11).enabled, "it is not armed at the muzzle");
  check(volume_at(10 + 12).enabled, "it is armed once arm_seconds have passed");

  check(std::fabs(center_x(volume_at(10 + 30)) - 300.f) < 0.01f,
        "half a second at 600 units a second is 300 units out");
  check(std::fabs(center_x(volume_at(10 + 60)) - 600.f) < 0.01f &&
            center_x(volume_at(10 + 60)) == center_x(volume_at(10 + 600)),
        "it stops at flight_ticks and stays there");

  const float rise_at_rest =
      (volume_at(10 + 60).bounds.min.y + volume_at(10 + 60).bounds.max.y) * 0.5f - 100.f;
  check(rise_at_rest > 0.f, "the Bubble row's negative gravity scale lifts it");

  check(volume_at(10 + 30).launch_velocity.y == bubble->bounce_speed &&
            volume_at(10 + 30).launch_velocity.x == 0.f,
        "the bounce is the bubble's own upward speed");
}

int main()
{
  test_every_predicted_type_feeds_exactly_one_collect();
  test_a_pad_is_flattened_into_what_the_step_reads();
  test_a_bubble_is_placed_by_the_tick_it_is_cut_for();
  test_a_switch_reaches_the_geometry_it_owns();
  test_a_drawn_mover_carries_its_rider_by_the_same_fraction();

  printf("\nmovement_volumes_test %s (%d)\n", failure_count == 0 ? "PASSED" : "FAILED",
         failure_count);
  return failure_count == 0 ? 0 : 1;
}

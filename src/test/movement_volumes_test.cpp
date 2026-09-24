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
#include "movement_modifiers.hpp"
#include "entities/entity_reflection.hpp"
#include "entities/generated/entities_generated.hpp"
#include "entity_system.hpp"
#include "movement_volumes.hpp"
#include "movers.hpp"
#include "predicted_world.hpp"
#include "shapes.hpp"
#include "canopy.hpp"
#include "statues.hpp"
#include "spawned_platforms.hpp"

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

    // A platform answers only once it has landed, as an owner answers only once it is off.
    if (entities::Platform_Entity* platform = system.get<entities::Platform_Entity>(spawned_uids.back()))
      platform->flight = {.launch_tick = 1, .flight_ticks = 0};
    if (entities::Shrinking_Platform_Entity* platform =
            system.get<entities::Shrinking_Platform_Entity>(spawned_uids.back()))
      platform->flight = {.launch_tick = 1, .flight_ticks = 0};
  }

  // A canopy answers only for a carrier the system holds: the one player spawned above.
  for (entities::Canopy_Entity& canopy : system.entities_of<entities::Canopy_Entity>())
    for (const entities::Player_Entity& player : system.entities_of<entities::Player_Entity>())
      canopy.carrier_uid = player.entity_id;

  // A player answers only while frozen.
  for (entities::Player_Entity& player : system.entities_of<entities::Player_Entity>())
    player.movement.active_override = entities::Movement_Override::Stasis;

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
  shared::collect_disabled_geometry(system, spawned_uids, entities::Team_Allegiance::Free_For_All,
                                    disabled);

  std::vector<shared::mover_t> movers;
  shared::collect_movers(system, {}, {}, 1, 60.0f, movers);
  shared::collect_canopies(system, 1, 0, 1.f / 60.f, movers);
  shared::collect_spawned_platforms(system, 1, {.tick_interval_seconds = 1.f / 60.f, .gravity = 800.f},
                                    movers);
  shared::collect_statues(system, movers);
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

  std::vector<shared::movement_modifier_t> modifiers;
  shared::collect_movement_modifiers(system, 1, {.tick_interval_seconds = 1.f / 60.f, .gravity = 800.f},
                                     modifiers);
  std::set<entities::entity_type> types_that_produced_a_modifier;
  for (const shared::movement_modifier_t& modifier : modifiers)
  {
    const entities::Entity* entity = system.try_find(modifier.uid);
    check(entity != nullptr, "every modifier names an entity that exists");
    if (entity != nullptr)
      types_that_produced_a_modifier.insert(entity->type);
  }

  for (uint32_t index = 0; index < spawned_types.size(); ++index)
  {
    const entities::entity_type type = spawned_types[index];

    const bool predicted = entities::entity_type_is_predicted(type);
    const bool volume    = types_that_produced_a_volume.count(type) > 0;
    const bool bit       = index < disabled.size() && disabled[index] != 0;
    const bool mover     = types_that_produced_a_mover.count(type) > 0;
    const bool modifier  = types_that_produced_a_modifier.count(type) > 0;

    printf("    %-26s predicted=%s volume=%s disabled_bit=%s mover=%s modifier=%s\n",
           entities::entity_info(type).classname, predicted ? "yes" : "no ",
           volume ? "yes" : "no ", bit ? "yes" : "no ", mover ? "yes" : "no ",
           modifier ? "yes" : "no ");

    check(predicted == (volume || bit || mover || modifier),
          predicted ? "a @predicted type feeds one of the four collects"
                    : "a type that is not @predicted feeds none");
    check((int)volume + (int)bit + (int)mover + (int)modifier <= 1, "no type feeds two collects");
  }

  check(types_that_produced_a_modifier.count(entities::entity_type::Movement_Modifier_Entity) > 0,
        "a movement modifier is a modifier");

  // Which one, for the two that exist -- the half the loop above cannot say,
  // since it only asks that ONE of them answered.
  check(types_that_produced_a_volume.count(entities::entity_type::Jump_Pad_Entity) > 0,
        "a jump pad is a movement volume");
  check(!types_that_produced_a_volume.count(entities::entity_type::Geometry_Owner_Entity),
        "a brush entity is not a movement volume");
  check(types_that_produced_a_mover.count(entities::entity_type::Mover_Entity) > 0,
        "a mover entity is a mover");
  check(types_that_produced_a_mover.count(entities::entity_type::Platform_Entity) > 0,
        "a landed platform is a mover");
  check(types_that_produced_a_mover.count(entities::entity_type::Shrinking_Platform_Entity) > 0,
        "a landed shrinking platform is a mover");
  check(types_that_produced_a_mover.count(entities::entity_type::Canopy_Entity) > 0,
        "a canopy with a carrier is a mover");
  check(types_that_produced_a_mover.count(entities::entity_type::Player_Entity) > 0,
        "a frozen player is a mover");

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

static void test_a_modifier_scales_the_settings_of_a_hull_inside_it()
{
  printf("\n[pin] a modifier scales what the step runs under, only inside it and only while on\n");

  shared::Entity_System system;
  const shared::entity_uid_t uid = system.spawn(entities::entity_type::Movement_Modifier_Entity);

  entities::Movement_Modifier_Entity* zone = system.get<entities::Movement_Modifier_Entity>(uid);
  zone->position            = {0.f, 0.f, 0.f};
  zone->volume.half_extents = {64.f, 64.f, 64.f};
  zone->gravity_scale       = -1.f;
  zone->friction_scale      = 0.f;
  zone->run_speed_scale     = 2.f;

  std::vector<shared::movement_modifier_t> modifiers;
  shared::collect_movement_modifiers(system, 1, {.tick_interval_seconds = 1.f / 60.f, .gravity = 800.f},
                                     modifiers);
  check(modifiers.size() == 1, "one zone, one modifier");

  const shared::movement_settings_t base{};
  const shared::aabb_bounds_t       inside{.min = {-16.f, 0.f, -16.f}, .max = {16.f, 72.f, 16.f}};
  const shared::aabb_bounds_t       outside{.min = {500.f, 0.f, 500.f}, .max = {532.f, 72.f, 532.f}};

  const shared::movement_settings_t in_zone =
      shared::modified_movement_settings(base, modifiers, inside);
  check(in_zone.shared.gravity == -base.shared.gravity, "gravity is inverted inside");
  check(in_zone.quake.friction == 0.f, "friction is gone inside");
  check(in_zone.shared.run_speed == 2.f * base.shared.run_speed, "run speed doubles inside");
  check(in_zone.shared.jump_speed == base.shared.jump_speed, "a scale of 1 moves no float");

  const shared::movement_settings_t out_of_zone =
      shared::modified_movement_settings(base, modifiers, outside);
  check(out_of_zone.shared.gravity == base.shared.gravity &&
            out_of_zone.quake.friction == base.quake.friction &&
            out_of_zone.shared.run_speed == base.shared.run_speed,
        "a hull outside the box runs under the settings it came with");

  zone->switch_state.value = false;
  shared::collect_movement_modifiers(system, 1, {.tick_interval_seconds = 1.f / 60.f, .gravity = 800.f},
                                     modifiers);
  const shared::movement_settings_t switched_off =
      shared::modified_movement_settings(base, modifiers, inside);
  check(modifiers.size() == 1 && switched_off.shared.gravity == base.shared.gravity,
        "a switched-off zone is still collected, and scales nothing");
}

static void test_a_timed_modifier_is_live_for_exactly_its_lifetime()
{
  printf("\n[pin] a shot's zone scales from the tick after its stamp until its lifetime runs out\n");

  shared::Entity_System system;
  const shared::entity_uid_t uid = system.spawn(entities::entity_type::Timed_Movement_Modifier_Entity);

  entities::Timed_Movement_Modifier_Entity* zone =
      system.get<entities::Timed_Movement_Modifier_Entity>(uid);
  zone->position         = {0.f, 0.f, 0.f};
  zone->half_extents     = {64.f, 64.f, 64.f};
  zone->gravity_scale    = 0.5f;
  zone->lifetime_seconds = 1.f;

  const float tick_interval = 1.f / 60.f;
  const shared::fixed_arc_flight_settings_t flight{.tick_interval_seconds = tick_interval, .gravity = 800.f};
  const shared::movement_settings_t base{};
  const shared::aabb_bounds_t       inside{.min = {-16.f, 0.f, -16.f}, .max = {16.f, 72.f, 16.f}};

  std::vector<shared::movement_modifier_t> modifiers;
  const auto gravity_at = [&](uint32_t tick)
  {
    shared::collect_movement_modifiers(system, tick, flight, modifiers);
    check(modifiers.size() == 1, "an unstamped or expired zone is still collected, as disabled");
    return shared::modified_movement_settings(base, modifiers, inside).shared.gravity;
  };

  check(gravity_at(100) == base.shared.gravity, "a zone the server has not stamped scales nothing");

  zone->spawned_tick = 100;
  check(gravity_at(100) == base.shared.gravity, "the tick it was stamped in already ran its cut");
  check(gravity_at(101) == 0.5f * base.shared.gravity, "live from the next tick");
  check(gravity_at(160) == 0.5f * base.shared.gravity, "live through the last tick of its lifetime");
  check(gravity_at(161) == base.shared.gravity, "and gone the tick after");
  check(!shared::timed_movement_modifier_is_active_at(*zone, 161, tick_interval) &&
            shared::timed_movement_modifier_is_active_at(*zone, 160, tick_interval),
        "the reap asks the same predicate the cut does");

  // A fired zone's box is wherever its arc puts it at the tick, and it is live in flight.
  zone->projectile.weapon_id = entities::Weapon::Modifier_Gun;
  zone->projectile.trigger   = entities::Fire_Trigger::Secondary;
  zone->projectile.velocity  = {700.f, 0.f, 0.f};
  zone->flight = {.launch_position = {0.f, 0.f, 0.f}, .launch_tick = 100, .flight_ticks = 60};
  shared::collect_movement_modifiers(system, 130, flight, modifiers);
  const shared::aabb_bounds_t at_launch = inside;
  const shared::aabb_bounds_t along_the_arc{.min = {500.f, 0.f, -16.f}, .max = {532.f, 72.f, 16.f}};
  check(shared::modified_movement_settings(base, modifiers, at_launch).shared.gravity == base.shared.gravity,
        "halfway through its flight the zone has left where it was fired from");
  check(shared::modified_movement_settings(base, modifiers, along_the_arc).shared.gravity ==
            0.5f * base.shared.gravity,
        "and is live where its arc has carried it");
}

static void test_a_switch_reaches_the_geometry_it_owns()
{
  printf("\n[pin] the bit follows the owner's switch, and an untied object has none\n");

  shared::Entity_System system;
  const shared::entity_uid_t owner = system.spawn(entities::entity_type::Geometry_Owner_Entity);

  // Three objects: one untied, two tied to the same owner -- N brushes per
  // entity is what the tie is FOR, so one switch has to reach both.
  const shared::entity_uid_t owner_of[] = {shared::null_entity_uid, owner, owner};

  const entities::Team_Allegiance no_team = entities::Team_Allegiance::Free_For_All;

  shared::disabled_geometry_t disabled;
  shared::collect_disabled_geometry(system, owner_of, no_team, disabled);
  check(disabled.size() == 3 && !disabled[0] && !disabled[1] && !disabled[2],
        "an enabled owner disables nothing");

  system.get<entities::Geometry_Owner_Entity>(owner)->switch_state.value = false;
  shared::collect_disabled_geometry(system, owner_of, no_team, disabled);
  check(disabled.size() == 3 && !disabled[0] && disabled[1] && disabled[2],
        "switching the owner off takes out every object tied to it and nothing else");

  shared::collect_hidden_geometry(system, owner_of, disabled);
  check(disabled.size() == 3 && !disabled[0] && disabled[1] && disabled[2],
        "the hidden set follows the switch too");

  // An owner the system no longer holds sets no bit: the object stays solid,
  // which is the safe direction -- a wall you cannot see is worse than one you
  // can walk through.
  (void)system.destroy(owner);
  shared::collect_disabled_geometry(system, owner_of, no_team, disabled);
  check(disabled.size() == 3 && !disabled[0] && !disabled[1] && !disabled[2],
        "a destroyed owner leaves its geometry solid");
}

static void test_a_team_wall_is_not_there_for_its_team_and_visible_to_everyone()
{
  printf("\n[pin] a team wall: passable by one team, solid for the rest, hidden from nobody\n");

  shared::Entity_System system;
  const shared::entity_uid_t owner = system.spawn(entities::entity_type::Geometry_Owner_Entity);
  system.get<entities::Geometry_Owner_Entity>(owner)->passable_by = entities::Team_Allegiance::Red;

  const shared::entity_uid_t owner_of[] = {shared::null_entity_uid, owner};

  shared::disabled_geometry_t disabled;
  shared::collect_disabled_geometry(system, owner_of, entities::Team_Allegiance::Red, disabled);
  check(disabled.size() == 2 && !disabled[0] && disabled[1],
        "the wall is not there for the team it is passable by");

  shared::collect_disabled_geometry(system, owner_of, entities::Team_Allegiance::Blu, disabled);
  check(disabled.size() == 2 && !disabled[0] && !disabled[1], "the wall is solid for the other team");

  shared::collect_disabled_geometry(system, owner_of, entities::Team_Allegiance::Free_For_All,
                                    disabled);
  check(disabled.size() == 2 && !disabled[0] && !disabled[1],
        "a mover with no team passes no team wall");

  // The draw asks a different question: a team wall is in nobody's hidden set.
  shared::collect_hidden_geometry(system, owner_of, disabled);
  check(disabled.size() == 2 && !disabled[0] && !disabled[1], "a team wall is visible to everyone");

  // Off is off for everyone, its team included.
  system.get<entities::Geometry_Owner_Entity>(owner)->switch_state.value = false;
  shared::collect_disabled_geometry(system, owner_of, entities::Team_Allegiance::Blu, disabled);
  check(disabled.size() == 2 && disabled[1], "a switched-off team wall is gone for the other team too");
  shared::collect_hidden_geometry(system, owner_of, disabled);
  check(disabled.size() == 2 && disabled[1], "and hidden, like any switched-off brush");

  // The storage cuts every team's set at once and the view picks one.
  system.get<entities::Geometry_Owner_Entity>(owner)->switch_state.value = true;
  shared::predicted_world_storage_t storage;
  for (uint32_t team = 0; team < storage.disabled_geometry.count; ++team)
    shared::collect_disabled_geometry(system, owner_of, static_cast<entities::Team_Allegiance>(team),
                                      storage.disabled_geometry.values[team]);
  check(shared::predicted_world_of(storage, entities::Team_Allegiance::Red).disabled_geometry[1] != 0 &&
            shared::predicted_world_of(storage, entities::Team_Allegiance::Blu).disabled_geometry[1] == 0,
        "the view is the mover's team's set");
  check(shared::predicted_world_of(storage, static_cast<entities::Team_Allegiance>(200))
                .disabled_geometry[1] == 0,
        "a team off the wire that names no value passes no team wall");
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

  bubble->flight              = {.launch_position = {0.f, 100.f, 0.f}, .launch_tick = 10, .flight_ticks = 60};
  bubble->projectile.velocity = {600.f, 0.f, 0.f};

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

  check(std::fabs(center_x(volume_at(10 + 30)) - 450.f) < 0.01f,
        "half the flight is three quarters of the path, easing out to rest");
  check(center_x(volume_at(10 + 60)) - center_x(volume_at(10 + 59)) <
            center_x(volume_at(10 + 1)) - center_x(volume_at(10 + 0)),
        "it arrives slower than it left");
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

static void test_a_platform_is_solid_from_the_tick_it_lands_until_its_rest_runs_out()
{
  printf("\n[pin] a platform is a ghost in flight, one still box at rest, and gone after\n");

  shared::Entity_System        system;
  const shared::entity_uid_t   uid      = system.spawn(entities::entity_type::Platform_Entity);
  entities::Platform_Entity*   platform = system.get<entities::Platform_Entity>(uid);

  const shared::fixed_arc_flight_settings_t flight{.tick_interval_seconds = 1.f / 60.f, .gravity = 800.f};

  std::vector<shared::mover_t> movers;
  const auto cut_at = [&](uint32_t tick) -> const std::vector<shared::mover_t>&
  {
    movers.clear();
    shared::collect_spawned_platforms(system, tick, flight, movers);
    return movers;
  };

  check(cut_at(1).empty(), "a platform whose launch is not latched yet is not solid");

  platform->flight              = {.launch_position = {0.f, 100.f, 0.f}, .launch_tick = 10, .flight_ticks = 30};
  platform->projectile.velocity = {600.f, 0.f, 0.f};
  platform->solid_seconds       = 2.f;
  platform->half_extents        = {48.f, 4.f, 48.f};

  check(cut_at(10 + 29).empty(), "the last tick of the flight is still a ghost");
  check(cut_at(10 + 30).size() == 1, "the tick it lands is the tick it is solid");
  check(cut_at(10 + 30 + 119).size() == 1, "the last tick of its rest is still solid");
  check(cut_at(10 + 30 + 120).empty(), "two seconds at sixty hertz is 120 ticks of rest, and then it is gone");
  const shared::platform_view_t view = shared::platform_view_of(*platform);
  check(shared::platform_has_vanished_at_tick(view, 10 + 30 + 120, flight.tick_interval_seconds) &&
            !shared::platform_has_vanished_at_tick(view, 10 + 30 + 119, flight.tick_interval_seconds),
        "the server reaps it on the tick the cut drops it");

  const shared::mover_t& rested = cut_at(10 + 60)[0];
  check(rested.uid == uid, "the mover names the platform, which is what ground_mover_uid stores");
  check(rested.pieces.size() == 1 && rested.pieces[0].planes.size() == 6, "it is one box");
  check(linalg::length(rested.pose_at_tick_end.position - rested.pose_at_tick_start.position) == 0.f,
        "its two poses are equal, so the push carries nobody");
  check(std::fabs(rested.swept_bounds.min.x - (300.f - 48.f)) < 0.01f &&
            std::fabs(rested.swept_bounds.max.x - (300.f + 48.f)) < 0.01f &&
            std::fabs(rested.swept_bounds.max.y - 104.f) < 0.01f,
        "the box sits where half a second at 600 units a second left it");

  check(shared::platform_solid_fraction_elapsed(view, 10 + 30, 0.f, flight.tick_interval_seconds) == 0.f &&
            std::fabs(shared::platform_solid_fraction_elapsed(view, 10 + 30 + 60, 0.f,
                                                              flight.tick_interval_seconds) - 0.5f) < 1e-4f,
        "the wipe runs from landing to vanishing");
  check(std::fabs(shared::platform_dissolve_fraction(0.8f)) < 1e-4f &&
            std::fabs(shared::platform_dissolve_fraction(0.9f) - 0.5f) < 1e-4f &&
            std::fabs(shared::platform_dissolve_fraction(1.f) - 1.f) < 1e-4f,
        "the dissolve runs over the last fifth");
}

static void test_a_shrinking_platform_shrinks_on_the_ticks_the_cut_sweeps()
{
  printf("\n[pin] a shrinking platform's box shrinks toward half_extents_when_vanishing on the cut's clock\n");

  shared::Entity_System               system;
  const shared::entity_uid_t          uid      = system.spawn(entities::entity_type::Shrinking_Platform_Entity);
  entities::Shrinking_Platform_Entity* platform = system.get<entities::Shrinking_Platform_Entity>(uid);

  const shared::fixed_arc_flight_settings_t flight{.tick_interval_seconds = 1.f / 60.f, .gravity = 800.f};

  platform->flight                      = {.launch_position = {0.f, 100.f, 0.f}, .launch_tick = 10, .flight_ticks = 0};
  platform->projectile.velocity         = {0.f, 0.f, 0.f};
  platform->solid_seconds               = 2.f;
  platform->half_extents                = {64.f, 4.f, 64.f};
  platform->half_extents_when_vanishing = {8.f, 4.f, 8.f};

  const shared::platform_view_t view = shared::platform_view_of(*platform);

  std::vector<shared::mover_t> movers;
  const auto cut_at = [&](uint32_t tick) -> const std::vector<shared::mover_t>&
  {
    movers.clear();
    shared::collect_spawned_platforms(system, tick, flight, movers);
    return movers;
  };

  check(std::fabs(cut_at(10)[0].swept_bounds.max.x - 64.f) < 1e-4f, "it lands at half_extents");
  check(std::fabs(cut_at(10 + 60)[0].swept_bounds.max.x - 36.f) < 1e-4f,
        "halfway through its solid time it is halfway to half_extents_when_vanishing");
  check(std::fabs(cut_at(10 + 119)[0].swept_bounds.max.y - 104.f) < 1e-4f, "y is held, so the top does not drop");
  check(cut_at(10 + 120).empty(), "and it vanishes on the same tick a plain platform would");

  const linalg::vec3f drawn = shared::platform_half_extents_at(view, 10 + 60, 0.5f, flight.tick_interval_seconds);
  check(std::fabs(drawn.x - (64.f - 56.f * (60.5f / 120.f))) < 1e-4f,
        "the draw reads the same lerp with the sub-tick fraction");
}

int main()
{
  test_every_predicted_type_feeds_exactly_one_collect();
  test_a_pad_is_flattened_into_what_the_step_reads();
  test_a_modifier_scales_the_settings_of_a_hull_inside_it();
  test_a_timed_modifier_is_live_for_exactly_its_lifetime();
  test_a_bubble_is_placed_by_the_tick_it_is_cut_for();
  test_a_platform_is_solid_from_the_tick_it_lands_until_its_rest_runs_out();
  test_a_shrinking_platform_shrinks_on_the_ticks_the_cut_sweeps();
  test_a_switch_reaches_the_geometry_it_owns();
  test_a_team_wall_is_not_there_for_its_team_and_visible_to_everyone();
  test_a_drawn_mover_carries_its_rider_by_the_same_fraction();

  printf("\nmovement_volumes_test %s (%d)\n", failure_count == 0 ? "PASSED" : "FAILED",
         failure_count);
  return failure_count == 0 ? 0 : 1;
}

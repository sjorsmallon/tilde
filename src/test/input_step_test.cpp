// The shared input step — src/shared/input_step.{hpp,cpp}, step 3 of input_step_plan.md.
//
// One function turns a sub-tick step into what the player did, on the server and in both of
// the client's prediction paths. What is worth guarding is the ORDER inside a step and the
// gates around each action, which the two hand-written copies used to agree on by inspection.
// No server: feed steps, assert the outcome and the state.
#include "../shared/assets/asset_state.hpp"
#include "../shared/collision/collision_detection.hpp"
#include "../shared/cvars/generated/cvars_generated.hpp"
#include "../shared/input_step.hpp"
#include "../shared/movement/movement_override.hpp"
#include "../shared/weapons.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

using shared::apply_input_step_to_player;
using shared::input_restrictions_t;
using shared::input_step_outcome_t;
using shared::player_input_state_t;
using shared::shot_refusal_t;
using shared::subtick_step_t;
using shared::subtick_time;
using shared::subtick_time_after;
using shared::subtick_time_t;
using shared::SUBTICK_SLOT_COUNT;

static int failures = 0;

static void check(bool condition, const char* what)
{
  printf(condition ? "  ok   %s\n" : "  FAIL %s\n", what);
  if (!condition)
    ++failures;
}

static constexpr float TICK_DT = 1.f / 60.f;

struct bench_t
{
  cvars::cvar_state_t         cvars;
  shared::movement_settings_t settings = shared::movement_settings_from_cvars(cvars);
  Bounding_Volume_Hierarchy   empty_world = build_bvh({});
  shared::predicted_world_t   world{};
};

static subtick_step_t step_from(uint64_t buttons, uint32_t start_slot, uint32_t slot_count)
{
  return {.buttons     = buttons,
          .view        = {},
          .dt          = TICK_DT * static_cast<float>(slot_count) / static_cast<float>(SUBTICK_SLOT_COUNT),
          .start_slot  = start_slot,
          .view_at_end = {},
          .slot_count  = slot_count};
}

static subtick_step_t whole_tick(uint64_t buttons)
{
  return step_from(buttons, 0, SUBTICK_SLOT_COUNT);
}

// The weapon in the slot its row names, born with the row's counts.
static void carry(player_input_state_t& state, entities::Weapon weapon)
{
  const shared::weapon_definition_t& row = shared::get_weapon_definition(weapon);
  check(!state.inventory.slots[row.slot].weapon.has_value(), "the test loadout does not double a slot");
  state.inventory.slots[row.slot] = {
      .weapon         = weapon,
      .magazine       = {.size = row.magazine_size, .ammo = shared::get_full_magazine_ammo(row), .reserve_ammo = -1},
      .next_fire_time = 0};
}

static player_input_state_t holding(entities::Weapon weapon)
{
  player_input_state_t state{};
  carry(state, weapon);
  state.inventory.active_slot = shared::get_weapon_definition(weapon).slot;
  state.move.feet             = {0.f, 100.f, 0.f};
  return state;
}

static input_step_outcome_t run(const bench_t& bench, player_input_state_t& state, const subtick_step_t& step,
                                uint64_t buttons_entering_step, uint32_t tick,
                                input_restrictions_t restrictions = {})
{
  return apply_input_step_to_player(bench.settings, bench.empty_world, bench.world, restrictions, step,
                                    buttons_entering_step, subtick_time(tick, step.start_slot), TICK_DT,
                                    state);
}

static bool fired(const input_step_outcome_t& outcome, entities::Fire_Trigger trigger)
{
  return outcome.fire_due[trigger].has_value();
}

static void test_a_pilot_release_does_not_dash(const bench_t& bench)
{
  printf("a press while piloting lets go of the rocket and fires nothing\n");

  player_input_state_t state = holding(entities::Weapon::Dash);
  shared::attach_override(state.move.movement, {.kind            = entities::Movement_Override::Pilot,
                                                .target_position = {500.f, 100.f, 0.f},
                                                .seconds         = 2.f,
                                                .speed           = 100.f,
                                                .radius          = 10.f});

  const input_step_outcome_t outcome = run(bench, state, whole_tick(Button::Fire), 0, 10);

  check(outcome.pilot_released, "the press released the pilot");
  check(!fired(outcome, entities::Fire_Trigger::Primary), "nothing fired");
  check(state.move.movement.seconds_until_impulse_ready == 0.f, "no dash was charged");
  check(state.move.movement.override_seconds_remaining == 0.f, "the flight ends at the next step");
}

static void test_a_switch_cancels_the_reload_started_earlier_in_the_tick(const bench_t& bench)
{
  printf("a switch inside a step cancels a reload started earlier in the same tick\n");

  player_input_state_t state = holding(entities::Weapon::Scout);
  carry(state, entities::Weapon::Knife);
  state.inventory.slots[entities::Inventory_Slot::Primary].magazine.ammo = 4;

  const input_step_outcome_t reload = run(bench, state, step_from(Button::Reload, 0, 20), 0, 10);
  check(reload.reload_started, "the reload started");
  check(state.inventory.reload_complete_time == subtick_time_after(subtick_time(10, 0), 2.0f, TICK_DT),
        "the reload deadline is the scout's 2.0s from the press");

  const input_step_outcome_t switched =
      run(bench, state, step_from(Button::Reload | Button::Key3, 20, 44), Button::Reload, 10);
  check(switched.switched_to == entities::Inventory_Slot::Melee, "the knife came up");
  check(state.inventory.active_slot == entities::Inventory_Slot::Melee, "the hand is on the knife");
  check(state.inventory.reload_complete_time == 0, "the reload died with the switch");
  check(state.inventory.deploy_complete_time == subtick_time_after(subtick_time(10, 20), 0.4f, TICK_DT),
        "the deploy runs from the switch's slot for the knife's 0.4s");
  check(state.inventory.slots[entities::Inventory_Slot::Primary].magazine.ammo == 4,
        "the scout's magazine was never filled");
}

static void test_a_held_trigger_repeats_at_the_interval(const bench_t& bench)
{
  printf("a held trigger repeats at fire_interval_seconds, not once per step\n");

  player_input_state_t state = holding(entities::Weapon::Mock);
  const shared::weapon_definition_t& mock = shared::get_weapon_definition(entities::Weapon::Mock);
  check(mock.primary_fire.fires_while_held, "the mock fires while held");

  const input_step_outcome_t press = run(bench, state, whole_tick(Button::Fire), 0, 1);
  check(press.fire_due[entities::Fire_Trigger::Primary] == subtick_time(1, 0), "the press fires at its slot");

  const subtick_time_t next = subtick_time_after(subtick_time(1, 0), mock.fire_interval_seconds, TICK_DT);
  check(state.inventory.slots[mock.slot].next_fire_time == next, "the clock is charged from the press");

  int repeats = 0;
  for (uint32_t tick = 2; tick < 20; ++tick)
  {
    // Two steps per tick, both with the button down: a step is not a shot.
    for (const subtick_step_t& step : {step_from(Button::Fire, 0, 32), step_from(Button::Fire, 32, 32)})
    {
      const subtick_time_t       clock_before = state.inventory.slots[mock.slot].next_fire_time;
      const input_step_outcome_t held         = run(bench, state, step, Button::Fire, tick);
      if (fired(held, entities::Fire_Trigger::Primary))
      {
        ++repeats;
        check(*held.fire_due[entities::Fire_Trigger::Primary] ==
                  std::max(subtick_time(tick, step.start_slot), clock_before),
              "the repeat lands when the clock opens, inside the step it opens in");
      }
    }
  }
  const int ticks_covered = 18;
  const int expected      = static_cast<int>(ticks_covered * TICK_DT / mock.fire_interval_seconds);
  check(repeats == expected || repeats == expected + 1, "the repeats count the intervals, not the steps");
}

static void test_a_due_reload_completes_before_the_shot(const bench_t& bench)
{
  printf("a shot that lands while a reload is due completes the reload first and fires\n");

  player_input_state_t state = holding(entities::Weapon::Scout);
  const shared::weapon_definition_t& scout = shared::get_weapon_definition(entities::Weapon::Scout);
  state.inventory.slots[scout.slot].magazine.ammo = 3;

  (void)run(bench, state, whole_tick(Button::Reload), 0, 1);
  check(state.inventory.reload_complete_time != 0, "reloading");

  const input_step_outcome_t shot = run(bench, state, whole_tick(Button::Fire), 0, 200);
  check(shot.reload_finished, "the due reload finished at the press");
  check(fired(shot, entities::Fire_Trigger::Primary), "and the shot fired");
  check(state.inventory.reload_complete_time == 0, "not reloading any more");
  check(state.inventory.slots[scout.slot].magazine.ammo == scout.magazine_size - 1,
        "a full magazine less the shot");
}

static void test_a_refused_shot_charges_nothing(const bench_t& bench)
{
  printf("a shot refused for deploy, interval or ammo charges nothing\n");

  const shared::weapon_definition_t& scout = shared::get_weapon_definition(entities::Weapon::Scout);

  {
    player_input_state_t state = holding(entities::Weapon::Knife);
    carry(state, entities::Weapon::Scout);
    (void)run(bench, state, whole_tick(Button::Key1), 0, 1);
    check(state.inventory.active_slot == scout.slot, "the scout came up");

    const input_step_outcome_t early = run(bench, state, whole_tick(Button::Fire), 0, 5);
    check(early.refused[entities::Fire_Trigger::Primary] == shot_refusal_t::Deploying, "refused: deploying");
    check(!fired(early, entities::Fire_Trigger::Primary), "no shot");
    check(state.inventory.slots[scout.slot].magazine.ammo == scout.magazine_size, "ammo untouched");
    check(state.inventory.slots[scout.slot].next_fire_time == 0, "clock untouched");
  }
  {
    player_input_state_t state = holding(entities::Weapon::Scout);
    const input_step_outcome_t first = run(bench, state, whole_tick(Button::Fire), 0, 50);
    check(fired(first, entities::Fire_Trigger::Primary), "the first shot fires");
    const subtick_time_t      charged = state.inventory.slots[scout.slot].next_fire_time;
    const input_step_outcome_t second = run(bench, state, whole_tick(Button::Fire), 0, 51);
    check(second.refused[entities::Fire_Trigger::Primary] == shot_refusal_t::Interval, "refused: interval");
    check(state.inventory.slots[scout.slot].magazine.ammo == scout.magazine_size - 1, "one shot's ammo");
    check(state.inventory.slots[scout.slot].next_fire_time == charged, "the clock is the first shot's");
  }
  {
    player_input_state_t state = holding(entities::Weapon::Scout);
    state.inventory.slots[scout.slot].magazine.ammo = 0;
    const input_step_outcome_t empty = run(bench, state, whole_tick(Button::Fire), 0, 50);
    check(empty.refused[entities::Fire_Trigger::Primary] == shot_refusal_t::Empty, "refused: empty");
    check(state.inventory.slots[scout.slot].next_fire_time == 0, "clock untouched");
  }
}

static void test_frozen_holds_the_hull_and_lets_a_switch_land(const bench_t& bench)
{
  printf("frozen: velocity zeroed, no fire, a switch still lands\n");

  player_input_state_t state = holding(entities::Weapon::Scout);
  carry(state, entities::Weapon::Knife);
  state.move.velocity = {300.f, 0.f, 0.f};
  const vec3f feet_before = state.move.feet;

  const input_step_outcome_t outcome = run(bench, state, whole_tick(Button::Fire | Button::Key3), 0, 10,
                                           {.world_is_frozen = true});

  check(state.move.velocity.x == 0.f && state.move.velocity.y == 0.f, "velocity zeroed");
  check(state.move.feet.y == feet_before.y, "the hull did not fall");
  check(!fired(outcome, entities::Fire_Trigger::Primary), "no shot");
  check(outcome.switched_to == entities::Inventory_Slot::Melee, "the switch landed");
}

static void test_dead_fires_nothing_and_still_falls(const bench_t& bench)
{
  printf("dead: no fire, the hull still falls\n");

  player_input_state_t state = holding(entities::Weapon::Scout);
  const float          y_before = state.move.feet.y;

  const input_step_outcome_t outcome =
      run(bench, state, whole_tick(Button::Fire | Button::Forward), 0, 10, {.is_dead = true});

  check(!fired(outcome, entities::Fire_Trigger::Primary), "no shot");
  check(outcome.refused[entities::Fire_Trigger::Primary] == shot_refusal_t::None, "and no refusal: the trigger was never read");
  check(state.move.feet.y < y_before, "the hull fell");
  check(std::fabs(state.move.velocity.x) < 1e-3f, "the forward key moved nothing");
}

int main()
{
  static assets::asset_state_t asset_state;
  assets::set_state(&asset_state);
  assets::number_asset_ids_from_tree(asset_state, "resources");

  const bench_t bench;

  test_a_pilot_release_does_not_dash(bench);
  test_a_switch_cancels_the_reload_started_earlier_in_the_tick(bench);
  test_a_held_trigger_repeats_at_the_interval(bench);
  test_a_due_reload_completes_before_the_shot(bench);
  test_a_refused_shot_charges_nothing(bench);
  test_frozen_holds_the_hull_and_lets_a_switch_land(bench);
  test_dead_fires_nothing_and_still_falls(bench);

  printf(failures == 0 ? "\ninput_step: all checks passed\n" : "\ninput_step: %d FAILED\n", failures);
  return failures == 0 ? 0 : 1;
}

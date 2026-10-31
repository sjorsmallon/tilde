#include "input_step.hpp"

#include "entities/generated/entities/weapon_entity_generated.hpp"
#include "linalg.hpp"
#include "log.hpp"
#include "weapon_instance.hpp"
#include "movement/player_constants.hpp"

namespace shared
{

// try_begin_shot's four comparisons, charging the slot when every one passes.
static shot_refusal_t charge_shot_clocks(inventory_state_t& inventory, inventory_slot_state_t& held,
                                         const weapon_definition_t& row, subtick_time_t fire_time,
                                         float tick_dt, input_step_outcome_t& outcome)
{
  if (fire_time < held.next_fire_time)
    return shot_refusal_t::Interval;

  if (fire_time < inventory.deploy_complete_time)
    return shot_refusal_t::Deploying;

  if (inventory.reload_complete_time != 0)
  {
    if (fire_time < inventory.reload_complete_time)
      return shot_refusal_t::Reloading;

    held.magazine                  = reloaded_magazine(row, held.magazine);
    inventory.reload_complete_time = 0;
    outcome.reload_finished        = true;
  }

  if (!ammo_allows_a_shot(held.magazine.ammo))
    return shot_refusal_t::Empty;

  if (held.magazine.ammo > 0)
    --held.magazine.ammo;

  held.next_fire_time = subtick_time_after(fire_time, row.fire_interval_seconds, tick_dt);
  return shot_refusal_t::None;
}

player_input_state_t read_input_state_from_player(const Entity_System& system,
                                                  const entities::Player_Entity& player)
{
  player_input_state_t state{};
  state.move = {.feet = player.position, .velocity = player.velocity, .movement = player.movement};

  state.inventory.active_slot          = player.inventory.active_slot;
  state.inventory.deploy_complete_time = player.inventory.deploy_complete_time;
  state.inventory.reload_complete_time = player.reload_complete_time;

  for (uint32_t index = 0; index < enum_traits<entities::Inventory_Slot>::count; ++index)
  {
    const entities::Inventory_Slot slot       = static_cast<entities::Inventory_Slot>(index);
    const entity_uid_t             weapon_uid = player.inventory.weapons[slot];
    if (weapon_uid == null_entity_uid)
      continue;

    const entities::Weapon_Entity* weapon = system.get<entities::Weapon_Entity>(weapon_uid);
    if (weapon == nullptr)
    {
      log_error("read_input_state_from_player: player {} holds uid {} in {}, which resolves to nothing",
                player.entity_id, weapon_uid, to_string(slot));
      continue;
    }
    if (static_cast<uint32_t>(weapon->weapon_id) >= WEAPON_DEFINITIONS.size())
    {
      log_error("read_input_state_from_player: player {} holds weapon id {} in {}, outside the Weapon enum",
                player.entity_id, static_cast<uint32_t>(weapon->weapon_id), to_string(slot));
      continue;
    }

    state.inventory.slots[slot] = {.weapon         = weapon->weapon_id,
                                   .magazine       = get_magazine(*weapon),
                                   .next_fire_time = weapon->next_fire_time};
  }

  state.toggles = {.reveal_light_on       = player.reveal_light_on,
                   .reveal_light_overhead = player.reveal_light_overhead,
                   .held_lens_on          = player.held_lens_on};
  return state;
}

bool switch_active_slot(inventory_state_t& inventory, entities::Inventory_Slot slot,
                        subtick_time_t switch_time, float tick_dt)
{
  if (inventory.active_slot == slot)
    return false;

  inventory.active_slot          = slot;
  inventory.reload_complete_time = 0;

  const weapon_definition_t* raised = try_find_weapon_row(inventory.slots[slot]);
  inventory.deploy_complete_time    = subtick_time_after(
      switch_time, raised != nullptr ? raised->deploy_duration_seconds : 0.f, tick_dt);
  return true;
}

static uint64_t button_for_trigger(entities::Fire_Trigger trigger)
{
  switch (trigger)
  {
  case entities::Fire_Trigger::Primary:   return Button::Fire;
  case entities::Fire_Trigger::Secondary: return Button::Secondary_Fire;
  }
  return Button::Fire;
}

// One trigger. A pilot's hands are on the rocket: a press lets go of it and nothing fires. Else
// the row on the button acts at the press, or at the held repeat, behind its own gates.
static void apply_trigger_to_player(const movement_settings_t& settings, const subtick_step_t& step,
                                    uint64_t pressed, subtick_time_t step_start,
                                    subtick_time_t step_end, float tick_dt,
                                    entities::Fire_Trigger trigger, player_input_state_t& state,
                                    input_step_outcome_t& outcome)
{
  const uint64_t button               = button_for_trigger(trigger);
  const bool     pressed_in_this_step = (pressed & button) != 0;
  const bool     down                 = (step.buttons & button) != 0;

  if (state.move.movement.active_override == entities::Movement_Override::Pilot)
  {
    if (pressed_in_this_step && try_end_pilot_flight(state.move.movement))
      outcome.pilot_released = true;
    return;
  }

  inventory_state_t&         inventory = state.inventory;
  inventory_slot_state_t&    held      = inventory.slots[inventory.active_slot];
  const weapon_definition_t* held_row  = try_find_weapon_row(held);
  if (held_row == nullptr)
    return;

  const weapon_fire_t& fire = get_weapon_fire_for_button(*held_row, trigger);

  std::optional<subtick_time_t> fire_time;
  if (pressed_in_this_step)
    fire_time = step_start;
  else if (down)
    fire_time = try_find_held_fire_time(fire, held.next_fire_time, inventory.deploy_complete_time,
                                        step_start, step_end);
  if (!fire_time)
    return;

  switch (fire.resolution)
  {
  case entities::Fire_Resolution::None:
  case entities::Fire_Resolution::Zoom:
    break;

  case entities::Fire_Resolution::Lens_View:
    state.toggles.held_lens_on = !state.toggles.held_lens_on;
    break;

  case entities::Fire_Resolution::Reveal_Light:
  case entities::Fire_Resolution::Erase_Light:
    state.toggles.reveal_light_on = !state.toggles.reveal_light_on;
    break;

  case entities::Fire_Resolution::Reveal_Light_Overhead:
    state.toggles.reveal_light_overhead = !state.toggles.reveal_light_overhead;
    break;

  // No clocks and no ammo: the world half is the whole of these.
  case entities::Fire_Resolution::Canopy:
  case entities::Fire_Resolution::Recall:
    outcome.fire_due[trigger] = fire_time;
    break;

  case entities::Fire_Resolution::Hitscan:
  case entities::Fire_Resolution::Projectile:
  case entities::Fire_Resolution::Place:
  {
    const shot_refusal_t refusal =
        charge_shot_clocks(inventory, held, *held_row, *fire_time, tick_dt, outcome);
    if (refusal == shot_refusal_t::None)
      outcome.fire_due[trigger] = fire_time;
    else
      outcome.refused[trigger] = refusal;
    break;
  }

  case entities::Fire_Resolution::Self_Impulse:
  {
    const vec3f direction = linalg::direction_from_angles(step.view.yaw, step.view.pitch);
    if (try_apply_self_impulse(settings, *held_row, trigger, direction, state.move.movement,
                               state.move.velocity))
      outcome.fire_due[trigger] = fire_time;
    break;
  }

  case entities::Fire_Resolution::Pilot:
  {
    const vec3f eye = shared::player_eye_position(state.move.feet);
    if (try_begin_pilot_flight(*held_row, trigger, eye, state.move.movement))
      outcome.fire_due[trigger] = fire_time;
    break;
  }
  }
}

input_step_outcome_t apply_input_step_to_player(
    const movement_settings_t& settings, const Bounding_Volume_Hierarchy& bvh,
    const predicted_world_t& world, const input_restrictions_t& restrictions,
    const subtick_step_t& step, uint64_t buttons_entering_step, subtick_time_t step_start,
    float tick_dt, player_input_state_t& state, debug_collision::Face_Bucket* debug_faces)
{
  input_step_outcome_t outcome{};
  inventory_state_t&   inventory = state.inventory;

  const uint64_t       pressed  = step.buttons & ~buttons_entering_step;
  const subtick_time_t step_end = step_start + step.slot_count;

  // 1. The switch.
  if (const std::optional<entities::Inventory_Slot> selected = try_slot_selected_by(pressed);
      selected && switch_active_slot(inventory, *selected, step_start, tick_dt))
    outcome.switched_to = *selected;

  // 2. The reload, on the slot in hand and not already reloading.
  inventory_slot_state_t&    held     = inventory.slots[inventory.active_slot];
  const weapon_definition_t* held_row = try_find_weapon_row(held);
  if ((pressed & Button::Reload) != 0 && held_row != nullptr && inventory.reload_complete_time == 0 &&
      reload_may_start(*held_row, held.magazine))
  {
    inventory.reload_complete_time =
        subtick_time_after(step_start, held_row->reload_duration_seconds, tick_dt);
    outcome.reload_started = true;
  }

  // 3. The move. Frozen holds the hull; dead clears the buttons and still falls.
  if (restrictions.world_is_frozen)
  {
    state.move.velocity = {0.f, 0.f, 0.f};
  }
  else
  {
    move_input_t move_input = move_input_from_subtick_step(step);
    if (restrictions.is_dead)
      move_input.buttons = Move_Input{};

    state.move = player_move(settings, bvh, world, state.move, move_input, &outcome.move_events,
                             debug_faces);
  }

  // 4. The triggers, Primary then Secondary. 5. The toggles, flipped by a press under the same gates.
  const bool may_fire =
      !restrictions.is_dead && !restrictions.world_is_frozen && !restrictions.drives_merged_body;
  if (may_fire)
    for (const entities::Fire_Trigger trigger :
         {entities::Fire_Trigger::Primary, entities::Fire_Trigger::Secondary})
      apply_trigger_to_player(settings, step, pressed, step_start, step_end, tick_dt, trigger, state,
                              outcome);

  return outcome;
}

const char* to_string(shot_refusal_t refusal)
{
  switch (refusal)
  {
  case shot_refusal_t::None:      return "none";
  case shot_refusal_t::Interval:  return "the fire interval has not elapsed";
  case shot_refusal_t::Deploying: return "the weapon is still deploying";
  case shot_refusal_t::Reloading: return "the reload is not done";
  case shot_refusal_t::Empty:     return "the magazine is empty";
  }
  return "?";
}

} // namespace shared

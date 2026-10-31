#include "subtick.hpp"

namespace shared
{

namespace
{

float duration_of_slots(float tick_dt, uint32_t from_slot, uint32_t to_slot)
{
  return tick_dt * static_cast<float>(to_slot - from_slot) /
         static_cast<float>(SUBTICK_SLOT_COUNT);
}

} // namespace

subtick_steps_t split_input_per_tick_into_subtick_steps(const subtick_input_t& input,
                                                        float tick_dt)
{
  subtick_steps_t result{};

  uint64_t       buttons = input.buttons_at_start;
  subtick_view_t view    = input.view_at_start;
  uint32_t       slot    = 0;

  for (uint32_t change_index = 0; change_index < input.button_change_count; ++change_index)
  {
    const subtick_button_change_t& change = input.button_changes[change_index];
    result.steps[result.step_count++] = {.buttons     = buttons,
                                         .view        = view,
                                         .dt          = duration_of_slots(tick_dt, slot, change.slot),
                                         .start_slot  = slot,
                                         .view_at_end = change.view_after,
                                         .slot_count  = change.slot - slot};
    buttons = change.buttons_after;
    view    = change.view_after;
    slot    = change.slot;
  }

  result.steps[result.step_count++] = {
      .buttons     = buttons,
      .view        = view,
      .dt          = duration_of_slots(tick_dt, slot, SUBTICK_SLOT_COUNT),
      .start_slot  = slot,
      .view_at_end = input.view_at_end,
      .slot_count  = SUBTICK_SLOT_COUNT - slot};

  return result;
}

uint64_t subtick_presses(const subtick_input_t& input, uint64_t buttons_before)
{
  uint64_t rising   = input.buttons_at_start & ~buttons_before;
  uint64_t previous = input.buttons_at_start;

  for (uint32_t change_index = 0; change_index < input.button_change_count; ++change_index)
  {
    const uint64_t buttons = input.button_changes[change_index].buttons_after;
    rising |= buttons & ~previous;
    previous = buttons;
  }

  return rising;
}

uint32_t subtick_slot_of_press(const subtick_input_t& input, uint64_t buttons_before,
                               uint64_t button)
{
  if ((input.buttons_at_start & ~buttons_before & button) != 0)
    return 0;

  uint64_t previous = input.buttons_at_start;
  for (uint32_t change_index = 0; change_index < input.button_change_count; ++change_index)
  {
    const subtick_button_change_t& change = input.button_changes[change_index];
    if ((change.buttons_after & ~previous & button) != 0)
      return change.slot;
    previous = change.buttons_after;
  }

  return SUBTICK_SLOT_COUNT;
}

bool try_append_subtick_button_change(subtick_input_t& input, uint32_t slot, uint64_t buttons_after,
                             subtick_view_t view_after)
{
  if (slot == 0 || slot >= SUBTICK_SLOT_COUNT)
    return false;

  if (input.button_change_count != 0 && slot <= input.button_changes[input.button_change_count - 1].slot)
    return false;

  if (input.button_change_count >= MAX_SUBTICK_BUTTON_CHANGES)
    return false;

  input.button_changes[input.button_change_count++] = {static_cast<uint8_t>(slot), buttons_after, view_after};
  return true;
}

bool try_record_subtick_state(subtick_input_t& input, uint32_t slot, uint64_t buttons,
                              subtick_view_t view)
{
  if (slot >= SUBTICK_SLOT_COUNT)
    slot = SUBTICK_SLOT_COUNT - 1;

  // Out of order. The recorder takes one frame's transitions in arrival order,
  // so a slot behind the last button change is the caller having sorted nothing; pin it
  // to that button change rather than drop the transition.
  if (input.button_change_count != 0 && slot < input.button_changes[input.button_change_count - 1].slot)
    slot = input.button_changes[input.button_change_count - 1].slot;

  // The pin above leaves slot 0 reachable only while there are no button changes.
  if (slot == 0)
  {
    input.buttons_at_start = buttons;
    input.view_at_start    = view;
    return true;
  }

  const bool lands_on_last_button_change =
      input.button_change_count != 0 && input.button_changes[input.button_change_count - 1].slot == slot;

  const uint64_t state_entering_slot =
      lands_on_last_button_change ? (input.button_change_count == 1
                                ? input.buttons_at_start
                                : input.button_changes[input.button_change_count - 2].buttons_after)
                         : input.buttons_at_end();

  if (lands_on_last_button_change)
  {
    if (buttons == state_entering_slot)
    {
      --input.button_change_count;
    }
    else
    {
      input.button_changes[input.button_change_count - 1].buttons_after = buttons;
      input.button_changes[input.button_change_count - 1].view_after    = view;
    }
    return true;
  }

  // No step to cut, and the aim is deliberately dropped with it: an earlier
  // button change's view_after is the aim AT that earlier slot, and folding a later
  // reading back onto it would hand a shot the angle the mouse reached after
  // the trigger. view_at_end is where a motion past the last button change belongs.
  if (buttons == state_entering_slot)
    return true;

  return try_append_subtick_button_change(input, slot, buttons, view);
}

uint32_t subtick_slot_from_fraction(float fraction)
{
  if (!(fraction > 0.f))
    return 0;
  if (fraction >= 1.f)
    return SUBTICK_SLOT_COUNT - 1;

  const uint32_t slot = static_cast<uint32_t>(fraction * static_cast<float>(SUBTICK_SLOT_COUNT));
  return slot >= SUBTICK_SLOT_COUNT ? SUBTICK_SLOT_COUNT - 1 : slot;
}

} // namespace shared

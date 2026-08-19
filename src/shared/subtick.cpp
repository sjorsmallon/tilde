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

subtick_schedule_t split_tick(const subtick_input_t& input, float tick_dt)
{
  subtick_schedule_t schedule{};

  uint64_t buttons = input.buttons_at_start;
  uint32_t slot    = 0;

  for (uint32_t edge_index = 0; edge_index < input.edge_count; ++edge_index)
  {
    const subtick_edge_t& edge = input.edges[edge_index];
    schedule.steps[schedule.step_count++] = {
        .buttons = buttons, .dt = duration_of_slots(tick_dt, slot, edge.slot), .start_slot = slot};
    buttons = edge.buttons_after;
    slot    = edge.slot;
  }

  schedule.steps[schedule.step_count++] = {
      .buttons    = buttons,
      .dt         = duration_of_slots(tick_dt, slot, SUBTICK_SLOT_COUNT),
      .start_slot = slot};

  return schedule;
}

uint64_t subtick_rising_edges(const subtick_input_t& input, uint64_t buttons_before)
{
  uint64_t rising   = input.buttons_at_start & ~buttons_before;
  uint64_t previous = input.buttons_at_start;

  for (uint32_t edge_index = 0; edge_index < input.edge_count; ++edge_index)
  {
    const uint64_t buttons = input.edges[edge_index].buttons_after;
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
  for (uint32_t edge_index = 0; edge_index < input.edge_count; ++edge_index)
  {
    const subtick_edge_t& edge = input.edges[edge_index];
    if ((edge.buttons_after & ~previous & button) != 0)
      return edge.slot;
    previous = edge.buttons_after;
  }

  return SUBTICK_SLOT_COUNT;
}

bool try_append_subtick_edge(subtick_input_t& input, uint32_t slot, uint64_t buttons_after)
{
  if (slot == 0 || slot >= SUBTICK_SLOT_COUNT)
    return false;

  if (input.edge_count != 0 && slot <= input.edges[input.edge_count - 1].slot)
    return false;

  if (input.edge_count >= MAX_SUBTICK_EDGES)
    return false;

  input.edges[input.edge_count++] = {static_cast<uint8_t>(slot), buttons_after};
  return true;
}

bool try_record_subtick_state(subtick_input_t& input, uint32_t slot, uint64_t buttons)
{
  if (slot >= SUBTICK_SLOT_COUNT)
    slot = SUBTICK_SLOT_COUNT - 1;

  // Out of order. The recorder takes one frame's transitions in arrival order,
  // so a slot behind the last edge is the caller having sorted nothing; pin it
  // to that edge rather than drop the transition.
  if (input.edge_count != 0 && slot < input.edges[input.edge_count - 1].slot)
    slot = input.edges[input.edge_count - 1].slot;

  // The pin above leaves slot 0 reachable only while there are no edges.
  if (slot == 0)
  {
    input.buttons_at_start = buttons;
    return true;
  }

  const bool lands_on_last_edge =
      input.edge_count != 0 && input.edges[input.edge_count - 1].slot == slot;

  const uint64_t state_entering_slot =
      lands_on_last_edge ? (input.edge_count == 1
                                ? input.buttons_at_start
                                : input.edges[input.edge_count - 2].buttons_after)
                         : input.buttons_at_end();

  if (lands_on_last_edge)
  {
    if (buttons == state_entering_slot)
      --input.edge_count;
    else
      input.edges[input.edge_count - 1].buttons_after = buttons;
    return true;
  }

  if (buttons == state_entering_slot)
    return true;

  return try_append_subtick_edge(input, slot, buttons);
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

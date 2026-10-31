#include "subtick_codec.hpp"

namespace network
{

namespace
{

void write_view_angle(game::ViewAngle& out, const shared::subtick_view_t& view)
{
  out.set_yaw(view.yaw);
  out.set_pitch(view.pitch);
}

} // namespace

void write_subtick_input(game::C2S_ClientInput& out_message,
                         const shared::subtick_input_t& input)
{
  out_message.set_buttons_bitfield(input.buttons_at_start);
  write_view_angle(*out_message.mutable_viewangles_at_start(), input.view_at_start);
  write_view_angle(*out_message.mutable_viewangles(), input.view_at_end);
  out_message.clear_subtick_button_changes();

  for (uint32_t change_index = 0; change_index < input.button_change_count; ++change_index)
  {
    game::C2S_SubtickButtonChange* wire_change = out_message.add_subtick_button_changes();
    wire_change->set_slot(input.button_changes[change_index].slot);
    wire_change->set_buttons_after(input.button_changes[change_index].buttons_after);
    write_view_angle(*wire_change->mutable_view_after(), input.button_changes[change_index].view_after);
  }
}

std::optional<shared::subtick_input_t>
try_read_subtick_input(const game::C2S_ClientInput& message)
{
  shared::subtick_input_t input{};
  input.buttons_at_start = message.buttons_bitfield();

  // A single-angle tick is still a whole tick, not a malformed one: a bot, a
  // replay and a client that reports no per-change aim all send one, and every
  // step then runs under it exactly as it did before the aim went sub-tick.
  input.view_at_end   = {message.viewangles().yaw(), message.viewangles().pitch()};
  input.view_at_start = message.has_viewangles_at_start()
                            ? shared::subtick_view_t{message.viewangles_at_start().yaw(),
                                                     message.viewangles_at_start().pitch()}
                            : input.view_at_end;

  shared::subtick_view_t view_in_effect = input.view_at_start;
  for (const game::C2S_SubtickButtonChange& wire_change : message.subtick_button_changes())
  {
    if (wire_change.has_view_after())
      view_in_effect = {wire_change.view_after().yaw(), wire_change.view_after().pitch()};

    if (!shared::try_append_subtick_button_change(input, wire_change.slot(), wire_change.buttons_after(),
                                         view_in_effect))
      return std::nullopt;
  }

  return input;
}

} // namespace network

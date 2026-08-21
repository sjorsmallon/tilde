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
  out_message.clear_subtick_edges();

  for (uint32_t edge_index = 0; edge_index < input.edge_count; ++edge_index)
  {
    game::C2S_SubtickEdge* wire_edge = out_message.add_subtick_edges();
    wire_edge->set_slot(input.edges[edge_index].slot);
    wire_edge->set_buttons_after(input.edges[edge_index].buttons_after);
    write_view_angle(*wire_edge->mutable_view_after(), input.edges[edge_index].view_after);
  }
}

std::optional<shared::subtick_input_t>
try_read_subtick_input(const game::C2S_ClientInput& message)
{
  shared::subtick_input_t input{};
  input.buttons_at_start = message.buttons_bitfield();

  // A single-angle tick is still a whole tick, not a malformed one: a bot, a
  // replay and a client that reports no per-edge aim all send one, and every
  // step then runs under it exactly as it did before the aim went sub-tick.
  input.view_at_end   = {message.viewangles().yaw(), message.viewangles().pitch()};
  input.view_at_start = message.has_viewangles_at_start()
                            ? shared::subtick_view_t{message.viewangles_at_start().yaw(),
                                                     message.viewangles_at_start().pitch()}
                            : input.view_at_end;

  shared::subtick_view_t view_in_effect = input.view_at_start;
  for (const game::C2S_SubtickEdge& wire_edge : message.subtick_edges())
  {
    if (wire_edge.has_view_after())
      view_in_effect = {wire_edge.view_after().yaw(), wire_edge.view_after().pitch()};

    if (!shared::try_append_subtick_edge(input, wire_edge.slot(), wire_edge.buttons_after(),
                                         view_in_effect))
      return std::nullopt;
  }

  return input;
}

} // namespace network

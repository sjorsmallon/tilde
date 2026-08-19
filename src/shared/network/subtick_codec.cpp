#include "subtick_codec.hpp"

namespace network
{

void write_subtick_input(game::C2S_PlayerMoveCommand& move,
                         const shared::subtick_input_t& input)
{
  move.set_buttons_bitfield(input.buttons_at_start);
  move.clear_subtick_edges();

  for (uint32_t edge_index = 0; edge_index < input.edge_count; ++edge_index)
  {
    game::C2S_SubtickEdge* wire_edge = move.add_subtick_edges();
    wire_edge->set_slot(input.edges[edge_index].slot);
    wire_edge->set_buttons_after(input.edges[edge_index].buttons_after);
  }
}

std::optional<shared::subtick_input_t>
try_read_subtick_input(const game::C2S_PlayerMoveCommand& move)
{
  shared::subtick_input_t input{};
  input.buttons_at_start = move.buttons_bitfield();

  for (const game::C2S_SubtickEdge& wire_edge : move.subtick_edges())
  {
    if (!shared::try_append_subtick_edge(input, wire_edge.slot(), wire_edge.buttons_after()))
      return std::nullopt;
  }

  return input;
}

} // namespace network

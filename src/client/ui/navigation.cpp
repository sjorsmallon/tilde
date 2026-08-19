// Directional focus resolution and hit-testing. GPU-free; ui_test compiles it
// directly, which is what lets the two-column navigation case be a unit test
// rather than something you check by playing the game.

#include "navigation.hpp"

#include <cmath>

namespace client::ui
{

namespace
{

// How much lateral drift costs relative to distance along the axis. Above 1 so
// a well-aligned far candidate loses to a slightly-offset near one, which is
// what makes a column navigate like a column even when its rows are not the
// same width.
constexpr float LATERAL_PENALTY = 2.0f;

// Rows whose centres differ by less than this are the same row as far as
// navigation is concerned, so "down" never lands on a sibling beside you.
constexpr float SAME_POSITION_EPSILON = 0.5f;

[[nodiscard]] linalg::vec2 direction_vector(nav_direction_t direction)
{
  switch (direction)
  {
  case nav_direction_t::up:
    return {0.0f, -1.0f};
  case nav_direction_t::down:
    return {0.0f, 1.0f};
  case nav_direction_t::left:
    return {-1.0f, 0.0f};
  case nav_direction_t::right:
    return {1.0f, 0.0f};
  }

  return {0.0f, 0.0f};
}

// Signed distance along the direction, and unsigned drift across it.
struct separation_t
{
  float along   = 0.0f;
  float lateral = 0.0f;
};

[[nodiscard]] separation_t separation(linalg::vec2 from, linalg::vec2 to, nav_direction_t direction)
{
  const linalg::vec2 axis  = direction_vector(direction);
  const linalg::vec2 delta = {to.x - from.x, to.y - from.y};

  separation_t result;
  result.along = delta.x * axis.x + delta.y * axis.y;
  // The perpendicular of (x,y) is (-y,x); its dot with the delta is the drift.
  result.lateral = std::fabs(delta.x * -axis.y + delta.y * axis.x);
  return result;
}

} // namespace

ui_node_id_t find_neighbour(const ui_screen_t &screen, ui_node_id_t from, nav_direction_t direction)
{
  if (from == UI_INVALID_NODE_ID || from >= screen.nodes.size())
    return UI_INVALID_NODE_ID;

  const linalg::vec2 origin = resolve_node(screen, from).rect.center();

  ui_node_id_t best       = UI_INVALID_NODE_ID;
  float        best_score = 0.0f;

  for (uint32_t index = 0; index < screen.nodes.size(); ++index)
  {
    const ui_node_id_t candidate = (ui_node_id_t)index;
    if (candidate == from || !screen[candidate].focusable)
      continue;

    const separation_t gap = separation(origin, resolve_node(screen, candidate).rect.center(), direction);
    if (gap.along <= SAME_POSITION_EPSILON)
      continue; // behind, or level with, the node we are leaving

    const float score = gap.along + gap.lateral * LATERAL_PENALTY;
    if (best == UI_INVALID_NODE_ID || score < best_score)
    {
      best       = candidate;
      best_score = score;
    }
  }

  return best;
}

ui_node_id_t find_neighbour_wrapping(const ui_screen_t &screen, ui_node_id_t from,
                                     nav_direction_t direction)
{
  const ui_node_id_t neighbour = find_neighbour(screen, from, direction);
  if (neighbour != UI_INVALID_NODE_ID)
    return neighbour;

  if (from == UI_INVALID_NODE_ID || from >= screen.nodes.size())
    return UI_INVALID_NODE_ID;

  // At an edge: come back around to whatever is farthest in the opposite
  // direction, so pressing Down at the bottom of a menu lands on the top row.
  const linalg::vec2 origin = resolve_node(screen, from).rect.center();

  ui_node_id_t best       = UI_INVALID_NODE_ID;
  float        best_score = 0.0f;

  for (uint32_t index = 0; index < screen.nodes.size(); ++index)
  {
    const ui_node_id_t candidate = (ui_node_id_t)index;
    if (candidate == from || !screen[candidate].focusable)
      continue;

    const separation_t gap = separation(origin, resolve_node(screen, candidate).rect.center(), direction);

    // Most negative `along` is farthest backward; lateral still breaks ties so a
    // wrap out of one column stays in that column.
    const float score = gap.along + gap.lateral * LATERAL_PENALTY;
    if (best == UI_INVALID_NODE_ID || score < best_score)
    {
      best       = candidate;
      best_score = score;
    }
  }

  return best;
}

ui_node_id_t hit_test(const ui_screen_t &screen, linalg::vec2 point)
{
  ui_node_id_t best       = UI_INVALID_NODE_ID;
  uint32_t     best_depth = 0;

  for (uint32_t index = 0; index < screen.nodes.size(); ++index)
  {
    const ui_node_id_t candidate = (ui_node_id_t)index;
    if (!screen[candidate].focusable)
      continue;

    const ui_rect_t rect = resolve_node(screen, candidate).rect;
    if (point.x < rect.min.x || point.x >= rect.max.x || point.y < rect.min.y ||
        point.y >= rect.max.y)
      continue;

    // Deepest wins, and a later sibling at equal depth wins over an earlier one
    // because it drew on top.
    const uint32_t depth = node_depth(screen, candidate);
    if (best == UI_INVALID_NODE_ID || depth >= best_depth)
    {
      best       = candidate;
      best_depth = depth;
    }
  }

  return best;
}

ui_node_id_t first_focusable(const ui_screen_t &screen)
{
  for (uint32_t index = 0; index < screen.nodes.size(); ++index)
  {
    if (screen[(ui_node_id_t)index].focusable)
      return (ui_node_id_t)index;
  }

  return UI_INVALID_NODE_ID;
}

} // namespace client::ui

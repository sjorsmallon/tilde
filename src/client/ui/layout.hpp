#pragma once

// Screen-space rectangles, and the one operation a HUD actually needs: pin a
// box of known size to a corner or an edge, some margin in.
//
// This is deliberately NOT a layout system. There is no widget, no tree, no
// cursor and no push/pop, because a HUD is a handful of elements whose positions
// are decisions, not the output of a solver. What it removes is the arithmetic
// that puts a sign error in one corner out of four -- see ui_def.md.
//
// Everything here is pure and header-only: no state, no allocation, no ordering.

#include "../../shared/linalg.hpp"

#include <cstdint>

namespace client::ui
{

// Framebuffer pixels, origin top-left -- the same space ui_draw_list_t works in.
// A distinct type from shared::aabb_t because that one is 3D and world-space;
// converting between them is never right.
struct ui_rect_t
{
  linalg::vec2 min;
  linalg::vec2 max;

  [[nodiscard]] constexpr linalg::vec2 size() const { return {max.x - min.x, max.y - min.y}; }
  [[nodiscard]] constexpr linalg::vec2 center() const
  {
    return {(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f};
  }
};

enum class anchor_t : uint8_t
{
  top_left,
  top_center,
  top_right,
  center_left,
  center,
  center_right,
  bottom_left,
  bottom_center,
  bottom_right
};

// How big the box is and how far in it sits. Paired so both are NAMED at the
// call site: as two adjacent vec2 arguments they are silently swappable, and a
// swap compiles and puts a plausible box in the wrong place.
struct placement_t
{
  linalg::vec2 margin = {0.0f, 0.0f};
  linalg::vec2 size;
};

// Place a `placement.size` box against `anchor` of a `screen`-sized area,
// `placement.margin` pixels in from whichever edges the anchor names. A margin
// on a centered axis is ignored -- "16px in from the middle" is not a thing
// anyone means.
[[nodiscard]] constexpr ui_rect_t anchored(linalg::vec2 screen, anchor_t anchor,
                                           placement_t placement)
{
  const linalg::vec2 margin = placement.margin;
  const linalg::vec2 size   = placement.size;

  const uint32_t column = (uint32_t)anchor % 3u; // 0 = left,  1 = center, 2 = right
  const uint32_t row    = (uint32_t)anchor / 3u; // 0 = top,   1 = center, 2 = bottom

  float x = margin.x;
  if (column == 1)
    x = (screen.x - size.x) * 0.5f;
  else if (column == 2)
    x = screen.x - size.x - margin.x;

  float y = margin.y;
  if (row == 1)
    y = (screen.y - size.y) * 0.5f;
  else if (row == 2)
    y = screen.y - size.y - margin.y;

  return ui_rect_t{{x, y}, {x + size.x, y + size.y}};
}

// Shrink on every side. A negative amount grows, which is how a background panel
// gets its padding from the text rect it wraps.
[[nodiscard]] constexpr ui_rect_t inset(ui_rect_t rect, float amount)
{
  return ui_rect_t{{rect.min.x + amount, rect.min.y + amount},
                   {rect.max.x - amount, rect.max.y - amount}};
}

} // namespace client::ui

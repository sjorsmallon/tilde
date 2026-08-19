#pragma once

// Focus movement and hit-testing over a ui_screen_t.
//
// NAVIGATION IS GEOMETRIC, NOT A LINEAR INDEX. "Down" means the nearest
// focusable node that is actually below this one, resolved from the layout --
// not "the next entry in the array". The extra work is one scoring loop, and
// what it buys is that a two-column options screen, a grid of loadout tiles or
// a dialog with side-by-side buttons all navigate correctly on the day they are
// built. An index-based menu has to be rewritten the first time a screen is not
// a single column, and every screen eventually is not.
//
// NOTHING HERE WRITES FOCUS. These are pure queries and the caller decides what
// to do with the answer, because moving focus is a policy (does the pointer move
// it? does an edge wrap?) and a screen's business.

#include "screen.hpp"

namespace client::ui
{

enum class nav_direction_t : uint8_t
{
  up,
  down,
  left,
  right
};

inline constexpr uint32_t NAV_DIRECTION_COUNT = 4;

} // namespace client::ui

// Global scope, beside enum_traits' other specializations: see font.hpp for why.
template <> struct enum_traits<client::ui::nav_direction_t>
{
  static constexpr uint32_t count = client::ui::NAV_DIRECTION_COUNT;
};

namespace client::ui
{

// The nearest focusable node in `direction` from `from`, or UI_INVALID_NODE_ID at an
// edge. Candidates must lie in a cone around the axis, scored by distance along
// it plus a penalty for lateral drift -- so a slightly-offset row below still
// wins over a perfectly-aligned one three rows down.
[[nodiscard]] ui_node_id_t find_neighbour(const ui_screen_t &screen, ui_node_id_t from,
                                          nav_direction_t direction);

// find_neighbour, but an edge wraps to the FARTHEST focusable node in the
// opposite direction rather than returning nothing. Wrapping is a policy, which
// is why it is a separate function and not a bool: a menu wants it, a slider row
// or a paged list does not.
[[nodiscard]] ui_node_id_t find_neighbour_wrapping(const ui_screen_t &screen, ui_node_id_t from,
                                                   nav_direction_t direction);

// The deepest focusable node whose resolved rect contains `point`, in
// framebuffer pixels; UI_INVALID_NODE_ID for a miss. Depth breaks ties so a focusable
// child inside a focusable panel wins, though nesting focusables is a design
// error rather than a supported layout.
//
// This is also the whole of hover, which is why hover is stored nowhere.
[[nodiscard]] ui_node_id_t hit_test(const ui_screen_t &screen, linalg::vec2 point);

// The first focusable node in creation order, for seeding focus on entry.
// UI_INVALID_NODE_ID when a screen has nothing to focus, which is legitimate -- a
// title card is a screen too.
[[nodiscard]] ui_node_id_t first_focusable(const ui_screen_t &screen);

} // namespace client::ui

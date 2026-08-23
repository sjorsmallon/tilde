#pragma once

// A vertical list of labelled rows with a sliding focus highlight -- the shape
// the main menu and the pause menu both are.
//
// It exists because the second one of these was about to be a copy of the first:
// the build, the two bound passes, the highlight tween and the nav/hover/activate
// step are identical, and the only things that actually differ are WHERE the
// block sits, WHAT the rows say and WHAT activating one does. So the first two
// are a list_menu_style_t plus a label list, and the third stays entirely at the
// call site -- this file never knows what a row means.
//
// The three-owner rule from ui_def.md holds inside here, which is why there are
// separate entry points rather than one update(): AUTHORED structure comes out
// of build_list_menu once, BOUND rects and colours are rewritten every frame by
// advance_list_menu, and the highlight offset is ANIMATED. A caller with its own
// bound values (the main menu's server address) writes them beside the call.

#include "../../shared/color.hpp"
#include "../../shared/span.hpp"
#include "font.hpp"
#include "layout.hpp"
#include "navigation.hpp"
#include "screen.hpp"
#include "ui_input.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace client::ui
{

// Everything a menu differs on that is not its rows or what they do. Every
// member has a default, so a screen names only what it cares about.
// Every length below is in LOGICAL units -- what the sizes meant back when a
// pixel was a pixel. build_list_menu multiplies them by the display scale, so a
// screen authors one set of numbers and they hold at 96 and 192 DPI alike.
struct list_menu_style_t
{
  // Where the block of rows sits, and how big it is.
  anchor_t     anchor     = anchor_t::center_left;
  linalg::vec2 margin     = {76.0f, 0.0f};
  float        width      = 460.0f;
  float        row_height = 42.0f;

  // Room at the left edge of a row for the highlight bar to occupy.
  float label_inset         = 22.0f;
  float highlight_bar_width = 4.0f;

  font_size_t  label_size  = font_size_t::medium;
  text_align_t label_align = text_align_t::left;
  font_size_t  value_size  = font_size_t::small;

  color_t row_idle_color      = {170, 180, 188, 255};
  color_t row_focused_color   = {255, 255, 255, 255};
  color_t row_value_color     = {118, 138, 148, 255};
  color_t highlight_bar_color = {120, 200, 220, 255};

  // A fully transparent backdrop means NO backdrop node at all, rather than an
  // invisible one: a pause menu wants the game behind it dimmed, a main menu
  // over an empty frame has nothing to dim.
  color_t backdrop_color = {0, 0, 0, 0};

  float intro_fade_seconds      = 0.25f;
  float highlight_slide_seconds = 0.12f;

  // POLICY, not a fact about the type (see ui_def.md): does moving the pointer
  // take focus? Both menus say yes, so there is one highlight and one meaning
  // for activate. A list with per-row sub-controls would say no.
  bool pointer_moves_focus = true;

  // Does navigating off the top or bottom wrap to the other end?
  bool navigation_wraps = true;
};

// The screen plus the handles into it. Held together for the reason ui_screen_t
// holds its own three things together: an id means nothing except against the
// nodes it was minted from.
//
//   root (whole framebuffer)
//   ├── backdrop      (optional solid, whole framebuffer)
//   └── panel         (the anchored block)
//       ├── highlight (solid bar, tracks the focused row)
//       └── row i     (label text, focusable)
//           └── value i (right-aligned secondary text, empty unless written)
struct list_menu_t
{
  ui_screen_t screen;

  ui_node_id_t backdrop            = UI_INVALID_NODE_ID;
  ui_node_id_t panel               = UI_INVALID_NODE_ID;
  ui_node_id_t highlight_indicator = UI_INVALID_NODE_ID;

  std::vector<ui_node_id_t> rows;
  std::vector<ui_node_id_t> row_values;

  list_menu_style_t style;

  [[nodiscard]] uint32_t row_count() const { return (uint32_t)rows.size(); }
};

// Build the whole thing as a VALUE: nodes, focus on the first row, a first
// layout, and the intro fade. Callers assign the result wholesale and never edit
// it structurally -- that is what keeps every id in range with no check anywhere.
//
// `labels` is borrowed only for the duration of the call; the text is copied
// into the nodes.
// `display_scale` converts the style's LOGICAL lengths into the framebuffer
// pixels every rect is in -- renderer::display_scale() at the call site. It is
// applied ONCE, here, and the scaled style is what lands in list_menu_t::style,
// so every pass after this one is already in pixels and cannot forget.
[[nodiscard]] list_menu_t build_list_menu(Span<const char *const> labels,
                                          const list_menu_style_t &style,
                                          linalg::vec2             screen_size,
                                          float                    display_scale);

// The nav / hover / activate step, and the ONLY one that reads input. Returns
// the row that was activated this frame, if any -- not a failure channel, which
// is why there is no try_ on it.
//
// Deliberately does not touch layout or colours: the caller runs those after,
// with advance_list_menu, so a focus change made here is on screen the same
// frame.
[[nodiscard]] std::optional<uint32_t>
update_list_menu(list_menu_t &menu, const ui_input_t &input, linalg::vec2 screen_size);

// The per-frame bound passes: advance the tweens, rewrite every rect from the
// live screen size, rewrite every tint from the focus. All unconditional, which
// is what makes staleness unrepresentable rather than merely discouraged.
void advance_list_menu(list_menu_t &menu, float delta_seconds, linalg::vec2 screen_size);

// Secondary text on one row, right-aligned against the panel's right edge. Bound
// like everything else: call it every frame from whatever it mirrors, never once
// at build time.
void write_list_menu_row_value(list_menu_t &menu, uint32_t row_index, std::string_view text);

// Which row `node` is, or nothing when it is not a row (the panel, the backdrop,
// or UI_INVALID_NODE_ID from a hit-test miss).
[[nodiscard]] std::optional<uint32_t> try_row_index_for_node(const list_menu_t &menu,
                                                             ui_node_id_t       node);

// Move focus and re-aim the highlight bar's slide. Exposed because a screen may
// want to place focus itself (restoring a remembered row, say); the normal path
// is update_list_menu calling it.
void move_list_menu_focus_to(list_menu_t &menu, ui_node_id_t node, linalg::vec2 screen_size);

} // namespace client::ui

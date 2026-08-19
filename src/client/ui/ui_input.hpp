#pragma once

#include "../../shared/array.hpp"
#include "../../shared/linalg.hpp"
#include "navigation.hpp"

namespace client::ui
{

struct ui_input_t
{
  // Framebuffer pixels (which seems obvious but high dpi expresses in points.)
  linalg::vec2 pointer_position = {0.0f, 0.0f};

  // Whether the pointer actually moved this frame. Hover may only steal focus
  // when it did, or a cursor resting over one row fights every arrow key press.
  bool pointer_moved = false;

  bool pointer_activate = false; // left button went down this frame

  Enum_Array<nav_direction_t, bool> navigate = {};

  bool activate = false; // Enter / Space
  bool cancel   = false; // Escape
};

// Translate this frame's raw input. Call once per frame, per screen that wants
// it.
//
// Two things it arbitrates, both of which exist because ImGui is still on screen
// for the console and the editor:
//   * under input::imgui_wants_keyboard() the key actions are dropped, so typing in
//     the console does not also drive the menu behind it;
//   * under input::imgui_wants_mouse() the click is dropped, so an ImGui window
//     cannot be clicked THROUGH.
[[nodiscard]] ui_input_t gather_ui_input();

} // namespace client::ui

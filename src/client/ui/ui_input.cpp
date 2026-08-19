// The one translation from raw device input to UI actions.

#include "ui_input.hpp"

#include "../input.hpp"
#include "../renderer.hpp"

namespace client::ui
{

ui_input_t gather_ui_input()
{
  ui_input_t result;

  // input::mouse_position() is SDL_GetMouseState -- LOGICAL WINDOW POINTS. The
  // draw list and screen_size() are FRAMEBUFFER PIXELS, and the window carries
  // SDL_WINDOW_ALLOW_HIGHDPI, so on a scaled display those are different
  // numbers and every hit-test would sit at an offset from the text it belongs
  // to. The renderer owns the window, so it owns the conversion.
  const linalg::vec2i pointer = input::mouse_position();
  result.pointer_position = renderer::logical_window_points_to_framebuffer_pixels({(float)pointer.x, (float)pointer.y});

  const linalg::vec2i motion = input::mouse_delta();
  result.pointer_moved = motion.x != 0 || motion.y != 0;

  // An ImGui window under the cursor claims the click, so a console or a debug
  // panel cannot be clicked through to the screen behind it.
  if (!input::imgui_wants_mouse())
    result.pointer_activate = input::is_mouse_pressed(input::mouse_button_t::Left);

  // Likewise for keys: with the console open, arrows and Enter belong to the
  // text field, not to the menu behind it.
  if (input::imgui_wants_keyboard())
    return result;

  // Edge-triggered, not held. A menu that scrolled while you leaned on Down
  // would need a repeat delay to be usable, and nothing here has asked for one
  // yet.
  result.navigate[nav_direction_t::up] =
      input::is_key_pressed(input::key_t::Arrow_Up) || input::is_key_pressed(input::key_t::W);
  result.navigate[nav_direction_t::down] =
      input::is_key_pressed(input::key_t::Arrow_Down) || input::is_key_pressed(input::key_t::S);
  result.navigate[nav_direction_t::left] =
      input::is_key_pressed(input::key_t::Arrow_Left) || input::is_key_pressed(input::key_t::A);
  result.navigate[nav_direction_t::right] =
      input::is_key_pressed(input::key_t::Arrow_Right) || input::is_key_pressed(input::key_t::D);

  result.activate =
      input::is_key_pressed(input::key_t::Enter) || input::is_key_pressed(input::key_t::Space);
  result.cancel = input::is_key_pressed(input::key_t::Escape);

  return result;
}

} // namespace client::ui

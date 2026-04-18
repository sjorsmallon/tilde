#include "input.hpp"
#include <SDL.h>
#include <cstring>

namespace client {
namespace input {

static int g_mouse_delta_x = 0;
static int g_mouse_delta_y = 0;
static float g_scroll_delta = 0.0f;
static Uint8 g_prev_keyboard_state[SDL_NUM_SCANCODES];
static Uint8 g_curr_keyboard_state[SDL_NUM_SCANCODES];

void new_frame() {
  // Mouse delta is retrieved via SDL_GetRelativeMouseState which resets it.
  // However, if we want to query it multiple times per frame, we should cache
  // it. Or just wrap SDL_GetRelativeMouseState. BUT SDL_GetRelativeMouseState
  // only works if relative mode is enabled? No, it returns delta since last
  // call. So we must call it EXACTLY ONCE per frame to catch all movement.

  SDL_GetRelativeMouseState(&g_mouse_delta_x, &g_mouse_delta_y);
  g_scroll_delta = 0.0f;

  // Update keyboard state for "pressed" detection
  const Uint8 *state = SDL_GetKeyboardState(nullptr);
  std::memcpy(g_prev_keyboard_state, g_curr_keyboard_state, SDL_NUM_SCANCODES);
  std::memcpy(g_curr_keyboard_state, state, SDL_NUM_SCANCODES);
}

void process_event(const void *event) {
  const SDL_Event *e = static_cast<const SDL_Event *>(event);
  if (e->type == SDL_MOUSEWHEEL)
  {
    g_scroll_delta += static_cast<float>(e->wheel.y);
  }
}

bool is_key_down(int scancode) {
  if (scancode < 0 || scancode >= SDL_NUM_SCANCODES)
    return false;
  return g_curr_keyboard_state[scancode] != 0;
}

bool is_key_pressed(int scancode) {
  if (scancode < 0 || scancode >= SDL_NUM_SCANCODES)
    return false;
  return g_curr_keyboard_state[scancode] && !g_prev_keyboard_state[scancode];
}

bool is_mouse_down(int button) {
  return (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(button)) != 0;
}

void get_mouse_pos(int *x, int *y) { SDL_GetMouseState(x, y); }

void get_mouse_delta(int *x, int *y) {
  if (x)
    *x = g_mouse_delta_x;
  if (y)
    *y = g_mouse_delta_y;
}

float get_scroll_delta() { return g_scroll_delta; }

void set_relative_mouse_mode(bool enabled) {
  SDL_SetRelativeMouseMode(enabled ? SDL_TRUE : SDL_FALSE);
}

} // namespace input
} // namespace client

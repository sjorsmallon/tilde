#include "input.hpp"

#include <SDL.h>
#include "imgui.h"

#include <array>
#include <cstring>
#include <vector>

namespace client::input {

namespace {

// --- Key <-> SDL scancode tables --------------------------------------------
//
// One bidirectional source of truth: each row pairs a Key with its SDL
// scancode. Both lookup tables (Key -> scancode, scancode -> Key) are built
// from this list at compile time, so they can never drift.

constexpr size_t key_count = static_cast<size_t>(Key::Count);

struct key_mapping_t {
  Key key;
  int scancode;
};

constexpr key_mapping_t key_mappings[] = {
  // Letters
  {Key::A, SDL_SCANCODE_A}, {Key::B, SDL_SCANCODE_B},
  {Key::C, SDL_SCANCODE_C}, {Key::D, SDL_SCANCODE_D},
  {Key::E, SDL_SCANCODE_E}, {Key::F, SDL_SCANCODE_F},
  {Key::G, SDL_SCANCODE_G}, {Key::H, SDL_SCANCODE_H},
  {Key::I, SDL_SCANCODE_I}, {Key::J, SDL_SCANCODE_J},
  {Key::K, SDL_SCANCODE_K}, {Key::L, SDL_SCANCODE_L},
  {Key::M, SDL_SCANCODE_M}, {Key::N, SDL_SCANCODE_N},
  {Key::O, SDL_SCANCODE_O}, {Key::P, SDL_SCANCODE_P},
  {Key::Q, SDL_SCANCODE_Q}, {Key::R, SDL_SCANCODE_R},
  {Key::S, SDL_SCANCODE_S}, {Key::T, SDL_SCANCODE_T},
  {Key::U, SDL_SCANCODE_U}, {Key::V, SDL_SCANCODE_V},
  {Key::W, SDL_SCANCODE_W}, {Key::X, SDL_SCANCODE_X},
  {Key::Y, SDL_SCANCODE_Y}, {Key::Z, SDL_SCANCODE_Z},

  // Digits (top row)
  {Key::Num0, SDL_SCANCODE_0}, {Key::Num1, SDL_SCANCODE_1},
  {Key::Num2, SDL_SCANCODE_2}, {Key::Num3, SDL_SCANCODE_3},
  {Key::Num4, SDL_SCANCODE_4}, {Key::Num5, SDL_SCANCODE_5},
  {Key::Num6, SDL_SCANCODE_6}, {Key::Num7, SDL_SCANCODE_7},
  {Key::Num8, SDL_SCANCODE_8}, {Key::Num9, SDL_SCANCODE_9},

  // Function keys
  {Key::F1,  SDL_SCANCODE_F1},  {Key::F2,  SDL_SCANCODE_F2},
  {Key::F3,  SDL_SCANCODE_F3},  {Key::F4,  SDL_SCANCODE_F4},
  {Key::F5,  SDL_SCANCODE_F5},  {Key::F6,  SDL_SCANCODE_F6},
  {Key::F7,  SDL_SCANCODE_F7},  {Key::F8,  SDL_SCANCODE_F8},
  {Key::F9,  SDL_SCANCODE_F9},  {Key::F10, SDL_SCANCODE_F10},
  {Key::F11, SDL_SCANCODE_F11}, {Key::F12, SDL_SCANCODE_F12},

  // Whitespace / control
  {Key::Space,     SDL_SCANCODE_SPACE},
  {Key::Tab,       SDL_SCANCODE_TAB},
  {Key::Enter,     SDL_SCANCODE_RETURN},
  {Key::Backspace, SDL_SCANCODE_BACKSPACE},
  {Key::Delete,    SDL_SCANCODE_DELETE},
  {Key::Escape,    SDL_SCANCODE_ESCAPE},

  // Modifiers
  {Key::LeftShift,  SDL_SCANCODE_LSHIFT},
  {Key::RightShift, SDL_SCANCODE_RSHIFT},
  {Key::LeftCtrl,   SDL_SCANCODE_LCTRL},
  {Key::RightCtrl,  SDL_SCANCODE_RCTRL},
  {Key::LeftAlt,    SDL_SCANCODE_LALT},
  {Key::RightAlt,   SDL_SCANCODE_RALT},
  {Key::LeftGui,    SDL_SCANCODE_LGUI},
  {Key::RightGui,   SDL_SCANCODE_RGUI},

  // Arrows
  {Key::ArrowLeft,  SDL_SCANCODE_LEFT},
  {Key::ArrowRight, SDL_SCANCODE_RIGHT},
  {Key::ArrowUp,    SDL_SCANCODE_UP},
  {Key::ArrowDown,  SDL_SCANCODE_DOWN},

  // Punctuation
  {Key::LeftBracket,  SDL_SCANCODE_LEFTBRACKET},
  {Key::RightBracket, SDL_SCANCODE_RIGHTBRACKET},
  {Key::Tilde,        SDL_SCANCODE_GRAVE},
};

constexpr std::array<int, key_count> build_key_to_scancode()
{
  std::array<int, key_count> table{};
  for (auto &slot : table) slot = SDL_SCANCODE_UNKNOWN;
  for (const auto &mapping : key_mappings)
    table[static_cast<size_t>(mapping.key)] = mapping.scancode;
  return table;
}

constexpr std::array<Key, SDL_NUM_SCANCODES> build_scancode_to_key()
{
  std::array<Key, SDL_NUM_SCANCODES> table{};
  for (auto &slot : table) slot = Key::Unknown;
  for (const auto &mapping : key_mappings)
    if (mapping.scancode > 0 && mapping.scancode < SDL_NUM_SCANCODES)
      table[mapping.scancode] = mapping.key;
  return table;
}

constexpr auto g_key_to_scancode = build_key_to_scancode();
constexpr auto g_scancode_to_key = build_scancode_to_key();

Key scancode_to_key(int scancode)
{
  if (scancode <= 0 || scancode >= SDL_NUM_SCANCODES)
    return Key::Unknown;
  return g_scancode_to_key[scancode];
}

MouseButton sdl_button_to_mouse_button(uint8_t sdl_button)
{
  switch (sdl_button) {
    case SDL_BUTTON_LEFT:   return MouseButton::Left;
    case SDL_BUTTON_MIDDLE: return MouseButton::Middle;
    case SDL_BUTTON_RIGHT:  return MouseButton::Right;
    case SDL_BUTTON_X1:     return MouseButton::X1;
    case SDL_BUTTON_X2:     return MouseButton::X2;
    default:                return MouseButton::Count;
  }
}

uint32_t mouse_button_to_sdl_mask(MouseButton button)
{
  switch (button) {
    case MouseButton::Left:   return SDL_BUTTON(SDL_BUTTON_LEFT);
    case MouseButton::Middle: return SDL_BUTTON(SDL_BUTTON_MIDDLE);
    case MouseButton::Right:  return SDL_BUTTON(SDL_BUTTON_RIGHT);
    case MouseButton::X1:     return SDL_BUTTON(SDL_BUTTON_X1);
    case MouseButton::X2:     return SDL_BUTTON(SDL_BUTTON_X2);
    default:                  return 0;
  }
}

Modifiers modifiers_from_sdl_keymod(uint16_t sdl_mod)
{
  Modifiers result{};
  result.shift = (sdl_mod & KMOD_SHIFT) != 0;
  result.ctrl  = (sdl_mod & KMOD_CTRL)  != 0;
  result.alt   = (sdl_mod & KMOD_ALT)   != 0;
  result.gui   = (sdl_mod & KMOD_GUI)   != 0;
  return result;
}

// --- Per-frame state ---------------------------------------------------------

std::array<bool, key_count> g_prev_key_down{};
std::array<bool, key_count> g_curr_key_down{};

int g_mouse_delta_x = 0;
int g_mouse_delta_y = 0;
float g_scroll_delta = 0.0f;

std::vector<KeyEvent> g_key_events;
std::vector<MouseButtonEvent> g_mouse_button_events;

} // namespace

void new_frame()
{
  SDL_GetRelativeMouseState(&g_mouse_delta_x, &g_mouse_delta_y);
  g_scroll_delta = 0.0f;

  // Walk SDL's keyboard state once and project into our Key-indexed arrays.
  const Uint8 *sdl_state = SDL_GetKeyboardState(nullptr);
  g_prev_key_down = g_curr_key_down;
  for (size_t i = 0; i < key_count; ++i) {
    int scancode = g_key_to_scancode[i];
    g_curr_key_down[i] = (scancode > 0) && (sdl_state[scancode] != 0);
  }

  g_key_events.clear();
  g_mouse_button_events.clear();
}

void process_sdl_event(const void *sdl_event)
{
  const SDL_Event *event = static_cast<const SDL_Event *>(sdl_event);
  switch (event->type) {
    case SDL_KEYDOWN: {
      Key key = scancode_to_key(event->key.keysym.scancode);
      if (key == Key::Unknown) return;
      KeyEvent key_event{};
      key_event.key = key;
      key_event.mods = modifiers_from_sdl_keymod(event->key.keysym.mod);
      key_event.repeat = event->key.repeat != 0;
      g_key_events.push_back(key_event);
      break;
    }
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP: {
      MouseButton button = sdl_button_to_mouse_button(event->button.button);
      if (button == MouseButton::Count) return;
      MouseButtonEvent button_event{};
      button_event.button = button;
      button_event.action = (event->type == SDL_MOUSEBUTTONDOWN)
                                ? MouseAction::Down
                                : MouseAction::Up;
      button_event.position = {event->button.x, event->button.y};
      button_event.mods = modifiers_from_sdl_keymod(SDL_GetModState());
      g_mouse_button_events.push_back(button_event);
      break;
    }
    case SDL_MOUSEWHEEL:
      g_scroll_delta += static_cast<float>(event->wheel.y);
      break;
    default:
      break;
  }
}

bool is_key_down(Key key)
{
  size_t index = static_cast<size_t>(key);
  if (index >= key_count) return false;
  return g_curr_key_down[index];
}

bool is_key_pressed(Key key)
{
  size_t index = static_cast<size_t>(key);
  if (index >= key_count) return false;
  return g_curr_key_down[index] && !g_prev_key_down[index];
}

bool is_mouse_down(MouseButton button)
{
  uint32_t mask = mouse_button_to_sdl_mask(button);
  if (mask == 0) return false;
  return (SDL_GetMouseState(nullptr, nullptr) & mask) != 0;
}

Modifiers current_modifiers()
{
  return modifiers_from_sdl_keymod(static_cast<uint16_t>(SDL_GetModState()));
}

linalg::vec2i mouse_position()
{
  int x = 0;
  int y = 0;
  SDL_GetMouseState(&x, &y);
  return {x, y};
}

linalg::vec2i mouse_delta()
{
  return {g_mouse_delta_x, g_mouse_delta_y};
}

float scroll_delta() { return g_scroll_delta; }

void set_relative_mouse_mode(bool enabled)
{
  SDL_SetRelativeMouseMode(enabled ? SDL_TRUE : SDL_FALSE);
}

std::span<const KeyEvent> frame_key_events()
{
  return g_key_events;
}

std::span<const MouseButtonEvent> frame_mouse_button_events()
{
  return g_mouse_button_events;
}

bool ui_wants_mouse()
{
  return ImGui::GetIO().WantCaptureMouse;
}

bool ui_wants_keyboard()
{
  return ImGui::GetIO().WantCaptureKeyboard;
}

} // namespace client::input

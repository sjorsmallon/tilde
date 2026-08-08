#include "input.hpp"

#include "imgui.h"
#include <SDL.h>

#include <array>
#include <cstring>
#include <vector>

namespace client::input
{

namespace
{

// --- key_t <-> SDL scancode tables -------------------------------------------
//
// One bidirectional source of truth: each row pairs a key_t with its SDL
// scancode. Both lookup tables (key_t -> scancode, scancode -> key_t) are built
// from this list at compile time, so they can never drift.

constexpr size_t key_count = static_cast<size_t>(key_t::Count);
constexpr size_t mouse_button_count = static_cast<size_t>(mouse_button_t::Count);

struct key_mapping_t
{
  key_t key;
  int scancode;
};

constexpr key_mapping_t key_mappings[] = {
    // Letters
    {key_t::A, SDL_SCANCODE_A}, {key_t::B, SDL_SCANCODE_B},
    {key_t::C, SDL_SCANCODE_C}, {key_t::D, SDL_SCANCODE_D},
    {key_t::E, SDL_SCANCODE_E}, {key_t::F, SDL_SCANCODE_F},
    {key_t::G, SDL_SCANCODE_G}, {key_t::H, SDL_SCANCODE_H},
    {key_t::I, SDL_SCANCODE_I}, {key_t::J, SDL_SCANCODE_J},
    {key_t::K, SDL_SCANCODE_K}, {key_t::L, SDL_SCANCODE_L},
    {key_t::M, SDL_SCANCODE_M}, {key_t::N, SDL_SCANCODE_N},
    {key_t::O, SDL_SCANCODE_O}, {key_t::P, SDL_SCANCODE_P},
    {key_t::Q, SDL_SCANCODE_Q}, {key_t::R, SDL_SCANCODE_R},
    {key_t::S, SDL_SCANCODE_S}, {key_t::T, SDL_SCANCODE_T},
    {key_t::U, SDL_SCANCODE_U}, {key_t::V, SDL_SCANCODE_V},
    {key_t::W, SDL_SCANCODE_W}, {key_t::X, SDL_SCANCODE_X},
    {key_t::Y, SDL_SCANCODE_Y}, {key_t::Z, SDL_SCANCODE_Z},

    // Digits (top row)
    {key_t::Num_0, SDL_SCANCODE_0}, {key_t::Num_1, SDL_SCANCODE_1},
    {key_t::Num_2, SDL_SCANCODE_2}, {key_t::Num_3, SDL_SCANCODE_3},
    {key_t::Num_4, SDL_SCANCODE_4}, {key_t::Num_5, SDL_SCANCODE_5},
    {key_t::Num_6, SDL_SCANCODE_6}, {key_t::Num_7, SDL_SCANCODE_7},
    {key_t::Num_8, SDL_SCANCODE_8}, {key_t::Num_9, SDL_SCANCODE_9},

    // Function keys
    {key_t::F1, SDL_SCANCODE_F1}, {key_t::F2, SDL_SCANCODE_F2},
    {key_t::F3, SDL_SCANCODE_F3}, {key_t::F4, SDL_SCANCODE_F4},
    {key_t::F5, SDL_SCANCODE_F5}, {key_t::F6, SDL_SCANCODE_F6},
    {key_t::F7, SDL_SCANCODE_F7}, {key_t::F8, SDL_SCANCODE_F8},
    {key_t::F9, SDL_SCANCODE_F9}, {key_t::F10, SDL_SCANCODE_F10},
    {key_t::F11, SDL_SCANCODE_F11}, {key_t::F12, SDL_SCANCODE_F12},

    // Whitespace / control
    {key_t::Space, SDL_SCANCODE_SPACE},
    {key_t::Tab, SDL_SCANCODE_TAB},
    {key_t::Enter, SDL_SCANCODE_RETURN},
    {key_t::Backspace, SDL_SCANCODE_BACKSPACE},
    {key_t::Delete, SDL_SCANCODE_DELETE},
    {key_t::Escape, SDL_SCANCODE_ESCAPE},

    // Modifiers
    {key_t::Left_Shift, SDL_SCANCODE_LSHIFT},
    {key_t::Right_Shift, SDL_SCANCODE_RSHIFT},
    {key_t::Left_Ctrl, SDL_SCANCODE_LCTRL},
    {key_t::Right_Ctrl, SDL_SCANCODE_RCTRL},
    {key_t::Left_Alt, SDL_SCANCODE_LALT},
    {key_t::Right_Alt, SDL_SCANCODE_RALT},
    {key_t::Left_Gui, SDL_SCANCODE_LGUI},
    {key_t::Right_Gui, SDL_SCANCODE_RGUI},

    // Arrows
    {key_t::Arrow_Left, SDL_SCANCODE_LEFT},
    {key_t::Arrow_Right, SDL_SCANCODE_RIGHT},
    {key_t::Arrow_Up, SDL_SCANCODE_UP},
    {key_t::Arrow_Down, SDL_SCANCODE_DOWN},

    // Punctuation
    {key_t::Left_Bracket, SDL_SCANCODE_LEFTBRACKET},
    {key_t::Right_Bracket, SDL_SCANCODE_RIGHTBRACKET},
    {key_t::Tilde, SDL_SCANCODE_GRAVE},
};

constexpr std::array<int, key_count> build_key_to_scancode()
{
  std::array<int, key_count> table{};
  for (auto &slot : table)
    slot = SDL_SCANCODE_UNKNOWN;
  for (const auto &mapping : key_mappings)
    table[static_cast<size_t>(mapping.key)] = mapping.scancode;
  return table;
}

constexpr std::array<key_t, SDL_NUM_SCANCODES> build_scancode_to_key()
{
  std::array<key_t, SDL_NUM_SCANCODES> table{};
  for (auto &slot : table)
    slot = key_t::Unknown;
  for (const auto &mapping : key_mappings)
    if (mapping.scancode > 0 && mapping.scancode < SDL_NUM_SCANCODES)
      table[mapping.scancode] = mapping.key;
  return table;
}

constexpr auto g_key_to_scancode = build_key_to_scancode();
constexpr auto g_scancode_to_key = build_scancode_to_key();

key_t scancode_to_key(int scancode)
{
  if (scancode <= 0 || scancode >= SDL_NUM_SCANCODES)
    return key_t::Unknown;
  return g_scancode_to_key[scancode];
}

mouse_button_t sdl_button_to_mouse_button(uint8_t sdl_button)
{
  switch (sdl_button)
  {
  case SDL_BUTTON_LEFT:   return mouse_button_t::Left;
  case SDL_BUTTON_MIDDLE: return mouse_button_t::Middle;
  case SDL_BUTTON_RIGHT:  return mouse_button_t::Right;
  case SDL_BUTTON_X1:     return mouse_button_t::X1;
  case SDL_BUTTON_X2:     return mouse_button_t::X2;
  default:                return mouse_button_t::Count;
  }
}

uint32_t mouse_button_to_sdl_mask(mouse_button_t button)
{
  switch (button)
  {
  case mouse_button_t::Left:   return SDL_BUTTON(SDL_BUTTON_LEFT);
  case mouse_button_t::Middle: return SDL_BUTTON(SDL_BUTTON_MIDDLE);
  case mouse_button_t::Right:  return SDL_BUTTON(SDL_BUTTON_RIGHT);
  case mouse_button_t::X1:     return SDL_BUTTON(SDL_BUTTON_X1);
  case mouse_button_t::X2:     return SDL_BUTTON(SDL_BUTTON_X2);
  default:                     return 0;
  }
}

modifiers_t modifiers_from_sdl_keymod(uint16_t sdl_mod)
{
  modifiers_t result{};
  result.shift = (sdl_mod & KMOD_SHIFT) != 0;
  result.ctrl = (sdl_mod & KMOD_CTRL) != 0;
  result.alt = (sdl_mod & KMOD_ALT) != 0;
  result.gui = (sdl_mod & KMOD_GUI) != 0;
  return result;
}

// --- Per-frame state ---------------------------------------------------------

std::array<bool, key_count> g_prev_key_down{};
std::array<bool, key_count> g_curr_key_down{};

std::array<bool, mouse_button_count> g_prev_mouse_down{};
std::array<bool, mouse_button_count> g_curr_mouse_down{};

int g_mouse_delta_x = 0;
int g_mouse_delta_y = 0;
float g_scroll_delta = 0.0f;

std::vector<key_event_t> g_key_events;
std::vector<mouse_button_event_t> g_mouse_button_events;

} // namespace

void new_frame()
{
  SDL_GetRelativeMouseState(&g_mouse_delta_x, &g_mouse_delta_y);
  g_scroll_delta = 0.0f;

  // Walk SDL's keyboard state once and project into our key_t-indexed arrays.
  const Uint8 *sdl_state = SDL_GetKeyboardState(nullptr);
  g_prev_key_down = g_curr_key_down;
  for (size_t i = 0; i < key_count; ++i)
  {
    int scancode = g_key_to_scancode[i];
    g_curr_key_down[i] = (scancode > 0) && (sdl_state[scancode] != 0);
  }

  // Same treatment for the mouse buttons, so is_mouse_pressed can answer
  // "became down this frame" without every caller keeping its own last-frame
  // copy. is_mouse_down stays a live SDL query — this pair is only the edge.
  uint32_t mouse_state = SDL_GetMouseState(nullptr, nullptr);
  g_prev_mouse_down = g_curr_mouse_down;
  for (size_t i = 0; i < mouse_button_count; ++i)
  {
    uint32_t mask = mouse_button_to_sdl_mask(static_cast<mouse_button_t>(i));
    g_curr_mouse_down[i] = (mask != 0) && ((mouse_state & mask) != 0);
  }

  g_key_events.clear();
  g_mouse_button_events.clear();
}

void process_sdl_event(const void *sdl_event)
{
  const SDL_Event *event = static_cast<const SDL_Event *>(sdl_event);
  switch (event->type)
  {
  case SDL_KEYDOWN:
  {
    key_t key = scancode_to_key(event->key.keysym.scancode);
    if (key == key_t::Unknown)
      return;
    key_event_t key_event{};
    key_event.key = key;
    key_event.mods = modifiers_from_sdl_keymod(event->key.keysym.mod);
    key_event.repeat = event->key.repeat != 0;
    g_key_events.push_back(key_event);
    break;
  }
  case SDL_MOUSEBUTTONDOWN:
  case SDL_MOUSEBUTTONUP:
  {
    mouse_button_t button = sdl_button_to_mouse_button(event->button.button);
    if (button == mouse_button_t::Count)
      return;
    mouse_button_event_t button_event{};
    button_event.button = button;
    button_event.action = (event->type == SDL_MOUSEBUTTONDOWN)
                              ? mouse_action_t::Down
                              : mouse_action_t::Up;
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

bool is_key_down(key_t key)
{
  size_t index = static_cast<size_t>(key);
  if (index >= key_count)
    return false;
  return g_curr_key_down[index];
}

bool is_key_pressed(key_t key)
{
  size_t index = static_cast<size_t>(key);
  if (index >= key_count)
    return false;
  return g_curr_key_down[index] && !g_prev_key_down[index];
}

bool is_mouse_down(mouse_button_t button)
{
  uint32_t mask = mouse_button_to_sdl_mask(button);
  if (mask == 0)
    return false;
  return (SDL_GetMouseState(nullptr, nullptr) & mask) != 0;
}

bool is_mouse_pressed(mouse_button_t button)
{
  size_t index = static_cast<size_t>(button);
  if (index >= mouse_button_count)
    return false;
  return g_curr_mouse_down[index] && !g_prev_mouse_down[index];
}

modifiers_t current_modifiers()
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

float scroll_delta()
{
  return g_scroll_delta;
}

void set_relative_mouse_mode(bool enabled)
{
  SDL_SetRelativeMouseMode(enabled ? SDL_TRUE : SDL_FALSE);
}

Span<const key_event_t> frame_key_events()
{
  return g_key_events;
}

Span<const mouse_button_event_t> frame_mouse_button_events()
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

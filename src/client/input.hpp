#pragma once

#include "../shared/linalg.hpp"
#include "../shared/span.hpp"
#include <cstdint>

namespace client::input
{

// Two complementary APIs:
//   * Polling (is_key_down, is_mouse_down, current_modifiers) for continuous
//     state like WASD movement.
//   * Frame event queues (frame_key_events, frame_mouse_button_events) for
//     one-shot actions like shortcuts and clicks. The queues are filled by
//     process_sdl_event and cleared by new_frame.

enum class key_t : uint16_t
{
  Unknown = 0,
  // Letters (kept contiguous so key_t::A + (c - 'a') works for binds).
  A, B, C, D, E, F, G, H, I, J, K, L, M,
  N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
  // Top-row digits (contiguous so key_t::Num_1 + index works).
  Num_0, Num_1, Num_2, Num_3, Num_4, Num_5, Num_6, Num_7, Num_8, Num_9,
  // Function keys.
  F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
  // Whitespace / control.
  Space, Tab, Enter, Backspace, Delete, Escape,
  // Modifiers, kept left/right-distinct because the code distinguishes them.
  Left_Shift, Right_Shift, Left_Ctrl, Right_Ctrl,
  Left_Alt, Right_Alt, Left_Gui, Right_Gui,
  // Arrows.
  Arrow_Left, Arrow_Right, Arrow_Up, Arrow_Down,
  // Punctuation.
  Left_Bracket, Right_Bracket, Tilde,
  // Keypad digits, contiguous like the top row so key_t::Keypad_0 + digit
  // works. DISTINCT from Num_*: the editor's axis views are on the keypad
  // specifically, Blender-style, and binding them to the top row would collide
  // with the tool hotkeys.
  Keypad_0, Keypad_1, Keypad_2, Keypad_3, Keypad_4,
  Keypad_5, Keypad_6, Keypad_7, Keypad_8, Keypad_9,
  Count
};

enum class mouse_button_t : uint8_t
{
  Left = 0,
  Middle,
  Right,
  X1,
  X2,
  Count
};

enum class mouse_action_t : uint8_t
{
  Down,
  Up
};

struct modifiers_t
{
  bool shift;
  bool ctrl;
  bool alt;
  bool gui;
};

struct key_event_t
{
  key_t key;
  modifiers_t mods;
  bool repeat;
};

struct mouse_button_event_t
{
  mouse_button_t button;
  mouse_action_t action;
  linalg::vec2i position;
  modifiers_t mods;
};

// A processed mouse interaction handed to editor tools. Unlike the raw
// mouse_button_event_t queue, this carries a movement delta and is dispatched
// for drags as well as clicks.
struct mouse_event_t
{
  mouse_button_t button; // which button triggered the down/up; unspecified for drags
  linalg::vec2i position;
  linalg::vec2i delta;
  modifiers_t mods;
};

// --- Lifecycle ---------------------------------------------------------------

// Call once at the start of each frame, before pumping SDL events.
void new_frame();

// Translate one SDL_Event. Takes void* so the header stays SDL-free.
void process_sdl_event(const void *sdl_event);

// --- Polling -----------------------------------------------------------------

bool is_key_down(key_t key);
bool is_key_pressed(key_t key); // true only on the frame it became down
bool is_mouse_down(mouse_button_t button);
bool is_mouse_pressed(mouse_button_t button); // true only on the frame it became down
modifiers_t current_modifiers();

// --- Mouse state -------------------------------------------------------------

linalg::vec2i mouse_position();
linalg::vec2i mouse_delta();
float scroll_delta();
void set_relative_mouse_mode(bool enabled);

// --- Frame event queues (one-shot events this frame) -------------------------

Span<const key_event_t> frame_key_events();
Span<const mouse_button_event_t> frame_mouse_button_events();

// --- ImGui capture -----------------------------------------------------------

bool imgui_wants_mouse();
bool imgui_wants_keyboard();

} // namespace client::input

#pragma once

#include "../shared/linalg.hpp"
#include <cstdint>
#include <span>

namespace client::input {

// Two complementary APIs:
//   * Polling (`is_key_down`, `is_mouse_down`, `current_modifiers`) for
//     continuous state like WASD movement.
//   * Frame event queues (`frame_key_events`, `frame_mouse_button_events`)
//     for one-shot actions like shortcuts and clicks. The queues are filled
//     by `process_sdl_event` and cleared by `new_frame`.

enum class Key : uint16_t {
  Unknown = 0,
  // Letters (kept contiguous so `Key::A + (c - 'a')` works for binds).
  A, B, C, D, E, F, G, H, I, J, K, L, M,
  N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
  // Top-row digits (contiguous so `Key::Num1 + idx` works).
  Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
  // Function keys.
  F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
  // Whitespace / control.
  Space, Tab, Enter, Backspace, Delete, Escape,
  // Modifiers, kept L/R-distinct because the code distinguishes them today.
  LeftShift, RightShift, LeftCtrl, RightCtrl,
  LeftAlt, RightAlt, LeftGui, RightGui,
  // Arrows.
  ArrowLeft, ArrowRight, ArrowUp, ArrowDown,
  // Punctuation.
  LeftBracket, RightBracket, Tilde,
  Count
};

enum class MouseButton : uint8_t {
  Left = 0,
  Middle,
  Right,
  X1,
  X2,
  Count
};

struct Modifiers {
  bool shift;
  bool ctrl;
  bool alt;
  bool gui;
};

struct KeyEvent {
  Key key;
  Modifiers mods;
  bool repeat;
};

enum class MouseAction : uint8_t { Down, Up };

struct MouseButtonEvent {
  MouseButton button;
  MouseAction action;
  linalg::vec2i position;
  Modifiers mods;
};

// --- Lifecycle ---------------------------------------------------------------

// Call once at the start of each frame, before pumping SDL events.
void new_frame();

// Translate one SDL_Event. Takes void* so the header stays SDL-free.
void process_sdl_event(const void *sdl_event);

// --- Polling -----------------------------------------------------------------

bool is_key_down(Key key);
bool is_key_pressed(Key key); // True only on the frame it became down.
bool is_mouse_down(MouseButton button);
Modifiers current_modifiers();

// --- Mouse state -------------------------------------------------------------

linalg::vec2i mouse_position();
linalg::vec2i mouse_delta();
float scroll_delta();
void set_relative_mouse_mode(bool enabled);

// --- Frame event queues (one-shot events this frame) ------------------------

std::span<const KeyEvent> frame_key_events();
std::span<const MouseButtonEvent> frame_mouse_button_events();

// --- UI capture (forwarded from ImGui) --------------------------------------

bool ui_wants_mouse();
bool ui_wants_keyboard();

} // namespace client::input

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

// One button transition, with the time it ARRIVED.
//
// A THIRD queue, beside the two above, and the difference is what it is for.
// frame_key_events / frame_mouse_button_events answer "what happened this
// frame" for shortcuts and clicks: keys are presses only, and neither carries a
// time, because a menu does not care WHEN inside the frame you clicked.
// Sub-tick movement is the caller that does -- the press happened at a moment,
// and which side of a tick boundary that moment fell on is the whole point (see
// shared/subtick.hpp). So this one carries RELEASES too, spans both devices in
// one arrival-ordered list, and keeps the timestamp.
//
// The timestamp is a QueryPerformanceCounter reading taken by the raw-input
// thread the instant it woke (raw_input_win32.hpp), so it is the arrival moment
// to microseconds -- three orders of magnitude below a 0.26 ms sub-tick slot.
// It is NOT SDL's event timestamp, which was measured to be pump time and
// therefore constant across a frame. When the raw-input thread could not start
// this falls back to SDL's transitions stamped at the frame boundary, which is
// one frame coarse and exactly what it was before.
enum class input_device_t : uint8_t
{
  Key,
  Mouse_Button,
  // Mouse TRAVEL, in the same arrival-ordered list as the transitions, because
  // the ordering between them is the whole point: the aim a shot is taken
  // through is the motion that arrived BEFORE the trigger and not the motion
  // that arrived after it. Folding a frame's travel into one delta (which
  // input::mouse_delta() still does, for the editor and the UI) throws that
  // ordering away and is what left a flick shot pointed where the mouse
  // finished rather than where it was when the button went down.
  Mouse_Motion
};

struct input_edge_t
{
  input_device_t device = input_device_t::Key;
  key_t          key    = key_t::Unknown;        // device == Key
  mouse_button_t button = mouse_button_t::Count; // device == Mouse_Button
  bool           down   = false;
  linalg::vec2i  motion = {0, 0};                // device == Mouse_Motion
  uint64_t       arrival_qpc_ticks = 0;
};

// The window this frame's edges arrived in: the previous drain to this one.
// Both endpoints are read at the same point in the frame, so the span is what
// the accumulator's dt advance represents and an edge's position inside it is
// (arrival - start) / (end - start) -- in range by construction, with no clamp.
struct input_frame_span_t
{
  uint64_t start_qpc_ticks = 0;
  uint64_t end_qpc_ticks   = 0;
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

// Starts the raw-input thread that stamps edges. Never fatal: on failure (or on
// a platform without one) the edge queue falls back to SDL's transitions,
// stamped at the frame boundary -- one frame coarser, still playable, and
// logged rather than silent.
void init();
void shutdown();

// Call once at the start of each frame, before pumping SDL events.
void new_frame();

// Translate one SDL_Event. Takes void* so the header stays SDL-free.
void process_sdl_event(const void *sdl_event);

// --- Polling -----------------------------------------------------------------
//
// ONE SAMPLING INSTANT: every accessor below answers as of `new_frame`, which
// runs before the frame's events are pumped. None of them is a live query, and
// that is not a limitation being worked around -- SDL's state only moves during
// a pump and there is exactly one pump per frame, so a "live" read was never
// `now`, it was the same snapshot taken one frame later. Having two of these on
// different instants cost a real bug: a caller folding is_key_down and a live
// is_mouse_down into one bitfield and checking it against edge-derived state
// saw every click as a lost transition.
//
// The instant is BEFORE the pump on purpose. That is what makes these
// comparable with frame_input_edges() -- see the bracket check in
// play_state.cpp.

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

// Every key and mouse-button transition this frame, plus the mouse travel
// between them, in arrival order with a time on each. Releases included -- see
// input_edge_t.
Span<const input_edge_t> frame_input_edges();


// The span the edges above arrived in, and the counter frequency to divide by.
// Nothing else in the client keeps time in this domain.
input_frame_span_t frame_arrival_span();
uint64_t           arrival_clock_frequency();

// The arrival clock read NOW, for the one caller that needs a time from outside
// the frame boundary: recording when a frame was actually presented. Unlike
// every accessor above this is deliberately live -- the span endpoints are read
// at the top of the frame, and present happens at the bottom of it, so reusing
// one here would stamp the frame roughly a whole frame before it existed.
uint64_t arrival_clock_now();

// Whether edges are coming from the raw-input thread. False means the SDL
// fallback, whose stamps are frame-granular -- the sub-tick fold is still
// correct, just no finer than it was before raw input existed.
bool raw_input_is_active();

// --- ImGui capture -----------------------------------------------------------

bool imgui_wants_mouse();
bool imgui_wants_keyboard();

} // namespace client::input

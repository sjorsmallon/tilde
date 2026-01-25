#pragma once

namespace client {
namespace input {

// Call once per frame at the start of the tick
void new_frame();

// Process SDL events (optional, mostly for specific event-based needs)
// Most input can be polled via SDL directly.
void process_event(
    const void *event); // void* to avoid including SDL.h in header

// Keyboard
bool is_key_down(int scancode);    // SDL_Scancode
bool is_key_pressed(int scancode); // True only on the frame it was pressed

// Mouse
bool is_mouse_down(int button); // SDL_BUTTON_LEFT/RIGHT/MIDDLE
void get_mouse_pos(int *x, int *y);
void get_mouse_delta(int *x, int *y);
void set_relative_mouse_mode(bool enabled);

} // namespace input
} // namespace client

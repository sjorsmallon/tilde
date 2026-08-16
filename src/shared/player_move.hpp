#pragma once
#include "collision_detection.hpp"
#include "cvars/generated/cvars_generated.hpp"
#include "debug_collision.hpp"
#include "plane.hpp"
#include <tuple>
#include <vector>

struct Collider_Planes
{
  std::vector<Plane> ground_planes;
  std::vector<Plane> ceiling_planes;
  std::vector<Plane> wall_planes;
};

// Button bitfield constants — shared between client and server.
// These map 1:1 to the proto buttons_bitfield.
namespace Button
{
constexpr uint64_t Forward  = 1 << 0;
constexpr uint64_t Backward = 1 << 1;
constexpr uint64_t Left     = 1 << 2;
constexpr uint64_t Right    = 1 << 3;
constexpr uint64_t Jump     = 1 << 4;
constexpr uint64_t Key1     = 1 << 5;
constexpr uint64_t Key2     = 1 << 6;
constexpr uint64_t Key3     = 1 << 7;
constexpr uint64_t Key4     = 1 << 8;
constexpr uint64_t Key5     = 1 << 9;
constexpr uint64_t Key6     = 1 << 10;
constexpr uint64_t Key7     = 1 << 11;
constexpr uint64_t Key8     = 1 << 12;
constexpr uint64_t Key9     = 1 << 13;
constexpr uint64_t Key0     = 1 << 14;
constexpr uint64_t Fire     = 1 << 15;
constexpr uint64_t Zoom     = 1 << 16;
constexpr uint64_t P        = 1 << 17; // Placeholder that you can use to hijack.
} // namespace Button

// Initializers are load-bearing, not decoration. `Move_Input input;` at block
// scope is DEFAULT-initialization, which for a struct without them leaves every
// bool indeterminate -- and a caller that sets only the buttons it cares about
// then moves on garbage. That is what made bots jump constantly: bot_system
// assigned `forward_pressed` and nothing else, and the stack slot behind
// `jump_pressed` held the same leftover byte every tick.
//
// Reading an indeterminate bool is UB, so it is not merely a wrong value: a
// bool holding a byte other than 0 or 1 can take both branches of an `if`.
struct Move_Input
{
  bool forward_pressed  = false;
  bool backward_pressed = false;
  bool left_pressed     = false;
  bool right_pressed    = false;
  bool jump_pressed     = false;
};

inline Move_Input move_input_from_buttons(uint64_t buttons)
{
  return {
    .forward_pressed  = (buttons & Button::Forward)  != 0,
    .backward_pressed = (buttons & Button::Backward) != 0,
    .left_pressed     = (buttons & Button::Left)     != 0,
    .right_pressed    = (buttons & Button::Right)     != 0,
    .jump_pressed     = (buttons & Button::Jump)      != 0,
  };
}

inline uint64_t buttons_from_move_input(const Move_Input &input)
{
  uint64_t b = 0;
  if (input.forward_pressed)  b |= Button::Forward;
  if (input.backward_pressed) b |= Button::Backward;
  if (input.left_pressed)     b |= Button::Left;
  if (input.right_pressed)    b |= Button::Right;
  if (input.jump_pressed)     b |= Button::Jump;
  return b;
}

// Movement-driven cosmetic events produced by a single player_move() tick.
// This is an optional side-channel: callers that don't care (e.g. client
// reconciliation replays, bots) pass nullptr. The simulation only reports raw
// facts — whether a landing is loud enough to bother playing is a presentation
// decision left to consumers (gated by a cvar).
struct Move_Events
{
  bool  jumped = false;          // a jump impulse was applied this tick
  bool  landed = false;          // touched down from the air this tick
  float land_impact_speed = 0.f; // downward speed (units/s) arrested on landing
};

// new_player_position, new_player_velocity. `out_events`, if non-null, receives
// the movement cosmetics produced this tick (jump/land).
//
// `debug_faces`, if non-null AND debug_show_collisions is set, receives the
// contact face polygons this tick. Null means record nothing. Only the CLIENT
// passes a sink: recording happens in shared code but the drawing is
// client-side, so a server-side recording has no reader (see debug_collision.hpp).
//
// `bvh` is STATIC geometry, and taking nothing else about the world is
// load-bearing rather than incidental: it is why movement needs no lag
// compensation. Walls are identical at every tick, so there is no "which tick
// did I collide against" to answer, and the client can predict this exactly.
// Shooting rewinds because it tests against other players -- the only part of
// the world that differs between two ticks.
//
// So the day a dynamic collider becomes a movement input (player-vs-player,
// moving platforms, standing on a Physics_Body_Entity), prediction silently
// starts depending on time and drifts with no error to say so. That is a design
// decision, not a parameter to add -- see lag_compensation_def.md, "Why movement
// needs no rewind".
//
// `cvars` is the process's one cvar_state_t (the launcher's), passed by
// reference rather than read from a global: the pm_* tunables are @Mirrored, so
// the client's prediction and the server's authoritative run must feed the SAME
// values into this function or the client mispredicts every frame. A reference
// makes that a signature obligation instead of a hope about which copy of a
// static-lib global each module happened to link.
std::tuple<vec3, vec3> player_move(
    const cvars::cvar_state_t &cvars,
    const Move_Input &input,
    const Bounding_Volume_Hierarchy &bvh,
    const vec3 &old_position, const vec3 &old_velocity, const vec3 &front,
    const vec3 &right, const float half_width, const float half_height,
    const float dt, Move_Events *out_events = nullptr,
    debug_collision::Face_Sink *debug_faces = nullptr);

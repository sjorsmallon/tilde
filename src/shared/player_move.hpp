#pragma once
#include "collision_detection.hpp"
#include "cvar.hpp"
#include "plane.hpp"
#include <tuple>
#include <vector>

// Movement cvars (defined in player_move.cpp)
extern cvar::CVar<float> pm_maxspeed;
extern cvar::CVar<float> pm_stopspeed;
extern cvar::CVar<float> pm_friction;
extern cvar::CVar<float> pm_ground_acceleration;
extern cvar::CVar<float> pm_air_acceleration;
extern cvar::CVar<float> pm_overbounce;
extern cvar::CVar<float> pm_jumpspeed;
extern cvar::CVar<float> g_gravity;
extern cvar::CVar<float> pm_speed_threshold;

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
} // namespace Button

struct Move_Input
{
  bool forward_pressed;
  bool backward_pressed;
  bool left_pressed;
  bool right_pressed;
  bool jump_pressed;
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

// new_player_position, new_player_velocity
std::tuple<vec3, vec3> player_move(
    const Move_Input &input,
    const Bounding_Volume_Hierarchy &bvh,
    const vec3 &old_position, const vec3 &old_velocity, const vec3 &front,
    const vec3 &right, const float half_width, const float half_height, const float dt);

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

//@Note: this move input is serialized and sent across the wire. I don't think
// that this is the correct place to define it.
// but I will leave it here for now.
struct Move_Input
{
  bool forward_pressed;
  bool backward_pressed;
  bool left_pressed;
  bool right_pressed;
  bool jump_pressed;
};

// new_player_position, new_player_velocity
std::tuple<vec3, vec3> player_move(
    Move_Input &input,
    const Bounding_Volume_Hierarchy &bvh,
    const vec3 &old_position, const vec3 &old_velocity, const vec3 &front,
    const vec3 &right, float half_width, float half_height, const float dt);

#pragma once

#include "camera.hpp"
#include "input.hpp"
#include "../shared/linalg.hpp"

namespace client
{

// The editor's free-fly controls, as one function both the editor and
// Play_State's noclip drive: WASD along the view, Space/C up and down the world
// axis, shift for speed, and a mouse delta on yaw and pitch. Perspective only
// -- the editor's orthographic arms are the editor's.
struct fly_camera_input_t
{
  bool forward  = false;
  bool backward = false;
  bool left     = false;
  bool right    = false;
  bool up       = false;
  bool down     = false;
  bool fast     = false;
  linalg::vec2i look_delta = {0, 0};
};

struct fly_camera_settings_t
{
  float units_per_second  = 1600.0f;
  float fast_multiplier   = 2.0f;
  float degrees_per_pixel = 0.1f;
};

// The keys the editor binds, read off the level accessors. The look delta is
// the caller's: the editor takes SDL's folded delta while the right button is
// held, Play_State sums the frame's motion edges.
[[nodiscard]] fly_camera_input_t read_fly_camera_keys();

void fly_camera(camera_t& camera, const fly_camera_input_t& input,
                const fly_camera_settings_t& settings, float dt);

} // namespace client

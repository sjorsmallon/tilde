#pragma once

#include <cmath>

namespace client {

struct camera_t {
  float x = 0.0f, y = 0.0f, z = 0.0f;
  float yaw = 0.0f;
  float pitch = 0.0f;

  // Default constructor
  camera_t() = default;

  // Construct from position coordinates (yaw/pitch default to 0 - Looking +X)
  camera_t(float in_x, float in_y, float in_z)
      : x(in_x), y(in_y), z(in_z), yaw(0.0f), pitch(0.0f) {}

  // Construct from position vector and look direction vector
  // view_x, view_y, view_z should be a normalized direction vector
  static camera_t from_view_vector(float px, float py, float pz, float vx,
                                   float vy, float vz) {
    camera_t cam;
    cam.x = px;
    cam.y = py;
    cam.z = pz;

    // Pitch from Y component
    // Y up. sin(pitch) = y.
    cam.pitch = std::asin(vy) * 57.2957795f; // to degrees

    // Yaw from X/Z components
    // yaw 0 -> +X (1,0)
    // yaw 90 -> +Z (0,1)
    // atan2(z, x) gives angle from +X axis towards +Y (here +Z).
    cam.yaw = std::atan2(vz, vx) * 57.2957795f; // to degrees

    return cam;
  }
};

} // namespace client

#pragma once

#include <cmath>

#include "../shared/linalg.hpp"


namespace client
{

// An orthographic view draws this far in front of AND behind the camera, so the pick ray starts at the near end.
constexpr float ORTHOGRAPHIC_DEPTH_HALF_RANGE = 100000.0f;

struct camera_t
{
  linalg::vec3f position = {0.0f, 0.0f, 0.0f};
  float yaw = 0.0f;
  float pitch = 0.0f;
  bool orthographic = false;
  float ortho_height = 10.0f; // Scale/Zoom factor for ortho
  float fov_degrees = 90.0f;

  // Orbit mode fields — when orbit is true, position is derived from
  // orbit_target + orbit_distance + yaw/pitch each frame via update_orbit().
  bool orbit = false;
  linalg::vec3f orbit_target = {0.0f, 0.0f, 0.0f};
  float orbit_distance = 5.0f;
  float orbit_min_distance = 0.5f;
  float orbit_max_distance = 50.0f;

  // Default constructor
  camera_t() = default;

  // Construct from position coordinates (yaw/pitch default to 0 - Looking +X)
  camera_t(float in_x, float in_y, float in_z)
      : position{.x = in_x, .y = in_y, . z = in_z}, yaw(0.0f), pitch(0.0f)
  {
  }
};

inline void look_at(camera_t &cam, const linalg::vec3 &target)
{
  const linalg::vec3 to_target = target - cam.position;
  if (linalg::length(to_target) <= 1e-6f)
    return;

  const linalg::view_angles_t facing = linalg::view_angles_from_direction(to_target);
  cam.yaw   = facing.yaw_degrees;
  cam.pitch = facing.pitch_degrees;
}

struct camera_basis_t
{
  linalg::vec3 forward;
  linalg::vec3 right;
  linalg::vec3 up;
};

inline camera_basis_t get_orientation_vectors(const camera_t &cam)
{
  const linalg::vec3 forward = linalg::direction_from_angles(cam.yaw, cam.pitch);
  const linalg::vec3 right   = linalg::right_from_forward(forward);
  return camera_basis_t{forward, right, linalg::cross(right, forward)};
}

// --- Orbit mode helpers ---

// Recompute position from orbit_target + orbit_distance + yaw/pitch.
// Call after changing yaw/pitch/distance/target in orbit mode.
inline void update_orbit(camera_t &cam)
{
  cam.position = cam.orbit_target + linalg::direction_from_angles(cam.yaw, cam.pitch) * cam.orbit_distance;
}

// Rotate around target
inline void orbit_rotate(camera_t &cam, float delta_x, float delta_y,
                         float sensitivity = 0.3f)
{
  cam.yaw += delta_x * sensitivity;
  cam.pitch -= delta_y * sensitivity;
  cam.pitch = linalg::clamp_view_pitch(cam.pitch);
  update_orbit(cam);
}

// Pan orbit target in the camera's local right/up plane
inline void orbit_pan(camera_t &cam, float delta_x, float delta_y,
                      float sensitivity = 0.005f)
{
  auto [forward, right, up] = get_orientation_vectors(cam);
  float scale = cam.orbit_distance * sensitivity;
  cam.orbit_target = cam.orbit_target - right * (delta_x * scale);
  cam.orbit_target = cam.orbit_target + up * (delta_y * scale);
  update_orbit(cam);
}

// Zoom by changing orbit distance
inline void orbit_zoom(camera_t &cam, float delta, float sensitivity = 0.5f)
{
  cam.orbit_distance -= delta * sensitivity;
  cam.orbit_distance = linalg::clamp(cam.orbit_distance,
                                     cam.orbit_min_distance,
                                     cam.orbit_max_distance);
  update_orbit(cam);
}

inline linalg::ray_t get_pick_ray(const camera_t &cam, float ndc_x, float ndc_y,
                                  float aspect_ratio)
{
  using namespace linalg;
  auto [F, R, U] = get_orientation_vectors(cam);

  if (cam.orthographic)
  {
    float h = cam.ortho_height;
    float w = h * aspect_ratio;
    float ox = ndc_x * (w * 0.5f);
    float oy = ndc_y * (h * 0.5f);
    vec3 origin = {cam.position.x, cam.position.y, cam.position.z};
    origin = origin - F * ORTHOGRAPHIC_DEPTH_HALF_RANGE;
    origin = origin + R * ox + U * oy;
    return {origin, F};
  }
  else
  {
    float tanHalf = std::tan(to_radians(cam.fov_degrees) * 0.5f);
    float vx = ndc_x * aspect_ratio * tanHalf;
    float vy = ndc_y * tanHalf;
    vec3 dir = normalize(R * vx + U * vy + F);
    return {{cam.position.x, cam.position.y, cam.position.z}, dir};
  }
}

} // namespace client

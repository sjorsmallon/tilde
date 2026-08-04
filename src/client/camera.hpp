#pragma once

#include <cmath>

#include "../shared/linalg.hpp"


namespace client
{

struct camera_t
{
  linalg::vec3f position = {0.0f, 0.0f, 0.0f};
  float yaw = 0.0f;
  float pitch = 0.0f;
  bool orthographic = false;
  float ortho_height = 10.0f; // Scale/Zoom factor for ortho

  // Vertical FOV used by the perspective projection. Lives on the camera rather
  // than being read from r_fov at each use site: zoom varies it per frame, and
  // picking must agree with whatever the projection actually used this frame.
  // Owners seed it from r_fov; ignored when orthographic.
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

  // Construct from position vector and look direction vector
  // view_x, view_y, view_z should be a normalized direction vector
  static camera_t from_view_vector(float px, float py, float pz, float vx,
                                   float vy, float vz)
  {
    camera_t cam;
    cam.position.x = px;
    cam.position.y = py;
    cam.position.z = pz;

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

inline void look_at(camera_t &cam, const linalg::vec3 &target)
{
  float dx = target.x - cam.position.x;
  float dy = target.y - cam.position.y;
  float dz = target.z - cam.position.z;

  // Normalize
  float len = std::sqrt(dx * dx + dy * dy + dz * dz);
  if (len > 1e-6f)
  {
    dx /= len;
    dy /= len;
    dz /= len;
  }
  else
  {
    return; // maintain current look if target is same as eye
  }

  cam.pitch = std::asin(dy) * 57.2957795f;
  cam.yaw = std::atan2(dz, dx) * 57.2957795f;
}

struct camera_basis_t
{
  linalg::vec3 forward;
  linalg::vec3 right;
  linalg::vec3 up;
};

inline camera_basis_t get_orientation_vectors(const camera_t &cam,
                                              const linalg::vec3 &world_up = {
                                                  {0, 1, 0}})
{
  float yaw_in_radians = linalg::to_radians(cam.yaw);
  float pitch_in_radians = linalg::to_radians(cam.pitch);

  float cY = std::cos(yaw_in_radians);
  float sY = std::sin(yaw_in_radians);
  float cP = std::cos(pitch_in_radians);
  float sP = std::sin(pitch_in_radians);

  linalg::vec3 forward = {cY * cP, sP, sY * cP};
  linalg::vec3 right = linalg::cross(forward, world_up);

  float lenR = linalg::length(right);
  if (lenR < 0.001f)
  {
    right = {{1, 0, 0}};
  }
  else
  {
    right = right * (1.0f / lenR);
  }

  linalg::vec3 up = linalg::cross(right, forward);

  return camera_basis_t{forward, right, up};
}

// --- Orbit mode helpers ---

// Recompute position from orbit_target + orbit_distance + yaw/pitch.
// Call after changing yaw/pitch/distance/target in orbit mode.
inline void update_orbit(camera_t &cam)
{
  float yaw_rad = linalg::to_radians(cam.yaw);
  float pitch_rad = linalg::to_radians(cam.pitch);
  float cos_pitch = std::cos(pitch_rad);
  linalg::vec3f offset = {
      cos_pitch * std::cos(yaw_rad),
      std::sin(pitch_rad),
      cos_pitch * std::sin(yaw_rad)};
  cam.position = cam.orbit_target + offset * cam.orbit_distance;
}

// Rotate around target
inline void orbit_rotate(camera_t &cam, float delta_x, float delta_y,
                         float sensitivity = 0.3f)
{
  cam.yaw += delta_x * sensitivity;
  cam.pitch -= delta_y * sensitivity;
  cam.pitch = linalg::clamp(cam.pitch, -89.0f, 89.0f);
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
    
    // In editor, we usually start ray far back for orthographic picking
    origin = origin - F * 1000.0f;
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

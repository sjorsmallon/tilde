#pragma once

#include <cmath>

#include "../shared/linalg.hpp"

namespace client
{

struct camera_t
{
  float x = 0.0f, y = 0.0f, z = 0.0f;
  float yaw = 0.0f;
  float pitch = 0.0f;
  bool orthographic = false;
  float ortho_height = 10.0f; // Scale/Zoom factor for ortho

  // Default constructor
  camera_t() = default;

  // Construct from position coordinates (yaw/pitch default to 0 - Looking +X)
  camera_t(float in_x, float in_y, float in_z)
      : x(in_x), y(in_y), z(in_z), yaw(0.0f), pitch(0.0f)
  {
  }

  // Construct from position vector and look direction vector
  // view_x, view_y, view_z should be a normalized direction vector
  static camera_t from_view_vector(float px, float py, float pz, float vx,
                                   float vy, float vz)
  {
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

inline void look_at(camera_t &cam, const linalg::vec3 &target)
{
  float dx = target.x - cam.x;
  float dy = target.y - cam.y;
  float dz = target.z - cam.z;

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
  float radYaw = linalg::to_radians(cam.yaw);
  float radPitch = linalg::to_radians(cam.pitch);

  float cY = std::cos(radYaw);
  float sY = std::sin(radYaw);
  float cP = std::cos(radPitch);
  float sP = std::sin(radPitch);

  linalg::vec3 F = {cY * cP, sP, sY * cP};
  linalg::vec3 R = linalg::cross(F, world_up);

  float lenR = linalg::length(R);
  if (lenR < 0.001f)
  {
    R = {{1, 0, 0}};
  }
  else
  {
    R = R * (1.0f / lenR);
  }

  linalg::vec3 U = linalg::cross(R, F);

  return camera_basis_t{F, R, U};
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
    vec3 origin = {cam.x, cam.y, cam.z};
    // In editor, we usually start ray far back for orthographic picking?
    // The original code did: ray_origin = cam - F * 1000 + R*ox + U*oy
    // Let's replicate that behavior.
    origin = origin - F * 1000.0f;
    origin = origin + R * ox + U * oy;
    return {origin, F};
  }
  else
  {
    float fov = 90.0f; // Hardcoded default from editor_state.cpp for now
    float tanHalf = std::tan(to_radians(fov) * 0.5f);
    float vx = ndc_x * aspect_ratio * tanHalf;
    float vy = ndc_y * tanHalf;
    vec3 dir = normalize(R * vx + U * vy + F);
    return {{cam.x, cam.y, cam.z}, dir};
  }
}

} // namespace client

#include "debug_collision.hpp"
#include <cmath>

namespace debug_collision
{

// Global storage for collision planes
std::vector<Debug_Collision_Plane> g_collision_planes;

// CVar to toggle collision visualization
cvar::CVar<bool> debug_show_collisions("debug_show_collisions", false,
                                       "Show collision planes in green",
                                       cvar::flags::None);

// CVar to toggle hitbox visualization
cvar::CVar<bool> debug_show_hitboxes("debug_show_hitboxes", false,
                                     "Show entity hitboxes in wireframe",
                                     cvar::flags::None);

void record_collision(const Plane &plane, const vec3 &collision_point, float approx_size)
{
  if (!debug_show_collisions.Get())
    return;

  Debug_Collision_Plane debug_plane;
  debug_plane.point = plane.point;
  debug_plane.normal = plane.normal;
  debug_plane.center = collision_point;
  debug_plane.radius = approx_size;

  g_collision_planes.push_back(debug_plane);
}

void clear_collision_planes()
{
  g_collision_planes.clear();
}

void generate_plane_quad(const vec3 &point, const vec3 &normal, float size,
                        vec3 quad_vertices[4])
{
  // Generate two perpendicular vectors to the normal to form a basis
  vec3 tangent, bitangent;

  // Find a vector not parallel to normal
  vec3 up = {0, 1, 0};
  if (std::abs(dot(normal, up)) > 0.9f)
  {
    up = {1, 0, 0}; // Use X if normal is close to Y
  }

  // Generate perpendicular basis vectors
  tangent = normalize(cross(normal, up));
  bitangent = normalize(cross(normal, tangent));

  // Create quad corners around the point
  float half_size = size * 0.5f;
  quad_vertices[0] = point + (tangent * half_size) + (bitangent * half_size);
  quad_vertices[1] = point - (tangent * half_size) + (bitangent * half_size);
  quad_vertices[2] = point - (tangent * half_size) - (bitangent * half_size);
  quad_vertices[3] = point + (tangent * half_size) - (bitangent * half_size);
}

} // namespace debug_collision

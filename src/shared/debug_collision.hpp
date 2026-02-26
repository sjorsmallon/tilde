#pragma once

#include "linalg.hpp"
#include "plane.hpp"
#include "cvar.hpp"
#include <vector>

/**
 * Debug Collision Visualization System
 *
 * Captures the actual colliding face polygon during player movement and
 * provides it for rendering. Used for debugging collision detection issues.
 */

struct Debug_Collision_Face
{
  Plane plane;                        // push plane (normal + point)
  std::vector<linalg::vec3> polygon;  // actual face vertices from the primitive
};

namespace debug_collision
{

// Global state for collision visualization (cleared each frame)
extern std::vector<Debug_Collision_Face> g_collision_faces;

// CVar to enable/disable collision debug visualization
extern cvar::CVar<bool> debug_show_collisions;

// CVar to enable/disable hitbox debug visualization
extern cvar::CVar<bool> debug_show_hitboxes;

// CVar to enable/disable navmesh grid visualization
extern cvar::CVar<bool> debug_show_navmesh;

// Record a collision with a plane and its actual face polygon (called from collision code)
void record_collision(const Plane &plane, const std::vector<linalg::vec3> &polygon);

// Clear collision faces (call at end of frame after rendering)
void clear_collision_faces();

} // namespace debug_collision

#pragma once

#include "linalg.hpp"
#include "plane.hpp"
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
//
// The four debug_show_* toggles that used to live here are now fields on the
// launcher's cvar_state_t (see cvars.def) -- read them as
// `cvars.debug_show_collisions` etc.
extern std::vector<Debug_Collision_Face> g_collision_faces;

// Record a collision with a plane and its actual face polygon (called from
// collision code). Records UNCONDITIONALLY: the caller checks
// debug_show_collisions, because it is the caller that holds the cvar_state_t.
void record_collision(const Plane &plane, const std::vector<linalg::vec3> &polygon);

// Clear collision faces (call at end of frame after rendering)
void clear_collision_faces();

} // namespace debug_collision

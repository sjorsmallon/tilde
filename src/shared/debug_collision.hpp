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

// Where recorded faces go. Owned by whoever DRAWS them -- the client, and only
// for its own prediction run.
//
// The four debug_show_* toggles that used to live here are now fields on the
// launcher's cvar_state_t (see cvars.def) -- read them as
// `cvars.debug_show_collisions` etc.
//
// This replaced a game_shared global (g_collision_faces), which could not have
// worked: game_shared is a static lib, so the server DLL recorded into a copy
// the client's drawing never read and the client's clear never touched. Once
// debug_show_collisions genuinely reached the server (CVAR TRACK), that copy
// grew for as long as the toggle was on, and server::Tick had to drain it every
// tick purely to bound it. A bucket parameter deletes the global rather than
// draining it: the server passes nullptr and records nothing, which is honest,
// because server-side faces were never drawable -- in a networked build they
// would have to cross the wire to be seen at all.
//
// Modules DIFFER on this, deliberately. It is not shared state like the cvar
// values; it is one side's view of its own simulation run.
using Face_Bucket = std::vector<Debug_Collision_Face>;

// Record a collision with a plane and its actual face polygon (called from
// collision code). Records UNCONDITIONALLY: the caller checks
// debug_show_collisions, because it is the caller that holds the cvar_state_t.
void record_collision(Face_Bucket &bucket, const Plane &plane,
                      const std::vector<linalg::vec3> &polygon);

} // namespace debug_collision

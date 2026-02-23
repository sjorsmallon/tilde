#pragma once

#include "linalg.hpp"
#include "plane.hpp"
#include "cvar.hpp"
#include <vector>

/**
 * Debug Collision Visualization System
 *
 * This system captures collision planes during player movement and provides
 * data for rendering them. Used for debugging collision detection issues.
 */

struct Debug_Collision_Plane
{
  vec3 point;       // A point on the plane
  vec3 normal;      // Plane normal (outward facing)
  vec3 center;      // Approximate center for visualization
  float radius;     // Approximate size for quad generation
};

namespace debug_collision
{

// Global state for collision visualization (cleared each frame)
extern std::vector<Debug_Collision_Plane> g_collision_planes;

// CVar to enable/disable collision debug visualization
extern cvar::CVar<bool> debug_show_collisions;

// CVar to enable/disable hitbox debug visualization
extern cvar::CVar<bool> debug_show_hitboxes;

// Record a collision with a plane (called from collision detection code)
void record_collision(const Plane &plane, const vec3 &collision_point, float approx_size = 10.0f);

// Clear collision planes (call at start of frame)
void clear_collision_planes();

// Helper: Generate quad vertices for rendering a plane
// Returns 4 corners of a quad perpendicular to the plane normal
void generate_plane_quad(const vec3 &point, const vec3 &normal, float size,
                        vec3 quad_vertices[4]);

} // namespace debug_collision

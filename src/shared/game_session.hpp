#pragma once

#include "collision_detection.hpp"
#include "entity_system.hpp"
#include "map.hpp"
#include "navmesh.hpp"
#include "physics.hpp"
#include <string>
#include <vector>

namespace shared
{

// The runtime representation of the game world.
// Distinguished from map_t which is the serialized/file data format.
// This structure holds the active entity system, the physics world (BVH),
// and the actual static geometry data required for collision.
//
// Lifecycle:
// 1. Built from a map via build_session()
// 2. Updated via game loop (which updates Entity_System)
// 3. BVH is static for the duration of the session (for now)
struct game_session_t
{
  // Manages all active dynamic entities (Players, Weapons, Projectiles)
  Entity_System entity_system;

  // The session's OWN COPY of the map's geometry, in map order — so
  // geometry[i] is the object behind BVH leaf i, and so is what the renderer
  // walks.
  //
  // A copy, not a reference: this used to be
  // std::vector<std::shared_ptr<entities::Entity>> holding the very pointers
  // map_t held, which meant map and session aliased one object and editing
  // either wrote through to the other. Geometry values copy, so the aliasing
  // (and its lifetime coupling, and the write-back-into-the-map hazard) simply
  // stops existing.
  std::vector<map_geometry_t> geometry;

  // The acceleration structure for collision queries against `geometry`.
  // Dynamic entity collision is handled separately via the Entity_System.
  Bounding_Volume_Hierarchy bvh;


  

  // Baked navmesh — copied from map_t on session init.
  navmesh_t navmesh;

  std::string map_name;
};

// The runtime world for `map`: entities copied into pools, geometry copied,
// BVH built over that copy, navmesh carried across.
//
// Returns a fresh session rather than refilling one, which is why there is no
// reset inside: replacing the caller's session is an assignment, and "what
// survived the last map" is not a question this can raise. `map` is not
// mutated -- the session stamps uids on its OWN copies (session_test guards it).
[[nodiscard]] game_session_t build_session(const map_t &map);

// Register Jolt static bodies for the map's geometry (boxes and displacements,
// both as their axis-aligned bound). Call after build_session on both
// server and client when physics is needed.
//
// Static meshes are skipped: their collision shape would be the triangle mesh,
// and registering their bounding box instead would put an invisible wall around
// every prop. The BVH still picks them up, so player movement collides with them.
void populate_static_physics_bodies(physics_state_t &state, const map_t &map);

} // namespace shared

#pragma once

#include "collision_detection.hpp"
#include "entity_system.hpp"
#include "map.hpp"
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
// 1. Initialized via start_session_from_map()
// 2. Updated via game loop (which updates Entity_System)
// 3. BVH is static for the duration of the session (for now)
struct game_session_t
{
  // Manages all active dynamic entities (Players, Weapons, Projectiles)
  Entity_System entity_system;

  // Static geometry data.
  // The BVH references indices into this vector.
  // We copy this from the map_t so the session is self-contained and
  // doesn't rely on the map_t remaining in memory.
  std::vector<static_geometry_t> static_geometry;

  // The acceleration structure for collision queries against static_geometry.
  // Dynamic entity collision is handled separately via the Entity_System.
  Bounding_Volume_Hierarchy bvh;

  std::string map_name;
};

// Initializes the session from a loaded map.
// - Resets the entity system and populates it from map entities.
// - Copies static geometry (AABBs).
// - Builds the BVH for static geometry.
void init_session_from_map(game_session_t &session, const map_t &map);

} // namespace shared

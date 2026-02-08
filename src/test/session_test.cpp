#include "entities/player_entity.hpp"
#include "game_session.hpp"
#include "log.hpp"
#include <cassert>
#include <iostream>

using namespace shared;

int main()
{
  log_error("Starting Session Test");

  // 1. Create a dummy map_t
  map_t test_map;
  test_map.name = "Test Map";

  // Add an AABB
  aabb_t aabb;
  aabb.center = {0, 0, 0};
  aabb.half_extents = {10, 10, 10};
  test_map.static_geometry.push_back({.data = aabb});

  // Add a Player Entity Spawn
  entity_spawn_t spawn;
  spawn.type = entity_type::PLAYER;
  spawn.position = {5, 5, 0};
  spawn.yaw = 90.0f;
  spawn.properties["origin"] = "5 5 0"; // Map loader would do this
  spawn.properties["view_angle_yaw"] = "90";
  test_map.entities.push_back(spawn);

  // 2. Initialize Session
  game_session_t session;
  init_session_from_map(session, test_map);

  // 3. Verify

  // Verify Map Name
  if (session.map_name != "Test Map")
  {
    log_error("Map Name Mismatch");
    return 1;
  }

  // Verify Static Geometry and BVH
  if (session.static_geometry.size() != 1)
  {
    log_error("Static Geometry Size Mismatch");
    return 1;
  }
  if (session.bvh.nodes.empty())
  {
    log_error("BVH Empty");
    return 1;
  }

  // Verify Entities
  // We need to request the player list or check count
  auto *players = session.entity_system.get_entities<network::Player_Entity>(
      entity_type::PLAYER);
  if (!players || players->empty())
  {
    log_error("No Players Spawned");
    return 1;
  }

  const auto &player = (*players)[0];
  log_error("Player spawned at: {}, {}, {}", player.position.value.x,
            player.position.value.y, player.position.value.z);

  // Check if properties were applied correctly
  if (player.position.value.x != 5.0f || player.position.value.y != 5.0f)
  {
    log_error("Player Position Mismatch. Expected 5,5,0. Got: {},{},{}",
              player.position.value.x, player.position.value.y,
              player.position.value.z);
    return 1;
  }

  log_error("Session Test Passed!");
  return 0;
}

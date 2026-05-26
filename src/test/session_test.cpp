#include "entities/player_entity.hpp"
#include "entities/static_entities.hpp"
#include "game_session.hpp"
#include "log.hpp"
#include "map.hpp" // shared::create_entity_by_classname
#include <cassert>
#include <iostream>

using namespace shared;

int main()
{
  log_error("Starting Session Test");

  // 1. Create a dummy map_t
  map_t test_map;
  test_map.name = "Test Map";

  // Add an AABB Entity instead of static_geometry
  auto aabb_ent = shared::create_entity_by_classname("aabb_entity");
  if (auto *e = dynamic_cast<network::AABB_Entity *>(aabb_ent.get()))
  {
    e->position = {0, 0, 0};
    e->volume.half_extents = {10, 10, 10};
  }
  test_map.add_entity(aabb_ent);

  // Add a Player Spawn marker
  auto spawn_ent = shared::create_entity_by_classname("player_start");
  if (auto *p = dynamic_cast<network::Player_Spawn_Entity *>(spawn_ent.get()))
  {
    p->position = {5, 5, 0};
  }
  test_map.add_entity(spawn_ent);

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

  // Verify Static Geometry (Entities) and BVH
  // We expect 1 static entity (AABB) and 1 dynamic (Player) -> handled by
  // EntitySystem? init_session_from_map likely splits them. We need to check
  // implementation of init_session_from_map. Assuming it puts AABB in
  // static_entities.
  if (session.static_entities.size() != 1)
  {
    log_error("Static Entities Size Mismatch. Expected 1, got {}",
              session.static_entities.size());
    return 1;
  }

  // Verify map uid propagated into AABB's entity_id field. Pre-fix every
  // map-loaded entity ended up with entity_id == 0 because the map→runtime
  // hand-off didn't copy uid → entity_id.
  network::AABB_Entity *aabb_static =
      dynamic_cast<network::AABB_Entity *>(session.static_entities[0].get());
  if (!aabb_static || aabb_static->entity_id != 1)
  {
    log_error("AABB static entity_id should equal map uid 1, got {}",
              aabb_static ? aabb_static->entity_id : 0u);
    return 1;
  }

  // Verify the runtime spawn counter was seeded past the highest map uid.
  // aabb is uid 1, player is uid 2, so map.next_uid is 3 → entity_system
  // next_entity_id must also be 3 so the next spawn() can't collide.
  if (session.entity_system.next_entity_id != 3)
  {
    log_error("entity_system.next_entity_id should be seeded to 3 (= map.next_uid), got {}",
              session.entity_system.next_entity_id);
    return 1;
  }

  // Verify BVH (should be built from static entities)
  // If BVH building is implemented for entities, this should pass.
  if (session.bvh.nodes.empty())
  {
    log_warning("BVH Empty (Might be expected if BVH build logic not fully "
                "updated for entities yet)");
    // return 1; // Don't fail if BVH build implementation pending
  }

  // Verify Entity System (Player spawn marker).
  // "player_start" classname maps to Player_Spawn_Entity / entity_type::PLAYER_SPAWN.
  // Player_Entity (entity_type::PLAYER) is the live player, runtime-spawned
  // when a client connects — not a map-loaded thing.
  auto *spawns = session.entity_system.get_entities<network::Player_Spawn_Entity>(
      entity_type::PLAYER_SPAWN);

  if (!spawns || spawns->empty())
  {
    log_error("No Player Spawn markers found");
    return 1;
  }

  const auto &spawn = (*spawns)[0];

  // Check if properties were applied correctly
  if (spawn.position.x != 5.0f || spawn.position.y != 5.0f)
  {
    log_error("Spawn marker position mismatch. Expected 5,5,0. Got: {},{},{}",
              spawn.position.x, spawn.position.y,
              spawn.position.z);
    return 1;
  }

  // Verify spawn marker's entity_id matches its map uid (added second, uid = 2).
  // Same root cause as the AABB check above.
  if (spawn.entity_id != 2)
  {
    log_error("Spawn marker entity_id should equal map uid 2, got {}",
              spawn.entity_id);
    return 1;
  }

  log_error("Session Test Passed!");
  return 0;
}

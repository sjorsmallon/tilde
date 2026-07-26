#include "entities/player_entity.hpp"
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

  // A box brush — map-owned geometry, not an entity.
  box_geometry_t floor;
  floor.position = {0, 0, 0};
  floor.half_extents = {10, 10, 10};
  const entity_uid_t floor_uid = test_map.add_geometry(floor);

  // Add a Player Spawn marker
  auto spawn_ent = shared::create_entity_by_classname("player_start");
  if (auto *p = shared::entity_as<network::Player_Spawn_Entity>(spawn_ent.get()))
  {
    p->position = {5, 5, 0};
  }
  const entity_uid_t spawn_uid = test_map.add_entity(spawn_ent);

  // Geometry and entities share one uid space, so the two must differ.
  if (floor_uid != 1 || spawn_uid != 2)
  {
    log_error("uids should be allocated from one shared counter; got floor={}, "
              "spawn={}",
              floor_uid, spawn_uid);
    return 1;
  }

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

  // The session copies the map's geometry; entities go to the entity system.
  if (session.geometry.size() != 1)
  {
    log_error("Session geometry size mismatch. Expected 1, got {}",
              session.geometry.size());
    return 1;
  }

  if (session.geometry[0].uid != floor_uid)
  {
    log_error("Session geometry uid mismatch. Expected {}, got {}", floor_uid,
              session.geometry[0].uid);
    return 1;
  }

  // The session's geometry must be a COPY, not an alias of the map's. Editing
  // the session must not write through into the map — the old shared_ptr path
  // did exactly that, and P7's ownership work depends on it staying broken apart.
  {
    box_geometry_t *session_box =
        std::get_if<box_geometry_t>(&session.geometry[0].value);
    if (!session_box)
    {
      log_error("Session geometry 0 should be a box");
      return 1;
    }
    session_box->half_extents = {99, 99, 99};

    const map_geometry_t *map_entry = test_map.find_geometry_by_uid(floor_uid);
    const box_geometry_t *map_box =
        map_entry ? std::get_if<box_geometry_t>(&map_entry->value) : nullptr;
    if (!map_box)
    {
      log_error("Map geometry {} should still be a box", floor_uid);
      return 1;
    }
    if (map_box->half_extents.x != 10.f)
    {
      log_error("Session aliases the map's geometry: writing the session's copy "
                "changed the map's half_extents to {}",
                map_box->half_extents.x);
      return 1;
    }

    // Put it back so the BVH check below still describes the real geometry.
    session_box->half_extents = {10, 10, 10};
  }

  // Verify the runtime spawn counter was seeded past the highest map uid.
  // The box is uid 1, the spawn is uid 2, so map.next_uid is 3 → entity_system
  // next_entity_id must also be 3 so the next spawn() can't collide.
  if (session.entity_system.next_entity_id != 3)
  {
    log_error("entity_system.next_entity_id should be seeded to 3 (= map.next_uid), got {}",
              session.entity_system.next_entity_id);
    return 1;
  }

  // Verify BVH (built over the session's geometry)
  if (session.bvh.nodes.empty())
  {
    log_error("BVH is empty; it should have one leaf for the box brush");
    return 1;
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
  if (spawn.entity_id != spawn_uid)
  {
    log_error("Spawn marker entity_id should equal map uid {}, got {}", spawn_uid,
              spawn.entity_id);
    return 1;
  }

  log_error("Session Test Passed!");
  return 0;
}

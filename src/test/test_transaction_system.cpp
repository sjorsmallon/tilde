#include "client/editor/transaction_system.hpp"
#include "shared/entities/static_entities.hpp"
#include "shared/map.hpp"
#include <cassert>
#include <iostream>

using namespace client;
using namespace shared;
using namespace network;

void test_add_remove()
{
  std::cout << "Testing Add/Remove..." << std::endl;
  Transaction_System ts;
  map_t map;

  // Initial state
  assert(map.entities.empty());

  // 1. Add
  {
    Editor_Transaction t(&ts, &map, "Add AABB");

    auto ent = std::make_shared<AABB_Entity>();
    ent->center = {0, 0, 0};
    ent->half_extents = {1, 1, 1};

    shared::entity_placement_t placement;
    placement.entity = ent;
    placement.position = {0, 0, 0};
    placement.scale = {1, 1, 1};
    placement.rotation = {0, 0, 0};
    map.entities.push_back(placement);
  } // Commit

  assert(map.entities.size() == 1);
  assert(ts.can_undo());
  assert(!ts.can_redo());

  // 2. Undo Add
  ts.undo(map);
  assert(map.entities.empty());
  assert(!ts.can_undo());
  assert(ts.can_redo());

  // 3. Redo Add
  ts.redo(map);
  assert(map.entities.size() == 1);

  // 4. Remove
  {
    auto placement = map.entities[0];
    ts.commit_remove(0, placement.entity.get(), "aabb_entity");
    map.entities.erase(map.entities.begin());
  }

  assert(map.entities.empty());
  assert(ts.can_undo());

  // 5. Undo Remove
  ts.undo(map);
  assert(map.entities.size() == 1);
  // Check if restored entity has correct properties would be good too

  // 6. Redo Remove
  ts.redo(map);
  assert(map.entities.empty());

  std::cout << "Add/Remove Passed." << std::endl;
}

void test_modify()
{
  std::cout << "Testing Modify..." << std::endl;
  Transaction_System ts;
  map_t map;

  // Setup
  auto ent = std::make_shared<AABB_Entity>();
  ent->center = {0, 0, 0};

  shared::entity_placement_t placement;
  placement.entity = ent;
  placement.position = {0, 0, 0};
  placement.scale = {1, 1, 1};
  placement.rotation = {0, 0, 0};
  map.entities.push_back(placement);

  // 1. Modify
  {
    Editor_Transaction t(&ts, &map, (uint32_t)0);

    // Modify the object
    if (auto *aabb =
            dynamic_cast<AABB_Entity *>(map.entities[0].entity.get()))
    {
      aabb->center = {10.0f, 0, 0};
    }
  } // Commit

  auto *aabb = dynamic_cast<AABB_Entity *>(map.entities[0].entity.get());
  assert(aabb);
  assert(aabb->center.x == 10.0f);
  assert(ts.can_undo());

  // 2. Undo Modify
  ts.undo(map);
  aabb = dynamic_cast<AABB_Entity *>(map.entities[0].entity.get());
  assert(aabb->center.x == 0.0f);

  // 3. Redo Modify
  ts.redo(map);
  aabb = dynamic_cast<AABB_Entity *>(map.entities[0].entity.get());
  assert(aabb->center.x == 10.0f);

  std::cout << "Modify Passed." << std::endl;
}

int main()
{
  test_add_remove();
  test_modify();
  std::cout << "All Transaction Logic Tests Passed." << std::endl;
  return 0;
}

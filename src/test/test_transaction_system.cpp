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

  // 1. Add via Edit_Recorder
  entity_uid_t added_uid;
  {
    Edit_Recorder edit(map);
    auto ent = std::make_shared<AABB_Entity>();
    ent->position = {0, 0, 0};
    ent->half_extents = {1, 1, 1};
    added_uid = edit.add(ent);
    auto txn = edit.take();
    assert(txn.has_value());
    ts.push(*txn);
  }

  assert(map.entities.size() == 1);
  assert(map.find_by_uid(added_uid) != nullptr);
  assert(ts.can_undo());
  assert(!ts.can_redo());

  // 2. Undo Add
  ts.undo(map);
  assert(map.entities.empty());
  assert(map.find_by_uid(added_uid) == nullptr);
  assert(!ts.can_undo());
  assert(ts.can_redo());

  // 3. Redo Add
  ts.redo(map);
  assert(map.entities.size() == 1);
  assert(map.find_by_uid(added_uid) != nullptr);

  // 4. Remove via Edit_Recorder
  {
    Edit_Recorder edit(map);
    edit.remove(added_uid);
    auto txn = edit.take();
    assert(txn.has_value());
    ts.push(*txn);
  }

  assert(map.entities.empty());
  assert(ts.can_undo());

  // 5. Undo Remove — entity comes back with same uid
  ts.undo(map);
  assert(map.entities.size() == 1);
  assert(map.find_by_uid(added_uid) != nullptr);

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
  ent->position = {0, 0, 0};
  entity_uid_t uid = map.add_entity(ent);

  // 1. Modify via Edit_Recorder
  {
    Edit_Recorder edit(map);
    edit.track(uid);

    auto *entry = map.find_by_uid(uid);
    auto *aabb = dynamic_cast<AABB_Entity *>(entry->entity.get());
    aabb->position = {10.0f, 0, 0};

    edit.finish(uid);
    auto txn = edit.take();
    assert(txn.has_value());
    ts.push(*txn);
  }

  auto *entry = map.find_by_uid(uid);
  auto *aabb = dynamic_cast<AABB_Entity *>(entry->entity.get());
  assert(aabb);
  assert(aabb->position.x == 10.0f);
  assert(ts.can_undo());

  // 2. Undo Modify
  ts.undo(map);
  entry = map.find_by_uid(uid);
  aabb = dynamic_cast<AABB_Entity *>(entry->entity.get());
  assert(aabb->position.x == 0.0f);

  // 3. Redo Modify
  ts.redo(map);
  entry = map.find_by_uid(uid);
  aabb = dynamic_cast<AABB_Entity *>(entry->entity.get());
  assert(aabb->position.x == 10.0f);

  std::cout << "Modify Passed." << std::endl;
}

void test_batch_delete()
{
  std::cout << "Testing Batch Delete..." << std::endl;
  Transaction_System ts;
  map_t map;

  // Add 3 entities
  auto e1 = std::make_shared<AABB_Entity>();
  e1->position = {1, 0, 0};
  entity_uid_t uid1 = map.add_entity(e1);

  auto e2 = std::make_shared<AABB_Entity>();
  e2->position = {2, 0, 0};
  entity_uid_t uid2 = map.add_entity(e2);

  auto e3 = std::make_shared<AABB_Entity>();
  e3->position = {3, 0, 0};
  entity_uid_t uid3 = map.add_entity(e3);

  assert(map.entities.size() == 3);

  // Batch delete all 3 in one transaction
  {
    Edit_Recorder edit(map);
    edit.remove(uid1);
    edit.remove(uid2);
    edit.remove(uid3);
    auto txn = edit.take();
    assert(txn.has_value());
    assert(txn->deltas.size() == 3);
    ts.push(*txn);
  }

  assert(map.entities.empty());

  // Single undo restores all 3
  ts.undo(map);
  assert(map.entities.size() == 3);
  assert(map.find_by_uid(uid1) != nullptr);
  assert(map.find_by_uid(uid2) != nullptr);
  assert(map.find_by_uid(uid3) != nullptr);

  // Verify positions are correct
  auto *r1 = dynamic_cast<AABB_Entity *>(map.find_by_uid(uid1)->entity.get());
  auto *r2 = dynamic_cast<AABB_Entity *>(map.find_by_uid(uid2)->entity.get());
  auto *r3 = dynamic_cast<AABB_Entity *>(map.find_by_uid(uid3)->entity.get());
  assert(r1->position.x == 1.0f);
  assert(r2->position.x == 2.0f);
  assert(r3->position.x == 3.0f);

  // Redo removes all 3 again
  ts.redo(map);
  assert(map.entities.empty());

  std::cout << "Batch Delete Passed." << std::endl;
}

int main()
{
  test_add_remove();
  test_modify();
  test_batch_delete();
  std::cout << "All Transaction Logic Tests Passed." << std::endl;
  return 0;
}

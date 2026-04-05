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

  // 1. Add entity and record diff
  auto ent = std::make_shared<AABB_Entity>();
  ent->position = {0, 0, 0};
  ent->half_extents = {1, 1, 1};
  entity_uid_t added_uid = map.add_entity(ent);

  {
    transaction_builder_t builder;
    builder.add_created(added_uid, snapshot_entity(ent.get()));
    ts.push(builder.take());
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

  // 4. Remove entity and record diff
  {
    auto *entry = map.find_by_uid(added_uid);
    transaction_builder_t builder;
    builder.add_removed(added_uid, snapshot_entity(entry->entity.get()));
    map.remove_entity(added_uid);
    ts.push(builder.take());
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

  // 1. Modify via snapshot/diff
  {
    auto *entry = map.find_by_uid(uid);
    auto before = entry->entity->get_all_properties();

    auto *aabb = dynamic_cast<AABB_Entity *>(entry->entity.get());
    aabb->position = {10.0f, 0, 0};

    transaction_builder_t builder;
    builder.add_modified_from_diff(uid, before,
                                   entry->entity->get_all_properties());
    ts.push(builder.take());
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
    transaction_builder_t builder;
    auto *r1 = map.find_by_uid(uid1);
    auto *r2 = map.find_by_uid(uid2);
    auto *r3 = map.find_by_uid(uid3);
    builder.add_removed(uid1, snapshot_entity(r1->entity.get()));
    builder.add_removed(uid2, snapshot_entity(r2->entity.get()));
    builder.add_removed(uid3, snapshot_entity(r3->entity.get()));
    map.remove_entity(uid1);
    map.remove_entity(uid2);
    map.remove_entity(uid3);
    assert(builder.diffs.size() == 3);
    ts.push(builder.take());
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

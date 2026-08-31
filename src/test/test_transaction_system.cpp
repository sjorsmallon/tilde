#include "../shared/entities/entity_reflection.hpp"
#include "client/editor/transaction_system.hpp"
#include "shared/map.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace client;
using namespace shared;
using namespace network;

// A stand-in entity for the entity-flavor tests. Trigger_Volume_Entity is used
// because it's the box-volume entity that survived the geometry exit — the box
// brushes these tests used to build are geometry now, and exercise the OTHER
// flavor (see the geometry tests below).
static std::shared_ptr<entities::Trigger_Volume_Entity> make_test_entity(float x)
{
  auto entity = std::make_shared<entities::Trigger_Volume_Entity>();
  entity->position = {x, 0, 0};
  entity->volume.half_extents = {1, 1, 1};
  return entity;
}

static brush_geometry_t make_test_box(float x)
{
  return make_box_brush({x, 0, 0}, {1, 1, 1});
}

void test_add_remove()
{
  std::cout << "Testing Add/Remove..." << std::endl;
  Transaction_System ts;
  map_t map;

  // Initial state
  assert(map.entities.empty());

  // 1. Add entity and record diff
  auto ent = make_test_entity(0.f);
  entity_uid_t added_uid = map.add_entity(ent);

  {
    transaction_t transaction;
    transaction.add_created(added_uid, snapshot_entity(ent.get()));
    ts.push(std::move(transaction));
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
    transaction_t transaction;
    transaction.add_removed(added_uid, snapshot_entity(entry->entity.get()));
    map.remove_entity(added_uid);
    ts.push(std::move(transaction));
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
  auto ent = make_test_entity(0.f);
  entity_uid_t uid = map.add_entity(ent);

  // 1. Modify via snapshot/diff
  {
    auto *entry = map.find_by_uid(uid);
    entity_snapshot_t before = snapshot_entity(entry->entity.get());

    entry->entity->position = {10.0f, 0, 0};

    transaction_t transaction;
    transaction.add_modified_from_diff(uid, before, entry->entity.get());
    ts.push(std::move(transaction));
  }

  assert(map.find_by_uid(uid)->entity->position.x == 10.0f);
  assert(ts.can_undo());

  // 2. Undo Modify
  ts.undo(map);
  assert(map.find_by_uid(uid)->entity->position.x == 0.0f);

  // 3. Redo Modify
  ts.redo(map);
  assert(map.find_by_uid(uid)->entity->position.x == 10.0f);

  std::cout << "Modify Passed." << std::endl;
}

// The reason the entity flavor stopped being string-based. The old
// diff_properties compared "%.6f"-formatted text, so a change below that
// threshold produced NO diff at all and the edit silently vanished. memcmp over
// the field bytes sees it. Same assertion the geometry flavor already makes.
void test_modify_thresholds()
{
  std::cout << "Testing Modify thresholds..." << std::endl;
  Transaction_System ts;
  map_t map;

  auto ent = make_test_entity(0.f);
  entity_uid_t uid = map.add_entity(ent);
  auto *entry = map.find_by_uid(uid);

  // No change at all -> no transaction.
  {
    transaction_t transaction;
    transaction.add_modified_from_diff(uid, snapshot_entity(entry->entity.get()),
                                   entry->entity.get());
    assert(transaction.diffs.empty());
    ts.push(std::move(transaction));
  }
  assert(!ts.can_undo());

  // A change far below what "%.6f" would print is still a real change.
  const float tiny = 1e-9f;
  {
    entity_snapshot_t before = snapshot_entity(entry->entity.get());
    entry->entity->position = {tiny, 0, 0};

    transaction_t transaction;
    transaction.add_modified_from_diff(uid, before, entry->entity.get());
    assert(transaction.diffs.size() == 1);
    ts.push(std::move(transaction));
  }
  assert(ts.can_undo());

  ts.undo(map);
  assert(map.find_by_uid(uid)->entity->position.x == 0.f);
  ts.redo(map);
  assert(map.find_by_uid(uid)->entity->position.x == tiny);

  std::cout << "Modify thresholds Passed." << std::endl;
}

// A component field (Trigger_Volume's Box_Volume) is one memcmp/memcpy over the
// whole nested struct, so it must round-trip like any other field. The string
// flavor reached it only via init_from_map's "volume" special case.
void test_modify_nested_field()
{
  std::cout << "Testing Modify nested field..." << std::endl;
  Transaction_System ts;
  map_t map;

  entity_uid_t uid = map.add_entity(make_test_entity(0.f));
  auto *entry = map.find_by_uid(uid);

  entity_snapshot_t before = snapshot_entity(entry->entity.get());
  auto *trigger = entities::entity_as<entities::Trigger_Volume_Entity>(entry->entity.get());
  assert(trigger != nullptr);
  trigger->volume.half_extents = {8.f, 9.f, 10.f};

  {
    transaction_t transaction;
    transaction.add_modified_from_diff(uid, before, entry->entity.get());
    assert(transaction.diffs.size() == 1);
    ts.push(std::move(transaction));
  }

  ts.undo(map);
  assert(entities::entity_as<entities::Trigger_Volume_Entity>(map.find_by_uid(uid)->entity.get())
             ->volume.half_extents.x == 1.f);

  ts.redo(map);
  assert(entities::entity_as<entities::Trigger_Volume_Entity>(map.find_by_uid(uid)->entity.get())
             ->volume.half_extents.y == 9.f);

  std::cout << "Modify nested field Passed." << std::endl;
}

// Created/removed entries store a whole cloned entity. The clone is a byte copy,
// NOT a serialize/deserialize round trip — write_coord quantizes floats to ~1/32,
// so a bitstream-based snapshot would snap these values on undo.
void test_snapshot_is_exact()
{
  std::cout << "Testing Snapshot exactness..." << std::endl;
  Transaction_System ts;
  map_t map;

  // Deliberately not representable in write_coord's 5-bit fraction.
  const float awkward = 3.14159265f;
  auto ent = make_test_entity(0.f);
  ent->position = {awkward, -awkward, 0.001f};
  entity_uid_t uid = map.add_entity(ent);

  {
    transaction_t transaction;
    transaction.add_removed(uid, snapshot_entity(map.find_by_uid(uid)->entity.get()));
    map.remove_entity(uid);
    ts.push(std::move(transaction));
  }
  assert(map.entities.empty());

  ts.undo(map);
  {
    const auto *restored = map.find_by_uid(uid);
    assert(restored && restored->entity);
    assert(restored->entity->position.x == awkward);
    assert(restored->entity->position.y == -awkward);
    assert(restored->entity->position.z == 0.001f);
    // A fresh object, not the map's original and not the stack's snapshot.
    assert(restored->entity.get() != ent.get());
  }

  // Restoring a second time must produce another independent entity, so a
  // caller editing the restored one can't reach back into the undo stack.
  ts.redo(map);
  ts.undo(map);
  {
    const auto *restored = map.find_by_uid(uid);
    assert(restored && restored->entity);
    assert(restored->entity->position.x == awkward);
  }

  std::cout << "Snapshot exactness Passed." << std::endl;
}

void test_batch_delete()
{
  std::cout << "Testing Batch Delete..." << std::endl;
  Transaction_System ts;
  map_t map;

  // Add 3 entities
  entity_uid_t uid1 = map.add_entity(make_test_entity(1.f));
  entity_uid_t uid2 = map.add_entity(make_test_entity(2.f));
  entity_uid_t uid3 = map.add_entity(make_test_entity(3.f));

  assert(map.entities.size() == 3);

  // Batch delete all 3 in one transaction
  {
    transaction_t transaction;
    auto *r1 = map.find_by_uid(uid1);
    auto *r2 = map.find_by_uid(uid2);
    auto *r3 = map.find_by_uid(uid3);
    transaction.add_removed(uid1, snapshot_entity(r1->entity.get()));
    transaction.add_removed(uid2, snapshot_entity(r2->entity.get()));
    transaction.add_removed(uid3, snapshot_entity(r3->entity.get()));
    map.remove_entity(uid1);
    map.remove_entity(uid2);
    map.remove_entity(uid3);
    assert(transaction.diffs.size() == 3);
    ts.push(std::move(transaction));
  }

  assert(map.entities.empty());

  // Single undo restores all 3
  ts.undo(map);
  assert(map.entities.size() == 3);
  assert(map.find_by_uid(uid1) != nullptr);
  assert(map.find_by_uid(uid2) != nullptr);
  assert(map.find_by_uid(uid3) != nullptr);

  // Verify positions are correct
  assert(map.find_by_uid(uid1)->entity->position.x == 1.0f);
  assert(map.find_by_uid(uid2)->entity->position.x == 2.0f);
  assert(map.find_by_uid(uid3)->entity->position.x == 3.0f);

  // Redo removes all 3 again
  ts.redo(map);
  assert(map.entities.empty());

  std::cout << "Batch Delete Passed." << std::endl;
}

// ===================================================================
// Geometry value-swap flavor
// ===================================================================

void test_geometry_add_remove()
{
  std::cout << "Testing Geometry Add/Remove..." << std::endl;
  Transaction_System ts;
  map_t map;

  assert(map.geometry.empty());

  // 1. Add a box brush
  const brush_geometry_t box = make_test_box(4.f);
  entity_uid_t uid = map.add_geometry(box);
  {
    transaction_t transaction;
    transaction.add_geometry_created(uid, box);
    ts.push(std::move(transaction));
  }

  assert(map.geometry.size() == 1);
  assert(map.find_geometry_by_uid(uid) != nullptr);
  assert(map.has_object(uid));

  // 2. Undo / redo the add
  ts.undo(map);
  assert(map.geometry.empty());
  assert(!map.has_object(uid));

  ts.redo(map);
  assert(map.geometry.size() == 1);
  assert(geometry_values_equal(map.find_geometry_by_uid(uid)->value, box));

  // 3. Remove, then undo — the whole value comes back, same uid
  {
    transaction_t transaction;
    transaction.add_geometry_removed(uid, map.find_geometry_by_uid(uid)->value);
    map.remove_geometry(uid);
    ts.push(std::move(transaction));
  }
  assert(map.geometry.empty());

  ts.undo(map);
  assert(map.geometry.size() == 1);
  assert(geometry_values_equal(map.find_geometry_by_uid(uid)->value, box));

  ts.redo(map);
  assert(map.geometry.empty());

  std::cout << "Geometry Add/Remove Passed." << std::endl;
}

void test_geometry_modify()
{
  std::cout << "Testing Geometry Modify..." << std::endl;
  Transaction_System ts;
  map_t map;

  entity_uid_t uid = map.add_geometry(make_test_box(0.f));

  const geometry_value_t before = map.find_geometry_by_uid(uid)->value;
  translate_brush(std::get<brush_geometry_t>(map.find_geometry_by_uid(uid)->value),
                  {10.f, 0, 0});

  {
    transaction_t transaction;
    transaction.add_geometry_modified(uid, before,
                                  map.find_geometry_by_uid(uid)->value);
    ts.push(std::move(transaction));
  }
  assert(ts.can_undo());
  assert(get_position(map.find_geometry_by_uid(uid)->value).x == 10.f);

  ts.undo(map);
  assert(get_position(map.find_geometry_by_uid(uid)->value).x == 0.f);

  ts.redo(map);
  assert(get_position(map.find_geometry_by_uid(uid)->value).x == 10.f);

  std::cout << "Geometry Modify Passed." << std::endl;
}

// The value-swap flavor must not push an entry when nothing changed (a click
// without a drag), and — unlike the entity flavor's formatted-float compare — it
// must NOT lose a change too small to survive being printed to 6 decimals.
void test_geometry_modify_thresholds()
{
  std::cout << "Testing Geometry Modify thresholds..." << std::endl;
  Transaction_System ts;
  map_t map;

  entity_uid_t uid = map.add_geometry(make_test_box(0.f));
  const geometry_value_t unchanged = map.find_geometry_by_uid(uid)->value;

  // No change at all → no transaction.
  {
    transaction_t transaction;
    transaction.add_geometry_modified(uid, unchanged, unchanged);
    assert(transaction.diffs.empty());
    ts.push(std::move(transaction));
  }
  assert(!ts.can_undo());

  // One ULP on a single vertex: far below what "%.6f" would print, and still a
  // real change.
  const geometry_value_t before = map.find_geometry_by_uid(uid)->value;
  const float            original_x =
      std::get<brush_geometry_t>(before).hull_points[0].x;
  const float nudged_x = std::nextafterf(original_x, original_x + 1.f);
  assert(nudged_x != original_x);
  std::get<brush_geometry_t>(map.find_geometry_by_uid(uid)->value).hull_points[0].x =
      nudged_x;

  {
    transaction_t transaction;
    transaction.add_geometry_modified(uid, before,
                                  map.find_geometry_by_uid(uid)->value);
    assert(transaction.diffs.size() == 1);
    ts.push(std::move(transaction));
  }
  assert(ts.can_undo());

  ts.undo(map);
  assert(std::get<brush_geometry_t>(map.find_geometry_by_uid(uid)->value)
             .hull_points[0].x == original_x);
  ts.redo(map);
  assert(std::get<brush_geometry_t>(map.find_geometry_by_uid(uid)->value)
             .hull_points[0].x == nudged_x);

  std::cout << "Geometry Modify thresholds Passed." << std::endl;
}

// A sculpted face's snapshot is its whole grid, so verify the grid actually
// round-trips through undo/redo rather than just the transform. This is what
// the displacement test became when a displacement became a brush with one
// subdivided face.
void test_geometry_face_grid()
{
  std::cout << "Testing Geometry face grid..." << std::endl;
  Transaction_System ts;
  map_t map;

  brush_geometry_t brush = make_box_brush({0, 0, 0}, {64, 16, 64});
  sync_face_surfaces(brush);

  Plane top;
  top.normal = {0, 1, 0};
  top.point  = {0, 16, 0};
  resize_face_grid(face_surface_for(brush, top), 4);

  entity_uid_t uid = map.add_geometry(brush);

  const geometry_value_t before = map.find_geometry_by_uid(uid)->value;

  auto &live = std::get<brush_geometry_t>(map.find_geometry_by_uid(uid)->value);
  face_surface_for(live, top).offsets[2 * 5 + 2] = {0, 32.f, 0};

  {
    transaction_t transaction;
    transaction.add_geometry_modified(uid, before,
                                  map.find_geometry_by_uid(uid)->value);
    assert(transaction.diffs.size() == 1);
    ts.push(std::move(transaction));
  }

  ts.undo(map);
  {
    auto &restored = std::get<brush_geometry_t>(map.find_geometry_by_uid(uid)->value);
    const face_surface_t &face = face_surface_for(restored, top);
    assert(face.offsets[2 * 5 + 2].y == 0.f);
    assert(face.offsets.size() == (size_t)face_grid_vertex_count(face.subdivision_level));
  }

  ts.redo(map);
  {
    auto &restored = std::get<brush_geometry_t>(map.find_geometry_by_uid(uid)->value);
    assert(face_surface_for(restored, top).offsets[2 * 5 + 2].y == 32.f);
  }

  std::cout << "Geometry face grid Passed." << std::endl;
}

// A batch delete spanning both regimes is one transaction and one undo. This is
// the case the editor actually hits: a box select grabs brushes and spawn
// markers together.
void test_mixed_batch_delete()
{
  std::cout << "Testing Mixed Batch Delete..." << std::endl;
  Transaction_System ts;
  map_t map;

  entity_uid_t box_uid = map.add_geometry(make_test_box(1.f));
  entity_uid_t entity_uid = map.add_entity(make_test_entity(2.f));
  entity_uid_t box2_uid = map.add_geometry(make_test_box(3.f));

  // uids come from one counter across both lists.
  assert(box_uid == 1 && entity_uid == 2 && box2_uid == 3);
  assert(map.object_count() == 3);

  {
    transaction_t transaction;
    transaction.add_geometry_removed(box_uid, map.find_geometry_by_uid(box_uid)->value);
    transaction.add_removed(entity_uid,
                        snapshot_entity(map.find_by_uid(entity_uid)->entity.get()));
    transaction.add_geometry_removed(box2_uid, map.find_geometry_by_uid(box2_uid)->value);

    assert(map.remove_object(box_uid));
    assert(map.remove_object(entity_uid));
    assert(map.remove_object(box2_uid));

    assert(transaction.diffs.size() == 3);
    ts.push(std::move(transaction));
  }

  assert(map.object_count() == 0);

  // One undo brings back all three, regardless of regime.
  ts.undo(map);
  assert(map.object_count() == 3);
  assert(map.has_object(box_uid));
  assert(map.has_object(entity_uid));
  assert(map.has_object(box2_uid));
  assert(get_position(map.find_geometry_by_uid(box_uid)->value).x == 1.f);
  assert(map.find_by_uid(entity_uid)->entity->position.x == 2.f);
  assert(get_position(map.find_geometry_by_uid(box2_uid)->value).x == 3.f);

  ts.redo(map);
  assert(map.object_count() == 0);

  std::cout << "Mixed Batch Delete Passed." << std::endl;
}

void test_map_cvars()
{
  std::cout << "Testing Map Cvars..." << std::endl;
  Transaction_System ts;
  map_t map;

  // Add.
  {
    transaction_t transaction;
    transaction.add_map_cvars_modified(map.attached_cvars, {"g_gravity 200"});
    map.attached_cvars = {"g_gravity 200"};
    ts.push(std::move(transaction));
  }

  // Edit the value.
  {
    transaction_t transaction;
    transaction.add_map_cvars_modified(map.attached_cvars, {"g_gravity 120"});
    map.attached_cvars = {"g_gravity 120"};
    ts.push(std::move(transaction));
  }

  assert(map.attached_cvars.size() == 1);
  assert(map.attached_cvars[0] == "g_gravity 120");

  ts.undo(map);
  assert(map.attached_cvars[0] == "g_gravity 200");
  ts.undo(map);
  assert(map.attached_cvars.empty());

  ts.redo(map);
  assert(map.attached_cvars[0] == "g_gravity 200");
  ts.redo(map);
  assert(map.attached_cvars[0] == "g_gravity 120");

  // An edit that changes nothing must not push an entry the author has to undo
  // twice -- clicking into a value box and back out is not an edit.
  {
    transaction_t transaction;
    transaction.add_map_cvars_modified(map.attached_cvars, map.attached_cvars);
    assert(transaction.diffs.empty());
  }

  // The list is independent of the object lists: undoing a cvar edit must not
  // disturb geometry, and vice versa.
  {
    const entity_uid_t box_uid = map.add_geometry(make_test_box(1.f));

    transaction_t transaction;
    transaction.add_map_cvars_modified(map.attached_cvars, {});
    map.attached_cvars = {};
    ts.push(std::move(transaction));

    ts.undo(map);
    assert(map.attached_cvars.size() == 1);
    assert(map.has_object(box_uid));
  }

  std::cout << "Map Cvars Passed." << std::endl;
}

int main()
{
  test_add_remove();
  test_modify();
  test_modify_thresholds();
  test_modify_nested_field();
  test_snapshot_is_exact();
  test_batch_delete();
  test_geometry_add_remove();
  test_geometry_modify();
  test_geometry_modify_thresholds();
  test_geometry_face_grid();
  test_mixed_batch_delete();
  test_map_cvars();
  std::cout << "All Transaction Logic Tests Passed." << std::endl;
  return 0;
}

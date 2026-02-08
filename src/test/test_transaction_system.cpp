#include "client/editor/transaction_system.hpp"
#include <cassert>
#include <iostream>

using namespace client;
using namespace shared;

// Mock Schema for a simple struct to test generic modify
struct TestObject
{
  int x = 0;
  float y = 0.0f;
  DECLARE_SCHEMA(TestObject);
};

BEGIN_SCHEMA(TestObject)
DEFINE_FIELD(x, network::Field_Type::Int32)
DEFINE_FIELD(y, network::Field_Type::Float32)
END_SCHEMA(TestObject)

void test_add_remove()
{
  std::cout << "Testing Add/Remove..." << std::endl;
  Transaction_System ts;
  map_t map;

  // Initial state
  assert(map.static_geometry.empty());

  // 1. Add
  {
    Editor_Transaction t(&ts, &map, "Add Box");
    aabb_t box;
    box.center = {0, 0, 0};
    static_geometry_t geo = {box};
    map.static_geometry.push_back(geo);
  } // Commit

  assert(map.static_geometry.size() == 1);
  assert(ts.can_undo());
  assert(!ts.can_redo());

  // 2. Undo Add
  ts.undo(map);
  assert(map.static_geometry.empty());
  assert(!ts.can_undo());
  assert(ts.can_redo());

  // 3. Redo Add
  ts.redo(map);
  assert(map.static_geometry.size() == 1);

  // 4. Remove
  {
    // For remove, we need to manually invoke commit_remove or use a transaction
    // wrapper if we had one for remove? The placement tool uses commit_remove
    // directly. Let's use ts.commit_remove directly as the tool does.
    auto obj = map.static_geometry[0]; // Snapshot
    ts.commit_remove(0, obj);
    map.static_geometry.erase(map.static_geometry.begin());
  }

  assert(map.static_geometry.empty());
  assert(ts.can_undo());

  // 5. Undo Remove
  ts.undo(map);
  assert(map.static_geometry.size() == 1);

  // 6. Redo Remove
  ts.redo(map);
  assert(map.static_geometry.empty());

  std::cout << "Add/Remove Passed." << std::endl;
}

void test_modify()
{
  std::cout << "Testing Modify..." << std::endl;
  Transaction_System ts;
  map_t map;

  // Setup
  aabb_t box;
  box.center = {0, 0, 0};
  map.static_geometry.push_back({box});

  // 1. Modify
  {
    Editor_Transaction t(&ts, &map, (uint32_t)0);

    // Modify the object
    // We need to modify using std::get to be sure
    if (std::holds_alternative<aabb_t>(map.static_geometry[0].data))
    {
      std::get<aabb_t>(map.static_geometry[0].data).center.x = 10.0f;
    }
  } // Commit

  auto &geo = std::get<aabb_t>(map.static_geometry[0].data);
  assert(geo.center.x == 10.0f);
  assert(ts.can_undo());

  // 2. Undo Modify
  ts.undo(map);
  auto &geo_undo = std::get<aabb_t>(map.static_geometry[0].data);
  assert(geo_undo.center.x == 0.0f);

  // 3. Redo Modify
  ts.redo(map);
  auto &geo_redo = std::get<aabb_t>(map.static_geometry[0].data);
  assert(geo_redo.center.x == 10.0f);

  std::cout << "Modify Passed." << std::endl;
}

int main()
{
  test_add_remove();
  test_modify();
  std::cout << "All Transaction Logic Tests Passed." << std::endl;
  return 0;
}
